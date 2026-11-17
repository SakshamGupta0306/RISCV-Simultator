/*
 * Five-stage RV32I pipeline: IF -> ID -> EX -> MEM -> WB.
 *
 * Data-hazard policies (p->forwarding):
 *   --pipeline         : stall against all three downstream latches.
 *   --pipeline-forward : EX/MEM -> EX and MEM/WB -> EX forwarding; only
 *                        stall remaining is load-use (1 cycle).
 *
 * Branch prediction (p->bp_mode, Lab 4):
 *   The predictor is consulted in IF. If it predicts taken we immediately
 *   fetch from the branch target. In EX we know the actual outcome and
 *   update the predictor. If the prediction was wrong we flush the
 *   wrong-path instruction in IF/ID and redirect IF.
 *
 *   Unconditional jumps (jal / jalr) are never predicted; they always
 *   flush the IF/ID latch and redirect in EX (1-cycle penalty each).
 *
 * Every stage reads the OLD latches (p->...) and writes NEW ones (nx...);
 * `*p = nx` at the bottom is the clock edge.
 *
 * Set RVSIM_TRACE=1 in the environment for a per-cycle trace on stderr.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pipeline_state.h"
#include "hazard.h"
#include "cli_display.h"

/* ------------------------------------------------------------------ */
/* ALU                                                                 */
/* ------------------------------------------------------------------ */
static uint32_t alu(const DecodedInstr *i, uint32_t pc, uint32_t a, uint32_t b) {
    uint32_t imm = (uint32_t)i->imm;

    switch (i->opcode) {
        case 0x37: return imm;           /* lui   */
        case 0x17: return pc + imm;      /* auipc */
        case 0x03:                       /* loads:  effective address */
        case 0x23:                       /* stores: effective address */
            return a + imm;

        case 0x13:                       /* I-type ALU */
            switch (i->funct3) {
                case 0x0: return a + imm;
                case 0x2: return ((int32_t)a < (int32_t)imm) ? 1u : 0u;
                case 0x3: return (a < imm) ? 1u : 0u;
                case 0x4: return a ^ imm;
                case 0x6: return a | imm;
                case 0x7: return a & imm;
                case 0x1: return a << (imm & 0x1F);
                case 0x5:
                    if (i->funct7 == 0x20)
                        return (uint32_t)((int32_t)a >> (imm & 0x1F));
                    return a >> (imm & 0x1F);
            }
            break;

        case 0x33:                       /* R-type */
            switch (i->funct3) {
                case 0x0: return (i->funct7 == 0x20) ? a - b : a + b;
                case 0x1: return a << (b & 0x1F);
                case 0x2: return ((int32_t)a < (int32_t)b) ? 1u : 0u;
                case 0x3: return (a < b) ? 1u : 0u;
                case 0x4: return a ^ b;
                case 0x5:
                    if (i->funct7 == 0x20)
                        return (uint32_t)((int32_t)a >> (b & 0x1F));
                    return a >> (b & 0x1F);
                case 0x6: return a | b;
                case 0x7: return a & b;
            }
            break;
    }
    return 0;
}

/* Branch comparator (used with the forwarded operands). */
static int branch_taken(uint32_t funct3, uint32_t a, uint32_t b) {
    switch (funct3) {
        case 0x0: return a == b;
        case 0x1: return a != b;
        case 0x4: return (int32_t)a <  (int32_t)b;
        case 0x5: return (int32_t)a >= (int32_t)b;
        case 0x6: return a <  b;
        case 0x7: return a >= b;
    }
    return 0;
}

/* ------------------------------------------------------------------ */

void pipeline_init(Pipeline *p, int forwarding, BPMode bp_mode) {
    memset(p, 0, sizeof(*p));
    p->forwarding = forwarding;
    p->bp_mode    = bp_mode;
    /* 2-bit PHT initialised to weakly-not-taken (1) */
    if (bp_mode == BP_2BIT)
        memset(p->pht, 1, sizeof(p->pht));
}

CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem) {
    if (cpu->status != CPU_RUNNING) return cpu->status;

    Pipeline nx = *p;
    nx.if_id.valid  = 0;
    nx.id_ex.valid  = 0;
    nx.ex_mem.valid = 0;
    nx.mem_wb.valid = 0;

    int      flush       = 0;
    int      stall       = 0;
    int      load_use    = 0;
    uint32_t redirect_pc = 0;

    /* ============================ WB ============================ */
    if (p->mem_wb.valid) {
        const MEMWB *w = &p->mem_wb;

        if (w->fault != PF_NONE) {
            if (w->fault == PF_ILLEGAL) {
                uint32_t raw = 0;
                memory_read_word(mem, w->pc, &raw);
                cpu->status     = CPU_ILLEGAL_INSTRUCTION;
                cpu->fault_addr = w->pc;
                snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                         "unknown instruction 0x%08x at pc=0x%08x (no handler registered)",
                         raw, w->pc);
            } else if (w->fault == PF_FETCH) {
                cpu->status     = CPU_MEM_FAULT;
                cpu->fault_addr = w->pc;
                snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                         "instruction fetch out of bounds at pc=0x%08x", w->pc);
            } else {
                cpu->status     = CPU_MEM_FAULT;
                cpu->fault_addr = w->fault_addr;
                snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                         "data access out of bounds at addr=0x%08x (pc=0x%08x)",
                         w->fault_addr, w->pc);
            }
            p->cycles++;
            return cpu->status;
        }

        if (w->ctrl.reg_write)
            cpu_write_reg(cpu, (int)w->instr.rd, w->wb_val);

        if (w->ctrl.is_system) {
            uint32_t saved_pc = cpu->pc;
            DecodedInstr ins  = w->instr;
            cpu->pc = w->pc;
            uint32_t real_a7 = cpu_read_reg(cpu, 17), real_a0 = cpu_read_reg(cpu, 10);
            if (p->no_hazard) {           /* no interlock: ecall uses what ID read */
                cpu_write_reg(cpu, 17, w->sys_a7);
                cpu_write_reg(cpu, 10, w->sys_a0);
            }
            w->handler->execute(cpu, mem, &ins);
            if (p->no_hazard) {
                cpu_write_reg(cpu, 17, real_a7);
                cpu_write_reg(cpu, 10, real_a0);
            }

            if (cpu->status != CPU_RUNNING) {
                /* halt: leave cpu->pc = w->pc (the ecall's address) */
                p->retired++;
                p->cycles++;
                return cpu->status;
            }
            cpu->pc = saved_pc;   /* still running: restore fetch address */
        }
        nx.retired++;
    }

    /* ============================ MEM =========================== */
    if (p->ex_mem.valid) {
        const EXMEM *e = &p->ex_mem;
        MEMWB *o = &nx.mem_wb;

        o->valid      = 1;
        o->pc         = e->pc;
        o->fault      = e->fault;
        o->instr      = e->instr;
        o->handler    = e->handler;
        o->ctrl       = e->ctrl;
        o->sys_a7     = e->sys_a7;
        o->sys_a0     = e->sys_a0;
        o->wb_val     = e->alu_result;
        o->fault_addr = e->fault_addr;

        if (e->fault == PF_NONE) {
            uint32_t  addr = e->alu_result;
            MemStatus ms   = MEM_OK;

            if (e->ctrl.mem_read) {
                uint8_t  b = 0; uint16_t h = 0; uint32_t w = 0;
                switch (e->instr.funct3) {
                    case 0x0: ms = memory_read_byte(mem, addr, &b); o->wb_val = (uint32_t)(int32_t)(int8_t)b;  break;
                    case 0x1: ms = memory_read_half(mem, addr, &h); o->wb_val = (uint32_t)(int32_t)(int16_t)h; break;
                    case 0x2: ms = memory_read_word(mem, addr, &w); o->wb_val = w;                              break;
                    case 0x4: ms = memory_read_byte(mem, addr, &b); o->wb_val = b;                              break;
                    case 0x5: ms = memory_read_half(mem, addr, &h); o->wb_val = h;                              break;
                }
            } else if (e->ctrl.mem_write) {
                switch (e->instr.funct3) {
                    case 0x0: ms = memory_write_byte(mem, addr, (uint8_t) e->store_val); break;
                    case 0x1: ms = memory_write_half(mem, addr, (uint16_t)e->store_val); break;
                    case 0x2: ms = memory_write_word(mem, addr,           e->store_val); break;
                }
            }
            if (ms != MEM_OK) {
                o->fault      = PF_MEM;
                o->fault_addr = addr;
            }
        }
    }

    /* ============================ EX ============================ */
    if (p->id_ex.valid) {
        const IDEX *x = &p->id_ex;
        EXMEM *o = &nx.ex_mem;

        o->valid      = 1;
        o->pc         = x->pc;
        o->fault      = x->fault;
        o->instr      = x->instr;
        o->handler    = x->handler;
        o->ctrl       = x->ctrl;
        o->alu_result = 0;
        o->fault_addr = 0;
        o->sys_a7     = x->rs1_val;
        o->sys_a0     = x->rs2_val;

        Operands op = forward_operands(p, x);
        nx.fwd_ex  += (uint64_t)op.fwd_ex;
        nx.fwd_mem += (uint64_t)op.fwd_mem;
        uint32_t a = op.a, b = op.b;

        o->store_val = b;

        if (x->fault == PF_NONE) {
            if (x->ctrl.is_branch) {
                int taken       = branch_taken(x->instr.funct3, a, b);
                uint32_t target = x->pc + (uint32_t)x->instr.imm;

                /* Lab 4: update predictor, then check if we were right */
                bp_update(&nx, x->pc, taken);   /* nx, not p: *p = nx at commit would erase it */

                /* Misprediction: outcome differs from prediction */
                int mispred = (taken != x->predicted_taken) ||
                              (taken && (target != x->predicted_target));

                if (mispred) {
                    flush       = 1;
                    redirect_pc = taken ? target : (x->pc + 4);
                    nx.mispredicts++;
                    nx.redirects++;
                    nx.squashed += 2;    /* IF/ID + this-cycle IF fetch both squashed */
                }
                /* Lab 4 branch stats (counted when the branch reaches EX) */
                nx.branches++;

            } else if (x->ctrl.is_jump) {
                o->alu_result = x->pc + 4;   /* link value -> rd at WB */

                if (x->instr.opcode == 0x6F) {  /* jal */
                    nx.jal_count++;
                    if (p->bp_mode == BP_NONE) {
                        /* Lab 2/3 behaviour: no pre-decode, EX redirects */
                        flush       = 1;
                        redirect_pc = x->pc + (uint32_t)x->instr.imm;
                        nx.redirects++;
                        nx.squashed += 2;
                    }
                    /* else: IF already redirected from the pre-decoded
                     * immediate, so EX has nothing to fix: free. */
                } else {                         /* jalr: target needs a register */
                    flush       = 1;
                    redirect_pc = (a + (uint32_t)x->instr.imm) & ~1u;
                    nx.jalr_count++;
                    nx.redirects++;
                    nx.squashed += 2;
                }

            } else {
                o->alu_result = alu(&x->instr, x->pc, a, b);
            }
        }
    }

    /* A flush/redirect in EX kills the wrong-path instruction in IF/ID.
     * Flush wins over stall: the stalled instruction was on the wrong path. */
    if (flush) {
        stall    = 0;
        load_use = 0;
        nx.id_ex.valid = 0;
    } else if (stall) {
        nx.id_ex.valid = 0;
        nx.if_id = p->if_id;   /* hold */
    }

    /* ============================ ID ============================ */
    if (p->if_id.valid) {
        const IFID *f = &p->if_id;
        IDEX *o = &nx.id_ex;

        memset(o, 0, sizeof(*o));
        o->valid             = 1;
        o->pc                = f->pc;
        o->fault             = f->fault;
        o->predicted_taken   = f->predicted_taken;
        o->predicted_target  = f->predicted_target;

        if (f->fault == PF_NONE) {
            DecodedInstr d;
            const InstructionHandler *h = NULL;

            if (decode_instruction(f->raw, &d, &h) != DECODE_OK) {
                o->fault = PF_ILLEGAL;
            } else {
                ControlSignals c = control_decode(&d, h);
                HazardVerdict hz = hazard_check(p, &d, &c);
                if (hz.stall) {
                    stall    = 1;
                    load_use = hz.load_use;
                } else {
                    o->instr   = d;
                    o->handler = h;
                    o->ctrl    = c;
                    o->rs1_val = cpu_read_reg(cpu, (int)d.rs1);
                    o->rs2_val = cpu_read_reg(cpu, (int)d.rs2);
                    if (c.is_system) {            /* ecall reads a7, a0 in ID */
                        o->rs1_val = cpu_read_reg(cpu, 17);
                        o->rs2_val = cpu_read_reg(cpu, 10);
                    }
                }
            }
        }

        /* Re-apply flush override after ID (flush set during EX above).
         * ID may have raised a stall for the wrong-path instruction it just
         * decoded; a flush must cancel that too, or IF is skipped and the
         * redirect is lost. */
        if (flush) {
            nx.id_ex.valid = 0;
            stall    = 0;
            load_use = 0;
        } else if (stall) {
            nx.id_ex.valid = 0;
            nx.if_id = p->if_id;
        }
    }

    /* ============================ IF ============================ */
    if (!stall) {
        uint32_t fetch_pc = cpu->pc;   /* a redirect takes effect NEXT cycle (2-cycle penalty) */
        uint32_t raw = 0;

        nx.if_id.valid           = 1;
        nx.if_id.pc              = fetch_pc;
        nx.if_id.predicted_taken = 0;
        nx.if_id.predicted_target= fetch_pc + 4;

        if (memory_read_word(mem, fetch_pc, &raw) == MEM_OK) {
            nx.if_id.raw   = raw;
            nx.if_id.fault = PF_NONE;

            /* Lab 4: check if this raw word is a branch (opcode 0x63)
             * and ask the predictor whether to fetch taken or not-taken. */
            if (p->bp_mode != BP_NONE && (raw & 0x7F) == 0x63) {
                /* Decode the B-type immediate from the raw word */
                int32_t imm =
                    (((int32_t)(raw & 0x80000000u)) >> 19) |  /* imm[12] */
                    ((int32_t)((raw & 0x00000080u) << 4))   |  /* imm[11] */
                    ((int32_t)((raw & 0x7E000000u) >> 20))  |  /* imm[10:5] */
                    ((int32_t)((raw & 0x00000F00u) >> 7));      /* imm[4:1] */

                int pred = bp_predict(p, fetch_pc, imm);
                if (pred) {
                    uint32_t target = (uint32_t)((int32_t)fetch_pc + imm);
                    nx.if_id.predicted_taken  = 1;
                    nx.if_id.predicted_target = target;
                    /* Fetch from the predicted target next cycle */
                    cpu->pc = target;
                } else {
                    cpu->pc = fetch_pc + 4;
                }
            } else if (p->bp_mode != BP_NONE && (raw & 0x7F) == 0x6F) {
                /* jal: target is in the immediate, redirect right here in IF */
                uint32_t j = ((raw >> 31) & 1u) << 20 | ((raw >> 12) & 0xFFu) << 12 |
                             ((raw >> 20) & 1u) << 11 | ((raw >> 21) & 0x3FFu) << 1;
                int32_t  imm = (int32_t)(j << 11) >> 11;     /* sign-extend 21 bits */
                uint32_t target = (uint32_t)((int32_t)fetch_pc + imm);
                nx.if_id.predicted_taken  = 1;
                nx.if_id.predicted_target = target;
                cpu->pc = target;
            } else {
                cpu->pc = fetch_pc + 4;
            }
        } else {
            nx.if_id.raw   = 0;
            nx.if_id.fault = PF_FETCH;
            cpu->pc = fetch_pc + 4;
        }

        if (flush) {            /* what IF just fetched is on the wrong path */
            nx.if_id.valid = 0;
            cpu->pc = redirect_pc;
        }
    }

    /* ======================= counters + commit ================== */
    nx.cycles++;
    if (stall)    nx.stalls++;
    if (load_use) nx.load_use_stalls++;
    if (flush)    nx.flushes++;

    {
        static int trace = -1;
        if (trace < 0) trace = getenv("RVSIM_TRACE") != NULL;
        if (trace) {
            char ifs[8], ids[8], exs[8], mems[8], wbs[8];
            #define PCSTR(buf, v, pcv) snprintf(buf, sizeof buf, (v) ? "%04x" : "----", (unsigned)(pcv))
            PCSTR(ifs,  nx.if_id.valid,  nx.if_id.pc);
            PCSTR(ids,  p->if_id.valid,  p->if_id.pc);
            PCSTR(exs,  p->id_ex.valid,  p->id_ex.pc);
            PCSTR(mems, p->ex_mem.valid, p->ex_mem.pc);
            PCSTR(wbs,  p->mem_wb.valid, p->mem_wb.pc);
            #undef PCSTR
            fprintf(stderr, "c%-3llu IF=%s ID=%s EX=%s MEM=%s WB=%s %s%s%s\n",
                    (unsigned long long)nx.cycles, ifs, ids, exs, mems, wbs,
                    stall ? "STALL " : "", load_use ? "(load-use) " : "",
                    flush ? "FLUSH" : "");
        }
    }

    *p = nx;
    return cpu->status;
}

