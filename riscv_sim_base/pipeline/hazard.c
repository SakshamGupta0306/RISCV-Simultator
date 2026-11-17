/*
 * Hazard detection unit (ID) and forwarding unit (EX).
 *
 *   Stall-only mode (--pipeline):
 *       stall whenever ID/EX, EX/MEM or MEM/WB will write a register the
 *       instruction in ID reads. Distance 1/2/3 costs 3/2/1 stalls.
 *
 *   Forwarding mode (--pipeline-forward):
 *       EX takes operands from EX/MEM (distance 1) or MEM/WB (distance 2).
 *       Distance 3 needs no path: WB writes the register file before ID
 *       reads it in the same cycle. The only remaining stall is load-use:
 *       a load in EX whose result the instruction in ID needs.
 */
#include "hazard.h"

/* valid latch + really writes a register + not x0 + same register number */
int produces(int valid, const ControlSignals *lc, uint32_t rd, uint32_t rs) {
    return valid && lc->reg_write && rd != 0 && rd == rs;
}

HazardVerdict hazard_check(const Pipeline *p, const DecodedInstr *d,
                           const ControlSignals *c) {
    HazardVerdict v = {0, 0};

    /* An unused source becomes x0, which produces() already rejects
     * (stops e.g. lui from "reading" junk in its rs1 field). */
    uint32_t s1 = c->uses_rs1 ? d->rs1 : 0;
    uint32_t s2 = c->uses_rs2 ? d->rs2 : 0;

    if (!p->forwarding) {               /* stall on any of 3 latches */
        if (produces(p->id_ex.valid,  &p->id_ex.ctrl,  p->id_ex.instr.rd,  s1) ||
            produces(p->id_ex.valid,  &p->id_ex.ctrl,  p->id_ex.instr.rd,  s2) ||
            produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, s1) ||
            produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, s2) ||
            produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.instr.rd, s1) ||
            produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.instr.rd, s2))
            v.stall = 1;
        return v;
    }

    /* load-use: only ID/EX can hold a load whose value will not exist in
     * time; one in EX/MEM is in WB before we reach EX, MEM/WB -> EX covers
     * it. Test mem_read, not the opcode. */
    if (p->id_ex.valid && p->id_ex.ctrl.mem_read && p->id_ex.instr.rd != 0 &&
        ((c->uses_rs1 && d->rs1 == p->id_ex.instr.rd) ||
         (c->uses_rs2 && d->rs2 == p->id_ex.instr.rd))) {
        v.stall    = 1;
        v.load_use = 1;
    }
    return v;
}

Operands forward_operands(const Pipeline *p, const IDEX *d) {
    Operands o = { d->rs1_val, d->rs2_val, 0, 0 };
    if (!p->forwarding || d->fault != PF_NONE) return o;

    /* operand a <- rs1 */
    if (d->ctrl.uses_rs1 && d->instr.rs1 != 0) {
        if (produces(p->ex_mem.valid, &p->ex_mem.ctrl,
                     p->ex_mem.instr.rd, d->instr.rs1)) {
            o.a = p->ex_mem.alu_result; o.fwd_ex++;     /* EX/MEM wins: younger */
        } else if (produces(p->mem_wb.valid, &p->mem_wb.ctrl,
                            p->mem_wb.instr.rd, d->instr.rs1)) {
            o.a = p->mem_wb.wb_val;     o.fwd_mem++;
        }
    }

    /* operand b <- rs2 */
    if (d->ctrl.uses_rs2 && d->instr.rs2 != 0) {
        if (produces(p->ex_mem.valid, &p->ex_mem.ctrl,
                     p->ex_mem.instr.rd, d->instr.rs2)) {
            o.b = p->ex_mem.alu_result; o.fwd_ex++;
        } else if (produces(p->mem_wb.valid, &p->mem_wb.ctrl,
                            p->mem_wb.instr.rd, d->instr.rs2)) {
            o.b = p->mem_wb.wb_val;     o.fwd_mem++;
        }
    }
    return o;
}
