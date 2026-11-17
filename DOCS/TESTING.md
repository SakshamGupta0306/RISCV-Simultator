# Testing Guide: Labs 2–5

**Run everything from the project root** (the folder containing `Makefile`, `main.c`, `tests/`):

```bash
cd riscv_sim_base
```

Commands use bash (Linux, macOS, WSL, Git Bash). Notation: ✅ = what you should see, ❌ = what means a bug.

---

## 0. Known issues found in the zip you uploaded (fix these first)

I built the zip on Linux and found these before running anything. They are why a clean `make` fails.

| # | Problem | Evidence | Fix |
|---|---------|----------|-----|
| 0.1 | **Filename case mismatch.** `Hazard.c` has `#include "hazard.h"` but the file is `Hazard.h`. Works on Windows, fails on Linux/macOS. | `fatal error: hazard.h: No such file` | Rename to lowercase `hazard.h` / `hazard.c` (and update the Makefile). Same for `Micro.s` → `micro.s`, `Sample4_data_hazards.s` → `sample4_data_hazards.s`. |
| 0.2 | **Makefile links the wrong predictor file.** It lists `pipeline/branch_predictor.c`, but `pipeline.c` calls `bp_predict` and `bp_name`, which are defined in `pipeline/bp.c`. | `undefined reference to bp_predict / bp_name` | Either add `pipeline/bp.c` to `SRCS`, or finish the Lab 4 plug-in design (see 0.3). With `bp.c` swapped in, everything builds warning-free. |
| 0.3 | **Lab 4 is implemented in a different style from the sheet.** Sheet: plug-in `BranchPredictor` with `bp_static.c` + `bp_dynamic.c`. Zip: `bp.c` with a switch on `bp_mode`; `bp_static.c` exists but is not built; **no `bp_dynamic.c`**. | `ls pipeline/` | Decide: keep the `bp.c` approach (works, numbers below match) or migrate to the sheet's plug-in design. Note for the report. |
| 0.4 | **CLI differs from the sheet.** Zip has `--batch` (single-cycle oracle), `--pipeline`, `--pipeline-forward`, `--predict <name>`. Sheet 4 uses `--single-cycle`, `--predictor`, `--hazard stall/forward`, `--trace`; `make test` with goldens does not exist. | `grep '"--' main.c` | The commands in this file use **your** flags. Migrating is optional but needed if graders run sheet-4 commands. |
| 0.5 | `tests/t4_calls.s` (Lab 4) is **missing**. | `ls tests/` | Obtain it from the instructor. Rows involving it cannot be verified yet. |
| 0.6 | Zip contains build junk: `rvsim`, `rvsim.exe`, `test_static.exe`, `ref.txt`, `pipe.txt`. | | Delete before submitting. |

> All results below were measured after applying the minimum workaround (`ln -s Hazard.h pipeline/hazard.h` and swapping `branch_predictor.c` → `bp.c` in the Makefile) in a scratch copy. Your own files were not modified.

---

## 1. Build check

```bash
make clean && make 2>&1 | tee build.log
grep -ciE "warning|error" build.log
ls -l rvsim
```
✅ The `grep` prints `0` and `rvsim` exists. ❌ Any warning or error (G4).

Quick sanity:
```bash
./rvsim tests/sample1_sum_loop.s --batch | grep -E "^55|status"
```
✅ prints `55` and `status = ECALL_EXIT   cycles = 53`.

---

## 2. Universal check: registers match the oracle (all labs)

Save as `check_regs.sh`, run `bash check_regs.sh`.

```bash
#!/bin/bash
fail=0
for f in tests/*.s; do
  ./rvsim "$f" --batch 2>/dev/null | grep '^x' > /tmp/ref.txt
  for m in "--pipeline" "--pipeline-forward" \
           "--pipeline-forward --predict not-taken" \
           "--pipeline-forward --predict btfn" \
           "--pipeline-forward --predict 1bit" \
           "--pipeline-forward --predict 2bit"; do
    if ./rvsim "$f" $m 2>/dev/null | grep '^x' | diff -q /tmp/ref.txt - >/dev/null; then
      echo "PASS  $f  [$m]"
    else
      echo "FAIL  $f  [$m]"; fail=1
    fi
  done
done
[ $fail -eq 0 ] && echo "ALL REGISTER CHECKS PASSED" || echo "SOME FAILED"
```
✅ Every line `PASS` and the last line `ALL REGISTER CHECKS PASSED`. ❌ Any `FAIL` is a real bug (a leaking squash or a missed forward). My measured result on your code: **all 48 combinations PASS.**