void pipeline_run_batch(Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles) {
    while (cpu->status == CPU_RUNNING && p->cycles < max_cycles)
        pipeline_cycle(p, cpu, mem);

    cpu->cycle_count = p->cycles;

    printf("\n");
    print_registers(cpu);
    print_status(cpu);
    if (cpu->status == CPU_RUNNING)
        printf("  -> stopped: reached max cycle limit (%llu)\n",
               (unsigned long long)max_cycles);

    /* ----------------------------------------------------------------
     * Output format matching the expected golden files.
     *
     * Lab 2 (--pipeline, no hazard handling):
     *   pipeline        = 5-stage, no hazard handling
     *   retired         = N
     *   CPI             = X.XXX
     *   redirects       = N
     *   squashed        = N
     *
     * Lab 3 (--pipeline / --pipeline-forward with hazard unit):
     *   pipeline        = 5-stage, hazard unit 'stall'|'forward'
     *   retired         = N
     *   CPI             = X.XXX
     *   stalls          = N
     *   forwards        = N
     *   redirects       = N
     *   squashed        = N
     *
     * Lab 4 (--pipeline-forward --predict <name>):
     *   adds:
     *   branches        = N
     *   mispredicts     = N
     *   accuracy        = XX.X%
     *   jal             = N
     *   jalr            = N
     * ---------------------------------------------------------------- */

    /* Determine mode string */
    const char *hazard_label;
    if (p->no_hazard)
        hazard_label = "none";
    else if (!p->forwarding)
        hazard_label = "stall";
    else
        hazard_label = "forward";

    const char *pred_label = bp_name(p->bp_mode);  /* NULL for BP_NONE */

    /* Build the pipeline description string */
    if (hazard_label == NULL) {
        printf("pipeline        = 5-stage, no hazard handling\n");
    } else if (pred_label == NULL) {
        printf("pipeline        = 5-stage, hazard unit '%s'\n", hazard_label);
    } else {
        printf("pipeline        = 5-stage, hazard unit '%s', predictor '%s'\n",
               hazard_label, pred_label);
    }

    printf("retired         = %llu\n", (unsigned long long)p->retired);
    printf("CPI             = %.3f\n",
           p->retired ? (double)p->cycles / (double)p->retired : 0.0);

    /* Stalls line: only when hazard unit is active */
    if (hazard_label != NULL)
        printf("stalls          = %llu\n", (unsigned long long)p->stalls);

    /* Forwards line: always present when hazard unit active */
    if (hazard_label != NULL)
        printf("forwards        = %llu\n", (unsigned long long)p->fwd_ex + p->fwd_mem);

    /* Lab 4 extra stats */
    if (pred_label != NULL) {
        double accuracy = p->branches > 0
            ? 100.0 * (double)(p->branches - p->mispredicts) / (double)p->branches
            : 0.0;
        printf("branches        = %llu\n", (unsigned long long)p->branches);
        printf("mispredicts     = %llu\n", (unsigned long long)p->mispredicts);
        printf("accuracy        = %.1f%%\n", accuracy);
        printf("jal             = %llu\n", (unsigned long long)p->jal_count);
        printf("jalr            = %llu\n", (unsigned long long)p->jalr_count);
    }

    printf("redirects       = %llu\n", (unsigned long long)p->redirects);
    printf("squashed        = %llu\n", (unsigned long long)p->squashed);
}