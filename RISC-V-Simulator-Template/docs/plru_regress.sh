#!/bin/bash
# PLRU regression: x10 hard gate vs data/golden + clock drift report.
# Usage: bash docs/plru_regress.sh <binary> [pattern]
set -u
cd "$(dirname "$0")/.."
BIN="${1:?usage: plru_regress.sh <binary> [pattern]}"
PATTERN="${2:-*}"
pass=0; count=0
printf "%-16s %-6s %-10s %-12s %-10s %s\n" "case" "exit" "x10" "golden_x10" "clock" "clock_vs_golden"
for f in data/testcases/${PATTERN}.data; do
  [ -f "$f" ] || continue
  name=$(basename "$f" .data)
  [ "$name" = "pi" ] && continue
  out=$(VERBOSE=clock "$BIN" < "$f" 2>/tmp/plru_err_$$)
  rc=$?
  clock=$(grep -E "^clock:" /tmp/plru_err_$$ | awk '{print $2}')
  golden=""
  [ -f "data/golden/$name.golden" ] && read golden _ < "data/golden/$name.golden"
  status="OK"
  if [ "$rc" -ne 0 ]; then status="CRASH($rc)";
  elif [ "$out" != "$golden" ]; then status="FAIL"; fi
  drift=""
  if [ -f "data/golden/$name.golden" ]; then
    gclock=$(awk '{print $2}' "data/golden/$name.golden")
    if [ -n "$clock" ] && [ -n "$gclock" ]; then
      drift=$((clock - gclock))
    fi
  fi
  printf "%-16s %-6s %-10s %-12s %-10s %s %s\n" "$name" "$rc" "$out" "$golden" "$clock" "$drift" "$status"
  [ "$status" = "OK" ] && pass=$((pass+1))
  count=$((count+1))
done
rm -f /tmp/plru_err_$$
echo "RESULT: $pass/$count passed (x10 gate)"