---

## 3. Lab 2 tests (stall-only pipeline)

```bash
for f in sample1_sum_loop sample2_mem_and_branches sample3_hazards; do
  echo "== $f"; ./rvsim tests/$f.s --pipeline | grep -E "cycles|retired|CPI|redirects"
done
```

| Program | Expected cycles | Expected retired | Expected stalls / flushes | **Measured on your code** |
|---|---|---|---|---|
| sample1_sum_loop | 108 | 53 | 40 / 11 | **119**, retired 53 ⚠ |
| sample2_mem_and_branches | 26 | 13 | 8 / 1 | **27**, retired 13 ⚠ |
| sample3_hazards | 44 | 18 | 19 / 3 | **40**, retired 15 ⚠ (single-cycle says 15, see note) |

Notes:
- ✅ **retired** must equal the single-cycle `cycles` from `--batch`. sample3 single-cycle gives **15** on your build (the sheet table says 18, so the provided sample3 file may differ from the sheet's version; confirm with instructor). Pipeline retired = 15 matches your own oracle.
- ⚠ Cycle counts for sample1/2 are higher than the Lab 2 reference. Your `--pipeline` output prints `redirects` and `squashed` but **no `stalls`/`flushes` lines**, so the identity `cycles = retired + stalls + flushes + 4` cannot be checked directly. Action: print `stalls` and `flushes` in stall-only mode too, and check why 119 ≠ 108 (likely the jump/branch policy changed when Lab 4 code was added; the Lab 2 baseline expects every taken branch/jump to cost 2 and a 3-latch stall).
- Pipeline diagram check (5 independent `addi`): create `/tmp/five.s` with `addi x1,x0,10 … addi x5,x0,50` plus `addi a7,x0,10` / `ecall` (see sheet appendix). Expect 9 cycles for 5 instructions' worth of pipeline fill.

Test the 5-instruction CPI example:
```bash
cat > /tmp/five.s <<'EOF'
.text
.globl main
main:
 addi x1,x0,10
 addi x2,x0,20
 addi x3,x0,30
 addi x4,x0,40
 addi x5,x0,50
 addi a7,x0,10
 ecall
EOF
./rvsim /tmp/five.s --batch | grep cycles
./rvsim /tmp/five.s --pipeline | grep -E "cycles|CPI"
```
✅ single-cycle = 7 cycles (5 + addi + ecall); pipeline = 11 (7 + 4 fill). Ratio: 7 instr + 4 fill.

---

## 4. Lab 3 tests (hazards)

### 4a. Worked micro example
```bash
./rvsim tests/Micro.s --pipeline         | grep -E "cycles|retired|CPI"
./rvsim tests/Micro.s --pipeline-forward | grep -E "cycles|retired|CPI|stalls|forwards"
```
✅ stall-only: `cycles = 15`, `retired = 5`, `CPI = 3.000`. Forward: `cycles = 9`, `CPI = 1.800`, `stalls = 0`. **Measured: both match.** ✔

### 4b. All programs, forwarding mode (no predictor flag)
```bash
for f in Micro sample1_sum_loop sample2_mem_and_branches Sample4_data_hazards; do
  echo "== $f"; ./rvsim tests/$f.s --pipeline-forward | grep -E "cycles|retired|stalls|forwards"
done
```

| Program | Expected cycles / stalls | Measured |
|---|---|---|
| Micro | 9 / 0 | 9 / 0 ✔ |
| sample1_sum_loop | 68 / 0 (flushes 11) | **79** / 0 ⚠ |
| sample2_mem_and_branches | 18 / 0 | **19** / 0 ⚠ |
| sample4_data_hazards | 36 / 2 | **37** / 2 ⚠ |

⚠ Note: stalls match, cycles are 1–11 higher on programs containing branches/jumps. Cause hypothesis: without `--predict`, `BP_NONE` mode treats `jal`/`j` as a 2-cycle redirect like Lab 3 expects, but loops show 11 extra; investigate in `pipeline.c` IF/EX redirect logic. With a predictor (Lab 4 mode) sample1 = 59.

### 4c. Load-use cases (must be caught). Create and run:
```bash
cat > /tmp/lu.s <<'EOF'
.data
w: .word 7, 0
.text
.globl main
main:
 la gp, w
 lw t0, 0(gp)
 addi t1, t0, 1      # ALU operand
 lw t0, 0(gp)
 beq t0, zero, skip  # branch operand
 addi t2, zero, 1
skip:
 lw t0, 0(gp)
 sw t0, 4(gp)        # store data
 lw t0, 0(gp)
 lw t3, 0(t0)        # address base (t0=7 -> faults? use gp-based instead if so)
 addi a7, zero, 10
 ecall
EOF
./rvsim /tmp/lu.s --pipeline-forward | grep -E "stalls|status"
```
✅ `stalls` ≥ 3 for the first three patterns (one each). If the last `lw t3,0(t0)` faults because t0=7 is not a valid address, replace `7` in `.word` with the address of `w` (`.word` can't hold labels, so use `mv t0, gp` first). Registers must also match the oracle (use §2 with this file in `tests/`).

### 4d. Priority & store/branch/jalr forwarding
Add to `tests/` and run through §2:
```asm
.text
.globl main
main:
 addi t0, zero, 1
 addi t0, t0, 1
 addi t1, t0, 0     # must be 2, not 1
 addi a7, zero, 10
 ecall
```
✅ `x6/t1 = 0x00000002` in both pipeline modes.

### 4e. Counters identity
```bash
./rvsim tests/Sample4_data_hazards.s --pipeline-forward | grep -E "cycles|retired|stalls|redirects"
```
Compute `retired + 4 + 2×redirects + stalls` (Lab 4 form) and confirm it equals `cycles`. ✅ must be exactly equal.

---

## 5. Lab 4 tests (branch predictors)

```bash
for p in not-taken btfn 1bit 2bit; do
  echo "### $p"
  for f in t1_counted_loop t2_nested_loops t3_alternating; do
    printf "%-20s" $f
    ./rvsim tests/$f.s --predict $p | grep -E "^status|mispredicts" | tr -s ' ' | tr '\n' ' '
    echo
  done
done
```
Expected vs **measured** (cycles / mispredicts):

| Program | not-taken | btfn | 1bit | 2bit |
|---|---|---|---|---|
| t1_counted_loop | 59 / 9 ✔ | 43 / 1 ✔ | 45 / 2 ✔ | 45 / 2 ✔ |
| t2_nested_loops | 78 / 11 ✔ | 66 / 5 ✔ | 76 / 10 ✔ | 70 / 7 ✔ |
| t3_alternating | 76 / 11 ✔ | 64 / 5 ✔ | 74 / 10 ✔ | 74 / 10 ✔ |
| t4_calls | 278 / 2 | 278 / 2 | 280 / 3 | 278 / 2 | **cannot test: file missing** |

✅ All 12 measured cells match the Lab 4 sheet. 

Statistics block check (`./rvsim tests/t1_counted_loop.s --pipeline-forward --predict 2bit`):
✅ shows `retired = 37`, `branches = 10`, `mispredicts = 2`, `accuracy = 80.0%`, `redirects = 2`, `squashed = 4`, `cycles = 45`. **Measured: matches.**

Identity (Lab 4): `cycles = retired + 4 + 2×redirects + stalls` → 37 + 4 + 4 + 0 = 45 ✔. Repeat for every program/predictor; also `redirects = mispredicts + jalr`.

Other checks:
- **Saturation (no wrap) test:** a loop with ≥ 8 iterations should not mispredict every 4th iteration. t1 2bit = 2 misses proves this.
- **Aliasing:** two branches whose PCs differ by 256 bytes share an entry. Write a test with a branch at 0x10 and another at 0x110 (pad with `nop`s) and describe the effect in the report.
- **jal free / jalr costs 2:** once `t4_calls.s` is available, expect `jal = 33`, `jalr = 18`, `redirects = 20` for 2bit.
- **Bad predictor name:** `./rvsim tests/t1_counted_loop.s --predict foo` ✅ prints "unknown predictor" and exits non-zero.
- **Non-mutating predict:** confirm `predict` never writes state (code review of `bp.c`/`bp_static.c`).

---

## 6. Lab 5 tests (run after implementation; not runnable yet)

First confirm prerequisites exist (Constraints file, Lab 5 "Prerequisites"):
```bash
ls core/cpu_state.h isa/fp_helpers.c isa/zicsr.c tests/t1_fp_arith.s tests/t2_conversions.s tests/t3_special_values.s tests/t4_fp_program.s
grep -n "fregs\|fcsr" core/cpu_state.h
```
❌ In the uploaded zip **none of these exist** → we need them from the instructor before Lab 5 can start.

Once present:
```bash
grep -n "register_rv32f" main.c        # exactly one call added
make clean && make 2>&1 | grep -ci "warning"   # expect 0
```

| Step | Command | ✅ Expected |
|---|---|---|
| Baseline | `./rvsim tests/t4_fp_program.s --batch` | 133 cycles |
| Task 1 | `./rvsim tests/t1_fp_arith.s --batch` (load/store/move parts) and `t3_special_values.s` fclass + sign-injection registers | match the oracle's registers |
| Task 2 | `./rvsim tests/t1_fp_arith.s <pipeline mode>` | `cycles = 41`, `retired = 36`, `stalls = 1`, `forwards = 11`, `fcsr = 0x00000001 (frm=0 flags: NX)` |
| Task 3 | `./rvsim tests/t2_conversions.s <pipeline mode>` | `cycles = 113`, `retired = 109`, `stalls = 0` |
| Task 4 | `./rvsim tests/t3_special_values.s <mode>` | `cycles = 76`, `stalls = 0`, flags per special-value table |
| Whole | `./rvsim tests/t4_fp_program.s <mode>` | prints `12000014141000`, `cycles = 159`, `stalls = 14`, `forwards = 61`; stall mode: `261 / 116` |

Oracle comparison including f-registers and fcsr:
```bash
for f in tests/t*_fp*.s tests/t2_conv*.s tests/t3_special*.s; do
  ./rvsim $f --batch > ref.txt
  ./rvsim $f --pipeline-forward > got.txt
  diff <(grep -E '^(x|f|fcsr)' ref.txt) <(grep -E '^(x|f|fcsr)' got.txt) && echo "$f MATCH" || echo "$f MISMATCH"
done
```
(adjust flags to your CLI; sheet 5 uses `--single-cycle`, `--hazard forward|stall`).

Spot checks to run by hand in tiny `.s` files:
1. Rounding table: 2.5, −2.5, 3.5 with `fcvt.w.s` in rne, rtz, rdn, rup, rmm → 2/2/2/3/3, −2/−2/−3/−2/−3, 4/3/3/4/4.
2. Illegal rounding mode (static 5/6, or dyn with frm=5–7) → `ILLEGAL_INSTRUCTION`, `rd` and `fcsr` unchanged.
3. `1 + 2^-24` under rne = 1.0, under rmm = 1.0000001.
4. Every row of the special-values table, flags isolated with `fsflags zero, zero` before and `frflags` after.
5. `fneg.s(sNaN)` = `0xffa00000`, no flags; `fadd.s(sNaN, 1.0)` = `0x7fc00000` with NV.
6. Flag stickiness: two operations, second raises nothing, flag from the first still set.
7. Missing `IF_RS3_FP` negative test: a `fadd.s fs3,...` immediately followed by `fmadd.s fs0,fs1,fs2,fs3` should give the correct value and a forward/stall.
8. `-Wall -Wextra` and `-O2` rebuild gives identical outputs (strict-aliasing check).

---

## 7. Summary scoreboard (fill in as you go)

| Area | Status on uploaded zip |
|---|---|
| Build | ❌ fails as shipped (0.1, 0.2); ✔ after workaround |
| Registers match oracle, all modes | ✔ 48/48 |
| Lab 2 numbers | ⚠ 119/27/40 vs 108/26/44; stall/flush counters not printed in `--pipeline` |
| Lab 3 micro | ✔ 15 and 9 |
| Lab 3 other programs | ⚠ cycles 1–11 higher than reference (stalls correct) |
| Lab 4 t1–t3, all predictors | ✔ 12/12 |
| Lab 4 t4_calls | ❓ file missing |
| Lab 5 | ❌ not started; prerequisites absent from zip |

## 8. Next chat: suggested order
1. Fix 0.1, 0.2, 0.5, 0.6 (quick).
2. Investigate the Lab 2/3 cycle discrepancies (§3, §4b); add stalls/flushes output to `--pipeline`.
3. Decide on 0.3/0.4 (keep `bp.c` or migrate to plug-in + sheet CLI).
4. Obtain the Lab 5 infrastructure and implement `isa/rv32f.c` task by task, running §6 after each.
