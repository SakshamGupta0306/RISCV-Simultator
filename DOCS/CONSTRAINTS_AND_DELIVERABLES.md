# RV32 Pipeline Labs 2–5: Constraints & Deliverables

Project root for all commands: `riscv_sim_base/`
Oracle: the single-cycle engine, run as `./rvsim <prog.s> --batch`.
Sheet mapping: **W7 sheet = Lab 2**, then Lab 3, Lab 4, Lab 5.

Status: Labs 2–4 implemented (to be verified, see `TESTING.md`). Lab 5 not started.

---

## 0. Global constraints (apply to every lab)

| # | Constraint |
|---|-----------|
| G1 | `core/`, `isa/` (except Lab 5), `assembler/`, `ui/` stay untouched. Reading is fine. |
| G2 | New code lives in `pipeline/` (Labs 2–4) or `isa/rv32f.c` (Lab 5). |
| G3 | The single-cycle engine (`--batch`) is the oracle. Final x-registers must match it in **every** mode. Lab 5 also compares f-registers and `fcsr`. |
| G4 | Build must be warning-free under `-Wall -Wextra`. |
| G5 | `retired` must equal the single-cycle cycle count exactly, in every mode and predictor. |
| G6 | Older policies stay selectable (stall-only, forwarding, all four predictors). The comparison is the point of the labs. |
| G7 | Cycle identity must hold: `cycles = retired + stalls + flushes + 4` (Labs 2–3), or `cycles = retired + 4 + 2×redirects + stalls` (Lab 4). |
| G8 | Do not hide differences: any deviation from the reference tables needs a stated design choice in the report. Wrong `retired` or wrong registers is a bug, not a choice. |

---

## Lab 2 (W7 sheet): Five-stage pipeline

### Constraints
- Reuse `decode_instruction()`, `cpu_read_reg/write_reg`, `memory_*`. Do not re-implement field extraction.
- Only edits outside `pipeline/`: ~3 lines in `main.c`, 2 lines in `Makefile`.
- Snapshot/commit idiom: `Pipeline nx = *p;` stages read `p`, write `nx`, then `*p = nx;`.
- Faults are carried in latches and only become a `CPUStatus` in WB (precise exceptions).
- Stall must do 3 things: bubble into EX, freeze IF/ID, stop IF fetching and PC advance.
- Branches/jumps resolve in EX; 2 younger instructions squashed.
- Supported: all RV32I used by tests (R, I, shifts, lui, auipc, loads, stores, branches, jal, jalr, ecall).

### Deliverables
| ID | Deliverable | File |
|----|-------------|------|
| L2-1 | `ControlSignals` struct + `control_decode()` as one switch on `h->format` per the sheet table | `pipeline/control.{h,c}` |
| L2-2 | Latches `IFID/IDEX/EXMEM/MEMWB`, `Pipeline` with counters, `PF_NONE/ILLEGAL/FETCH/MEM` | `pipeline/pipeline_state.h` |
| L2-3 | Five stages WB/MEM/EX/ID/IF in `pipeline_cycle()`, `pipeline_init`, `pipeline_run_batch` | `pipeline/pipeline.c` |
| L2-4 | `--pipeline` option in `main.c`; pipeline files in Makefile | `main.c`, `Makefile` |
| L2-5 | `tests/sample3_hazards.s` copied in | `tests/` |
| L2-6 | Performance comparison (single-cycle vs pipeline: cycles, CPI, % change) on 2 programs, including the 5-independent-`addi` example (25 vs 9 cycles, CPI 5.00 vs 1.80, 64%) | report |

### Reference numbers (stall-only policy)
| Program | single | pipeline | retired | stalls | flushes | CPI |
|---|---|---|---|---|---|---|
| sample1_sum_loop | 53 | 108 | 53 | 40 | 11 | 2.038 |
| sample2_mem_and_branches | 13 | 26 | 13 | 8 | 1 | 2.000 |
| sample3_hazards | 18 | 44 | 18 | 19 | 3 | 2.444 |

---

## Lab 3: Data hazards (stall + forwarding + load-use)

### Constraints
- Keep Lab 2 stall policy under `--pipeline`; forwarding under new `--pipeline-forward`.
- Only allowed edits outside `pipeline/`: one `int forwarding;` field in `Pipeline` and one flag in `main.c`.
- A producer is never a hazard if: `rd == x0`, `reg_write == 0`, or latch invalid.
- Use `uses_rs1 ? rs1 : 0` so unused sources do not cause false stalls (e.g. `lui`).
- Forwarding priority: EX/MEM beats MEM/WB.
- Forwarded values must be used in **all four** places in EX: ALU operands, store data, branch compare, jalr target.
- Load-use: examined only in ID/EX, uses `ctrl.mem_read` (not opcode), exactly one stall. Must catch ALU operand, branch operand, store data (rs2), address base (rs1).
- No MEM→MEM path.

