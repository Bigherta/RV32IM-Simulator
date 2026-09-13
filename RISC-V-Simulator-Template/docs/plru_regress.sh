#!/bin/bash
# PLRU regression: x10 hard gate vs docs/benchmarks.md + clock drift report.
# Usage: bash docs/plru_regress.sh <binary> [pattern]
set -u
cd "$(dirname "$0")/.."
# Golden data = the repo-root docs/benchmarks.md markdown table (column "result"
# = x10, column "cycles" = reference clock). GOLDEN_MD may be overridden by env.
GOLDEN_MD="${GOLDEN_MD:-../docs/benchmarks.md}"
BIN="${1:?usage: plru_regress.sh <binary> [pattern]}"
PATTERN="${2:-*}"
pass=0; count=0

# Golden data lives in the single authoritative markdown table docs/benchmarks.md.
# Which column holds what is taken FROM THE HEADER ROW, so reordering or
# inserting columns in the doc cannot silently shift the lookup. Prints
# "<x10> <cycles>" for a case, or nothing when the case is not listed.
golden_for_case() {
  [ -f "$GOLDEN_MD" ] || return 0
  awk -F'|' -v want="$1" '
    !(caseCol && resCol && clkCol) {
      for (i=1; i<=NF; i++) {
        h=$i; gsub(/^[ \t]+|[ \t]+$/,"",h)
        if      (h=="case")   caseCol=i
        else if (h=="result") resCol=i
        else if (h=="cycles") clkCol=i
      }
      if (caseCol && resCol && clkCol) next
    }
    (resCol && clkCol) {
      n=$caseCol; gsub(/^[ \t]+|[ \t]+$/,"",n)
      if (n==want) {
        r=$resCol; k=$clkCol
        gsub(/^[ \t]+|[ \t]+$/,"",r); gsub(/^[ \t]+|[ \t]+$/,"",k)
        if (r ~ /^-?[0-9]+$/ && k ~ /^[0-9]+$/) { print r " " k; exit }
      }
    }' "$GOLDEN_MD"
}
printf "%-16s %-6s %-10s %-12s %-10s %s\n" "case" "exit" "x10" "golden_x10" "clock" "clock_vs_golden"
for f in data/testcases/${PATTERN}.data; do
  [ -f "$f" ] || continue
  name=$(basename "$f" .data)
  [ "$name" = "pi" ] && continue
  out=$(VERBOSE=clock "$BIN" < "$f" 2>/tmp/plru_err_$$)
  rc=$?
  clock=$(grep -E "^clock:" /tmp/plru_err_$$ | awk '{print $2}')
  golden=""; gclock=""
  read -r golden gclock <<< "$(golden_for_case "$name")" || true
  status="OK"
  if [ "$rc" -ne 0 ]; then status="CRASH($rc)";
  elif [ "$out" != "$golden" ]; then status="FAIL"; fi
  drift=""
  if [ -n "$clock" ] && [ -n "$gclock" ]; then
    drift=$((clock - gclock))
  fi
  printf "%-16s %-6s %-10s %-12s %-10s %s %s\n" "$name" "$rc" "$out" "$golden" "$clock" "$drift" "$status"
  [ "$status" = "OK" ] && pass=$((pass+1))
  count=$((count+1))
done
rm -f /tmp/plru_err_$$
echo "RESULT: $pass/$count passed (x10 gate)"
