#!/bin/bash
# Runs the course evaluator (evaluate.py) on every non-FP sample test.
# Usage (from the project root):
#   bash tests/eval_all.sh <path/to/student_package> <path/to/sample_tests>
# Each expected output becomes a test: a "# RUN:" line is prepended to the .s file.
E="${1:?path to student_package}"; S="${2:?path to sample_tests}"
ROOT="$(pwd)"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
pass=0; fail=0
run() { # prog gold opts name
  { echo "# RUN: $3"; cat "$1"; } > "$T/$4.s"
  r=$(python3 "$E/evaluate.py" "$ROOT" "$T/$4.s" "$2" 2>&1)
  if [[ "$r" == *"match= 100% ,counter match = 100%"* ]]; then pass=$((pass+1)); echo "100/100  $4  [$3]"
  else fail=$((fail+1)); echo "NOT 100  $4  [$3]  -> $(echo "$r" | head -2 | tr '\n' ' ')"; fi; }
for p in h1_broad h2_edge h3_kernel; do
  for m in none stall forward; do run "$S/lab3_hazards/$p.s" "$S/lab3_hazards/expected/$p.$m.out" "--hazard $m" "$p.$m"; done
  run "$S/lab3_hazards/$p.s" "$S/lab3_hazards/expected/$p.single-cycle.out" "--single-cycle" "$p.sc"; done
for p in p1_broad p2_edge p3_kernel; do
  run "$S/lab2_pipeline/$p.s" "$S/lab2_pipeline/expected/$p.pipeline.out" "--hazard none" "$p.none"
  run "$S/lab2_pipeline/$p.s" "$S/lab2_pipeline/expected/$p.single-cycle.out" "--single-cycle" "$p.sc"; done
for p in b1_patterns b2_adversarial b3_applied; do
  for b in not-taken btfn 1bit 2bit; do run "$S/lab4_branch/$p.s" "$S/lab4_branch/expected/$p.$b.out" "--hazard forward --predictor $b" "$p.$b"; done
  run "$S/lab4_branch/$p.s" "$S/lab4_branch/expected/$p.single-cycle.out" "--single-cycle" "$p.sc"; done
echo "TOTAL: $pass at 100/100, $fail not"