#!/bin/bash
# Usage (from the riscv_sim_base folder, after `make`):
#   bash run_sample_tests.sh /path/to/sample_tests
# Compares, for every program/mode: printed output, x registers, f registers + fcsr
# (if present) and the cycle count against the reference "expected" files.
# Does NOT touch your tests/ folder (works in a temp dir).

S="${1:-sample_tests}"
[ -d "$S" ] || { echo "sample_tests folder not found: $S"; exit 2; }
[ -x ./rvsim ] || { echo "./rvsim not found - run make first"; exit 2; }

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cp "$S"/lab*/*.s "$T"/

pass=0; fail=0; gap=0; failed=()

# keep: printed numbers, register rows (x.. / f..), fcsr line, cycle count
norm() { grep -E '^[0-9]+$|^[xf][0-9]+ ?/|^fcsr|^status' "$1" | sed -E 's/status = [A-Z_]+ +//'; }

# check <lab> <prog> <expected-file> <rvsim flags...>
check() {
  local lab=$1 prog=$2 gold=$3; shift 3
  if [ ! -f "$gold" ]; then return; fi
  ./rvsim "$T/$prog.s" "$@" > "$T/got.txt" 2>/dev/null
  if diff <(norm "$gold") <(norm "$T/got.txt") >/dev/null; then
    printf "PASS  %-5s %-16s %s\n" "$lab" "$prog" "$*"; pass=$((pass+1))
  else
    local gc mc
    gc=$(grep -o 'cycles = [0-9]*' "$gold" | head -1)
    mc=$(grep -o 'cycles = [0-9]*' "$T/got.txt" | head -1)
    printf "FAIL  %-5s %-16s %s   (expected %s, got %s)\n" "$lab" "$prog" "$*" "$gc" "${mc:-none}"
    fail=$((fail+1)); failed+=("$lab $prog $*")
  fi
}

echo "=== Lab 1-4 : single-cycle oracle (--single-cycle) ==="
for p in p1_broad p2_edge p3_kernel; do check L2 $p "$S/lab2_pipeline/expected/$p.single-cycle.out" --single-cycle; done
for p in h1_broad h2_edge h3_kernel; do check L3 $p "$S/lab3_hazards/expected/$p.single-cycle.out" --single-cycle; done
for p in b1_patterns b2_adversarial b3_applied; do check L4 $p "$S/lab4_branch/expected/$p.single-cycle.out" --single-cycle; done

echo; echo "=== Lab 2 : pipeline with NO hazard handling (--hazard none) ==="
for p in p1_broad p2_edge p3_kernel; do check L2 $p "$S/lab2_pipeline/expected/$p.pipeline.out" --hazard none; done

echo; echo "=== Lab 3 : stall and forward ==="
for p in h1_broad h2_edge h3_kernel; do
  check L3 $p "$S/lab3_hazards/expected/$p.none.out"    --hazard none
  check L3 $p "$S/lab3_hazards/expected/$p.stall.out"   --hazard stall
  check L3 $p "$S/lab3_hazards/expected/$p.forward.out" --hazard forward
done

echo; echo "=== Lab 4 : four predictors (forwarding on) ==="
for p in b1_patterns b2_adversarial b3_applied; do
  for b in not-taken btfn 1bit 2bit; do
    check L4 $p "$S/lab4_branch/expected/$p.$b.out" --predictor $b
  done
done

echo; echo "=== Lab 5 : floating point (expected to fail until Lab 5 is done) ==="
for p in f1_broad f2_special f3_kernel; do
  check L5 $p "$S/lab5_fp/expected/$p.single-cycle.out" --single-cycle
  check L5 $p "$S/lab5_fp/expected/$p.forward.out"      --hazard forward
  check L5 $p "$S/lab5_fp/expected/$p.stall.out"        --hazard stall
done

echo
echo "TOTAL: $pass passed, $fail failed"
echo "Failures:"; printf '  %s\n' "${failed[@]}"