#!/bin/bash
# Test behavior of the CPU simulator (./code) with the official test data:
#   - correctness: returned value (x10 & 0xFF) vs ../docs/benchmarks.md, crash detection
#   - branch accuracy, total cycles, retired instructions, and core IPC
#     (VERBOSE=branch,clock)
# Usage: ./test.sh [pattern]
#   pattern: optional glob filter for test names (e.g. "*sort*", "q*")
#   env BP_BIN: override executable path (default: ./code)
set -euo pipefail

cd "$(dirname "$0")"
DATA_DIR="data/testcases"
# Golden data = the repo-root markdown table in ../docs/benchmarks.md (column "result" = x10,
# column "cycles" = reference clock), kept in one place so the numbers cannot
# drift between per-case files and the docs.
GOLDEN_MD="${GOLDEN_MD:-../docs/benchmarks.md}"
BIN="${BP_BIN:-./code}"
PATTERN="${1:-*}"

STDOUT_TMP=$(mktemp)
trap 'rm -f "$STDOUT_TMP"' EXIT

# Golden data lives in the single authoritative markdown table ../docs/benchmarks.md.
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

run_test() {
  local data=$1
  # NOTE: on Windows the simulator emits CRLF on *both* stdout and stderr.
  # Every capture below must strip CR, otherwise:
  #   - "$result" becomes "123\r" and never equals golden "123"  -> every case FAILs
  #   - "$clock" becomes "341\r" and "\r" rewinds the terminal cursor mid-row,
  #     shredding the table layout (and breaking arithmetic like tot_clock+=clock)
  local stderr_out code=0
  set +e
  stderr_out=$(VERBOSE=branch,clock "$BIN" < "$data" 2>&1 >"$STDOUT_TMP" | tr -d '\r')
  code=$?
  set -e
  result=$(tr -d '\r' < "$STDOUT_TMP" 2>/dev/null || echo "")
  branch_line=$(echo "$stderr_out" | grep "^branch:" || echo "branch: 0/0 correct (0.00%)")
  clock_line=$(echo "$stderr_out" | grep "^clock:" || echo "clock: 0")
  ipc_line=$(echo "$stderr_out" | grep "^ipc:" || echo "ipc: 0.000000 retired=0 cycles=0")
  correct=$(echo "$branch_line" | awk '{print $2}' | cut -d/ -f1)
  total=$(echo "$branch_line" | awk '{print $2}' | cut -d/ -f2)
  rate=$(echo "$branch_line" | awk '{print $4}' | tr -d '()%')
  clock=$(echo "$clock_line" | awk '{print $2}')
  ipc=$(echo "$ipc_line" | awk '{print $2}')
  retired=$(echo "$ipc_line" | awk '{print $3}' | cut -d= -f2)
  ipc_clock=$(echo "$ipc_line" | awk '{print $4}' | cut -d= -f2)
  echo "$code $correct $total $rate $clock $ipc_clock $retired $ipc $result"
}

printf "%-16s | %-4s | %-13s | %-9s | %-10s | %-10s | %-10s | %-8s | %-7s | %-5s | %s\n" \
  "Program" "Exit" "Correct/Total" "Accuracy" "Clock" "IPC Clock" "Retired" "IPC" "Time" "x10" "Golden"
printf "%-16s-+-%-4s-+-%-13s-+-%-9s-+-%-10s-+-%-10s-+-%-10s-+-%-8s-+-%-7s-+-%-5s-+-%s\n" \
  "----------------" "----" "-------------" "---------" "----------" "----------" "----------" "--------" "-------" "-----" "------"

tot_correct=0
tot_total=0
tot_clock=0
tot_ipc_clock=0
tot_retired=0
pass=0
count=0

# Order test points so `pi` (the ~6-9min slow case) runs last:
# build an ordered list = all matched points except pi, then pi appended at
# the end (only when pi is in the matched set). Preserves the PATTERN filter.
_others=()
_pi_file=""
for _f in "$DATA_DIR"/${PATTERN}.data; do
  [ -f "$_f" ] || continue
  _bn=$(basename "$_f" .data)
  if [ "$_bn" = "pi" ]; then _pi_file="$_f"; else _others+=("$_f"); fi
done
_ordered=("${_others[@]}")
if [ -n "$_pi_file" ]; then _ordered+=("$_pi_file"); fi

for data in "${_ordered[@]}"; do
  [ -f "$data" ] || continue
  name=$(basename "$data" .data)
  start=$(date +%s%N)
  read code correct total rate clock ipc_clock retired ipc result <<< "$(run_test "$data")"
  end=$(date +%s%N)
  elapsed_ms=$(((end - start) / 1000000))
  elapsed=$(awk "BEGIN{printf \"%.2f\", $elapsed_ms/1000}")

  golden_x10=""
  golden_clock=""
  # Column "result" is the x10 gate; column "cycles" is reference only.
  read -r golden_x10 golden_clock <<< "$(golden_for_case "$name")" || true

  status=""
  if [ "$code" -ne 0 ]; then
    status="CRASH($code)"
  elif [ "$ipc_clock" -le 0 ]; then
    status="NO IPC STATS"
  elif [ -n "$golden_x10" ]; then
    if [ "$result" = "$golden_x10" ]; then status="OK"; else status="FAIL"; fi
  fi

  printf "%-16s | %-4s | %4s/%-8s | %-9s | %-10s | %-10s | %-10s | %-8s | %-7s | %-5s | %s\n" \
    "$name" "$code" "$correct" "$total" "${rate}%" "$clock" \
    "$ipc_clock" "$retired" "$ipc" "${elapsed}s" "$result" "$status"

  if [ "$status" = "OK" ]; then pass=$((pass + 1)); fi
  tot_correct=$((tot_correct + correct))
  tot_total=$((tot_total + total))
  tot_clock=$((tot_clock + clock))
  tot_ipc_clock=$((tot_ipc_clock + ipc_clock))
  tot_retired=$((tot_retired + retired))
  count=$((count + 1))
done

if [ "$count" -gt 0 ]; then
  if [ "$tot_total" -gt 0 ]; then
    overall=$(awk "BEGIN{printf \"%.2f\", $tot_correct*100/$tot_total}")
  else
    overall="0.00"
  fi
  if [ "$tot_ipc_clock" -gt 0 ]; then
    overall_ipc=$(awk "BEGIN{printf \"%.6f\", $tot_retired/$tot_ipc_clock}")
  else
    overall_ipc="0.000000"
  fi
  printf "%-16s | %-4s | %4s/%-8s | %-9s | %-10s | %-10s | %-10s | %-8s | %-7s | %-5s | %s\n" \
    "TOTAL" "" "$tot_correct" "$tot_total" "${overall}%" "$tot_clock" \
    "$tot_ipc_clock" "$tot_retired" "$overall_ipc" "" "" "$pass/$count passed"
  [ "$pass" -eq "$count" ] || exit 1
fi
