#!/bin/bash
# RV32IM behavior and cycle regression.
# Usage: ./test.sh [pattern]
#   pattern    : optional shell glob for case names (for example "gcd" or "q*")
#   env QUICK=1: skip pi
#   env BP_BIN=: override the simulator binary (default: ./code)
#   env GOLDEN_MD=: override docs/benchmarks.md
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA_DIR="$ROOT/data/testcases"
GOLDEN_MD="${GOLDEN_MD:-$ROOT/docs/benchmarks.md}"
BIN_INPUT="${BP_BIN:-$ROOT/code}"
PATTERN="${1:-*}"
QUICK="${QUICK:-0}"

case "$BIN_INPUT" in
  /*) BIN="$BIN_INPUT" ;;
  *) BIN="$ROOT/$BIN_INPUT" ;;
esac

if [ ! -x "$BIN" ]; then
  printf 'Simulator is not executable: %s\n' "$BIN" >&2
  exit 1
fi
if [ ! -f "$GOLDEN_MD" ]; then
  printf 'Golden table not found: %s\n' "$GOLDEN_MD" >&2
  exit 1
fi

EXPECTED_CASES=(
  array_test1 array_test2 basicopt1 bulgarian expr gcd hanoi lvalue2 magic
  manyarguments multiarray naive qsort queens statement_test superloop tak pi
)

cases=()
for name in "${EXPECTED_CASES[@]}"; do
  [[ "$name" == $PATTERN ]] || continue
  if [ "$QUICK" = "1" ] && [ "$name" = "pi" ]; then
    continue
  fi
  for suffix in data dump; do
    if [ ! -f "$DATA_DIR/$name.$suffix" ]; then
      printf 'Missing RV32IM corpus file: %s\n' "$DATA_DIR/$name.$suffix" >&2
      exit 1
    fi
  done
  cases+=("$name")
done

if [ "${#cases[@]}" -eq 0 ]; then
  printf 'No RV32IM cases matched pattern %s\n' "$PATTERN" >&2
  exit 1
fi

# Keep the longest case last.
ordered=()
pi_selected=0
for name in "${cases[@]}"; do
  if [ "$name" = "pi" ]; then
    pi_selected=1
  else
    ordered+=("$name")
  fi
done
if [ "$pi_selected" -eq 1 ]; then
  ordered+=(pi)
fi

STDOUT_TMP=$(mktemp)
trap 'rm -f "$STDOUT_TMP" 2>/dev/null || true' EXIT

# Prints "<result> <cycles>" from the first benchmark table with those columns.
golden_for_case() {
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
        if (r ~ /^[0-9]+$/ && k ~ /^[0-9]+$/) { print r " " k; exit }
      }
    }' "$GOLDEN_MD"
}

pct() {
  awk -v c="${1:-0}" -v t="${2:-0}" 'BEGIN {
    if (t == 0) { print "0"; exit }
    s=sprintf("%.4f", c * 100 / t)
    sub(/0+$/, "", s); sub(/\.$/, "", s)
    print s
  }'
}

op_count() {
  local dump=$1 regex=$2 count
  count=$(grep -Eow "$regex" "$dump" 2>/dev/null | wc -l) || count=0
  printf '%s\n' "$count"
}

run_case() {
  local data=$1 stderr_out code result
  set +e
  stderr_out=$(VERBOSE=branch,clock,icache,dcache "$BIN" < "$data" 2>&1 >"$STDOUT_TMP" | tr -d '\r')
  code=$?
  set -e
  result=$(tr -d '\r\n' < "$STDOUT_TMP")
  printf '%s\n%s\n' "$code $result" "$stderr_out"
}

printf '%-15s | %-4s | %11s | %11s | %11s | %8s | %7s | %6s | %-17s | %-7s | %-7s | %-7s | %-7s | %-6s | %-6s | %3s | %3s\n' \
  'Case' 'Exit' 'Clock' 'IPC Clock' 'Retired' 'IPC' 'Time' 'x10' 'Golden' \
  'Br%' 'Cond%' 'Jal%' 'Jalr%' 'I$%' 'D$%' 'mul' 'div'
printf '%-15s-+-%-4s-+-%11s-+-%11s-+-%11s-+-%8s-+-%7s-+-%6s-+-%-17s-+-%-7s-+-%-7s-+-%-7s-+-%-7s-+-%-6s-+-%-6s-+-%3s-+-%3s\n' \
  '---------------' '----' '-----------' '-----------' '-----------' '--------' '-------' \
  '------' '-----------------' '-------' '-------' '-------' '-------' '------' '------' '---' '---'

pass=0
count=0
tot_clock=0
tot_ipc_clock=0
tot_retired=0
tot_correct=0
tot_branches=0

for name in "${ordered[@]}"; do
  data="$DATA_DIR/$name.data"
  dump="$DATA_DIR/$name.dump"
  start=$(date +%s%N)
  output=$(run_case "$data")
  end=$(date +%s%N)
  elapsed_ms=$(( (end - start) / 1000000 ))
  elapsed=$(awk -v ms="$elapsed_ms" 'BEGIN { printf "%.2f", ms / 1000 }')

  first_line=${output%%$'\n'*}
  stderr_out=${output#*$'\n'}
  read -r code result <<< "$first_line"

  branch_line=$(printf '%s\n' "$stderr_out" | grep '^branch:' || true)
  type_line=$(printf '%s\n' "$stderr_out" | grep '^branch-type:' || true)
  clock_line=$(printf '%s\n' "$stderr_out" | grep '^clock:' || true)
  ipc_line=$(printf '%s\n' "$stderr_out" | grep '^ipc:' || true)
  ic_line=$(printf '%s\n' "$stderr_out" | grep '^icache:' || true)
  dc_line=$(printf '%s\n' "$stderr_out" | grep '^dcache:' || true)

  correct=$(printf '%s\n' "$branch_line" | awk '{split($2,a,"/"); print a[1]}')
  branches=$(printf '%s\n' "$branch_line" | awk '{split($2,a,"/"); print a[2]}')
  clock=$(printf '%s\n' "$clock_line" | awk '{print $2}')
  ipc=$(printf '%s\n' "$ipc_line" | awk '{print $2}')
  retired=$(printf '%s\n' "$ipc_line" | sed -n 's/.*retired=\([0-9]*\).*/\1/p')
  ipc_clock=$(printf '%s\n' "$ipc_line" | sed -n 's/.*cycles=\([0-9]*\).*/\1/p')
  cond_c=$(printf '%s\n' "$type_line" | sed -n 's/.*cond=\([0-9]*\)\/.*/\1/p')
  cond_t=$(printf '%s\n' "$type_line" | sed -n 's/.*cond=[0-9]*\/\([0-9]*\).*/\1/p')
  jal_c=$(printf '%s\n' "$type_line" | sed -n 's/.*[^r]jal=\([0-9]*\)\/.*/\1/p')
  jal_t=$(printf '%s\n' "$type_line" | sed -n 's/.*[^r]jal=[0-9]*\/\([0-9]*\).*/\1/p')
  jalr_c=$(printf '%s\n' "$type_line" | sed -n 's/.*jalr=\([0-9]*\)\/.*/\1/p')
  jalr_t=$(printf '%s\n' "$type_line" | sed -n 's/.*jalr=[0-9]*\/\([0-9]*\).*/\1/p')
  ic_rate=$(printf '%s\n' "$ic_line" | sed -n 's/.*hit-rate=\([0-9.]*\)%.*/\1/p')
  dc_rate=$(printf '%s\n' "$dc_line" | sed -n 's/.*hit-rate=\([0-9.]*\)%.*/\1/p')

  status=OK
  read -r golden_result golden_clock <<< "$(golden_for_case "$name")"
  if [ "$code" -ne 0 ]; then
    status="CRASH($code)"
  elif [[ ! "$result" =~ ^[0-9]+$ ]]; then
    status='BAD OUTPUT'
  elif [[ ! "$clock" =~ ^[0-9]+$ || ! "$ipc_clock" =~ ^[0-9]+$ || ! "$retired" =~ ^[0-9]+$ || ! "$ipc" =~ ^[0-9]+([.][0-9]+)?$ || ! "$correct" =~ ^[0-9]+$ || ! "$branches" =~ ^[0-9]+$ || ! "$ic_rate" =~ ^[0-9]+([.][0-9]+)?$ || ! "$dc_rate" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    status='BAD STATS'
  elif [ -z "${golden_result:-}" ] || [ -z "${golden_clock:-}" ]; then
    status='NO GOLDEN'
  elif [ "$result" != "$golden_result" ]; then
    status='X10 FAIL'
  elif [ "$clock" != "$golden_clock" ]; then
    status='CYCLE FAIL'
  fi

  br_rate=$(pct "${correct:-0}" "${branches:-0}")
  if [ -n "$type_line" ]; then
    cond_rate=$(pct "${cond_c:-0}" "${cond_t:-0}")
    jal_rate=$(pct "${jal_c:-0}" "${jal_t:-0}")
    jalr_rate=$(pct "${jalr_c:-0}" "${jalr_t:-0}")
  else
    cond_rate=-; jal_rate=-; jalr_rate=-
  fi
  golden_display="${golden_result:-?}/${golden_clock:-?} $status"

  printf '%-15s | %-4s | %11s | %11s | %11s | %8s | %6ss | %6s | %-17s | %-7s | %-7s | %-7s | %-7s | %-6s | %-6s | %3s | %3s\n' \
    "$name" "$code" "${clock:-?}" "${ipc_clock:-?}" "${retired:-?}" "${ipc:-?}" "$elapsed" \
    "${result:-?}" "$golden_display" "$br_rate" "$cond_rate" "$jal_rate" "$jalr_rate" \
    "${ic_rate:-?}" "${dc_rate:-?}" \
    "$(op_count "$dump" 'mul|mulh|mulhsu|mulhu')" \
    "$(op_count "$dump" 'div|divu|rem|remu')"

  count=$((count + 1))
  if [ "$status" = OK ]; then
    pass=$((pass + 1))
  fi
  if [[ "$clock" =~ ^[0-9]+$ ]]; then tot_clock=$((tot_clock + clock)); fi
  if [[ "$ipc_clock" =~ ^[0-9]+$ ]]; then tot_ipc_clock=$((tot_ipc_clock + ipc_clock)); fi
  if [[ "$retired" =~ ^[0-9]+$ ]]; then tot_retired=$((tot_retired + retired)); fi
  if [[ "${correct:-}" =~ ^[0-9]+$ ]]; then tot_correct=$((tot_correct + correct)); fi
  if [[ "${branches:-}" =~ ^[0-9]+$ ]]; then tot_branches=$((tot_branches + branches)); fi
done

overall_ipc=$(awk -v r="$tot_retired" -v c="$tot_ipc_clock" 'BEGIN {
  if (c == 0) print "0.000000"; else printf "%.6f", r / c
}')
printf -- '---------------------------------------------------------------------------------------------------------------------------------------------------\n'
printf 'TOTAL: %d/%d passed  clock=%d  IPC=%s (%d/%d)  branch=%s%% (%d/%d)\n' \
  "$pass" "$count" "$tot_clock" "$overall_ipc" "$tot_retired" "$tot_ipc_clock" \
  "$(pct "$tot_correct" "$tot_branches")" "$tot_correct" "$tot_branches"

[ "$pass" -eq "$count" ]