### Deliverables
| ID | Deliverable | File |
|----|-------------|------|
| L3-1 | `HazardVerdict hazard_check()` with `produces()` helper (non-static, declared in header) | `pipeline/hazard.{h,c}` |
| L3-2 | `Operands forward_operands()` | `pipeline/hazard.c` |
| L3-3 | Load-use detection setting `stall` and `load_use` | `pipeline/hazard.c` |
| L3-4 | Counters: `stalls, load_use_stalls, fwd_ex, fwd_mem, retired, flushes`; summary line printed from `pipeline_run_batch()` | `pipeline/pipeline_state.h`, `pipeline.c` |
| L3-5 | `--pipeline-forward` option | `main.c` |
| L3-6 | `tests/micro.s`, `tests/sample4_data_hazards.s` | `tests/` |
| L3-7 | Per-cycle debug trace (stderr) and cycle-by-cycle check on micro.s | `pipeline.c` |
| L3-8 | Report: tables for both policies, explanation of stalls and CPI, which design choices differ | report |

### Reference numbers
Stall-only: micro 15/6 stalls/CPI 3.000; sample1 108/40/11; sample2 26/8/1; sample4 69/35/1.
Forwarding: micro 9/0/0; sample1 68/0/11; sample2 18/0/1; sample4 36/2/1 (both stalls load-use).
Forward counts (ex/mem): micro 2/1; sample1 13/11; sample2 2/2; sample4 9/7.

---

## Lab 4: Branch prediction

### Constraints
- Predictor sees only branch PC and target. No register values, no outcome.
- `predict` is read-only on state; `update` (called in EX) is the only writer.
- No static variables inside predictor functions; table belongs to the pipeline.
- Table: 64 entries, index = `pc[7:2]`, no tags (aliasing is silent; discuss in report).
- 2-bit counters **saturate**, never wrap. Start value is a design choice (reference: 1 = weakly not-taken). State and justify it.
- `jal` redirected in IF (free); `jalr` never predicted (2 cycles each).
- All four predictors selectable: `not-taken`, `btfn`, `1bit`, `2bit`.
- Sheet says do not change `branch_predictor.{h,c}` / `pipeline.c` (they are "given").

### Deliverables
| ID | Deliverable | File |
|----|-------------|------|
| L4-1 | Always-not-taken and BTFN predictors | `pipeline/bp_static.c` |
| L4-2 | 1-bit predictor | `pipeline/bp_dynamic.c` |
| L4-3 | 2-bit saturating predictor | `pipeline/bp_dynamic.c` |
| L4-4 | Predictors registered in the table (`bp_find`) and selectable at run time | `branch_predictor.c`, `main.c` |
| L4-5 | Test programs `t1_counted_loop.s`, `t2_nested_loops.s`, `t3_alternating.s`, `t4_calls.s` | `tests/` |
| L4-6 | Report: branches, mispredicts, accuracy, redirects, cycles, CPI per program × predictor, plus one sentence per case on the gap to the floor; aliasing discussion; answers on (a) jal free, (b) btfn wins, (c) 2bit vs 1bit on t2/t3 | report |

### Reference numbers (total cycles)
| Program | single | not-taken | btfn | 1bit | 2bit |
|---|---|---|---|---|---|
| t1_counted_loop | 37 | 59 | 43 | 45 | 45 |
| t2_nested_loops | 52 | 78 | 66 | 76 | 70 |
| t3_alternating | 50 | 76 | 64 | 74 | 74 |
| t4_calls | 219 | 278 | 278 | 280 | 278 |

Mispredicts (t1/t2/t3/t4): not-taken 9/11/11/2, btfn 1/5/5/2, 1bit 2/10/10/3, 2bit 2/7/10/2. t4: 33 jal, 18 jalr.

---

## Lab 5: Floating point (RV32F)  — TO DO

### Constraints
- Write **only** `isa/rv32f.c` and add one line `register_rv32f();` to `main.c`. `core/`, `assembler/`, `ui/`, `pipeline/` untouched.
- **This lab assumes infrastructure that your zip does not yet contain** (see "Prerequisites" below). It is described as "given" in the sheet.
- Never cast pointers between `float*` and `uint32_t*`; use `memcpy` (helpers `f_of/bits_of`).
- Registers hold raw bits; use `cpu_write_freg` for f-registers (not `cpu_write_reg`).
- Resolve the rounding mode first; invalid mode (5, 6, or dyn with bad frm) raises ILLEGAL_INSTRUCTION and writes no register and no flag.
- Every NaN produced by arithmetic is canonical `0x7fc00000`.
- Flags are sticky (OR into fcsr, never cleared by hardware).
- Fused multiply-adds use `fmaf` semantics (single rounding) and register with `IF_RS3_FP`; funct3 and funct7 are don't-care (`-1`).
- Do NOT "fix" results to agree with RARS in its 4 known deviations (sNaN arithmetic, `fmadd(inf,0,qNaN)`, `fcvt.w.s` ties, `fcvt.wu.s` of -2.5).
- Build with `-frounding-math` and `-lm`.
- Warning-free.

### Prerequisites (check first; request from instructor if missing)
`core/cpu_state.*` (fregs, fcsr, `cpu_read_freg/write_freg/raise_fflags/frm`), `core/isa_registry.*` (`register_instruction_ex`, FMT_R2/R4/CSR, IF_* flags), updated decoder (rs3, CSR, fixed rs2), assembler FP support, pipeline `src3/fwd3` and unified register numbering, `isa/fp_helpers.c`, `isa/zicsr.c`, M-extension example, and tests `t1_fp_arith.s`, `t2_conversions.s`, `t3_special_values.s`, `t4_fp_program.s`. Also note the pipeline in your zip uses the Lab 3/4 era design (see `TESTING.md` §0), which needs `src3`-style changes the sheet says were "given".

### Deliverables
| ID | Deliverable |
|----|-------------|
| L5-T1 | Bit-only instructions: `flw`, `fsw`, `fsgnj.s`, `fsgnjn.s`, `fsgnjx.s`, `fmv.x.w`, `fmv.w.x`, `fclass.s` |
| L5-T2 | Arithmetic: `fadd.s fsub.s fmul.s fdiv.s fsqrt.s` + `fmadd.s fmsub.s fnmsub.s fnmadd.s` via `do_arith` |
| L5-T3 | `feq.s flt.s fle.s fmin.s fmax.s fcvt.w.s fcvt.wu.s fcvt.s.w fcvt.s.wu` with RISC-V NaN/zero/saturation rules |
| L5-T4 | Flag behaviour (NV, DZ, OF, UF, NX) verified against the special-values table |
| L5-D1 | `isa/rv32f.c` + one-line `main.c` change (state explicitly if anything else changed) |
| L5-D2 | `--batch` output of all four FP tests under forward, stall, and single-cycle, including `fcsr` line |
| L5-D3 | ≤2-page report answering the 8 questions (table reproduction; 3 enabling design decisions + `src3`; why fcsr needs no hazard logic; ten fcvt values; UF/NX difference; fneg vs fadd on sNaN; one RARS deviation with spec quote; why FP gap is bigger) |
| L5-D4 | Optional extension with expected-vs-actual infrastructure touches |

### Reference numbers
| Program | Instrs | forward: cycles/stalls/fwds/CPI | stall: cycles/stalls/CPI |
|---|---|---|---|
| t1_fp_arith | 36 | 41 / 1 / 11 / 1.139 | 56 / 16 / 1.556 |
| t2_conversions | 109 | 113 / 0 / 47 / 1.037 | 167 / 54 / 1.532 |
| t3_special_values | 72 | 76 / 0 / 7 / 1.056 | 85 / 9 / 1.181 |
| t4_fp_program | 133 | 159 / 14 / 61 / 1.195 | 261 / 116 / 1.962 |

`t4_fp_program` prints `12000014141000`; identity: 133 + 4 + 2×4 + 14 = 159.
Rounding table (rne/rtz/rdn/rup/rmm): 2.5 → 2/2/2/3/3; −2.5 → −2/−2/−3/−2/−3; 3.5 → 4/3/3/4/4.

Special values (result / flags): 0/0 → qNaN NV; 1/0 → +inf DZ; FLT_MAX+FLT_MAX → +inf OF,NX; FLT_MIN×FLT_MIN → +0 UF,NX; min_sub×1 → min_sub none; inf−inf → qNaN NV; sNaN+1 → qNaN NV; sqrt(−1) → qNaN NV; fmadd(inf,0,qNaN) → qNaN NV; fmin(qNaN,1) → 1 none; fmin(sNaN,1) → 1 NV; fmin(−0,+0) → −0; fmax(−0,+0) → +0; feq(qNaN,qNaN) → 0 none; flt(qNaN,1) → 0 NV; feq(sNaN,1) → 0 NV; fneg(sNaN) → 0xffa00000 none.

---

## Final submission checklist
- [ ] Labs 2–4 verified per `TESTING.md` (all PASS or documented deviations)
- [ ] Lab 5 `isa/rv32f.c` + `main.c` one-liner
- [ ] Outputs of all test programs in all modes
- [ ] Reports (Lab 2 comparison, Lab 3 tables, Lab 4 tables, Lab 5 two-pager)
- [ ] Statement of every file changed outside the allowed set
