#!/bin/bash
# A/B harness for the rv32im corpus living next to this script.
#
#   M arm = -march=rv32i_zmmul   -> mul/mulh/... inline hardware instructions
#   I arm = -march=rv32i         -> mul compiled into __mulsi3 soft routine
#
# Per case/arm it reports:
#   - correctness : returned value (x10 & 0xFF) vs data/golden/<case>.golden col 1
#   - consistency : M and I arms must return the SAME x10 (same semantics, two
#                   multiplication implementations -- a mismatch is always a bug)
#   - performance : clock cycles + branch prediction accuracy (VERBOSE=branch,clock)
#   - benefit     : clock(M) vs clock(I) -> delta% and speedup (the M gain)
#   - static mul  : mul opcode count in the .dump (explains cases with zero gain,
#                   e.g. multiarray where gcc turns i*10 into shifts+adds)
#
# NOTE on golden: its second column is the CLOCK OF THE COURSE rv32i IMAGE, whose
# layout differs from these self-built images. Only column 1 (x10) is compared.
#
# Usage: ./test_m.sh [pattern]
#   pattern    : glob filter on case name (e.g. "bul*", "pi")
#   env QUICK=1: skip pi (each arm costs several minutes even under WSL)
#   env BP_BIN=: override simulator binary (default ./code at repo root)
# Exit code is non-zero if any case failed.
#
# Recommended: run under WSL with a native ELF binary (see AGENTS.md); process
# startup there is ~0.008s vs ~6.4s for the MinGW binary under MSYS2.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"     # .../data/testcases_rv32im
ROOT="$(cd "$HERE/../.." && pwd)"                        # repo root
cd "$ROOT"

_BIN="${BP_BIN:-./code}"
# Resolve to an absolute path BEFORE cd-ing to the repo root, otherwise a
# relative BP_BIN (e.g. "../code.mingw") silently breaks after the cd.
case $_BIN in
  /*) BIN="$_BIN" ;;
  *)  BIN="$PWD/$_BIN" ;;
esac
PATTERN="${1:-*}"
QUICK="${QUICK:-0}"
GOLDEN_DIR="data/golden"

# "|| true" keeps a failing cleanup from overwriting the harness's exit status.
STDOUT_TMP=$(mktemp)
trap 'rm -f "$STDOUT_TMP" 2>/dev/null || true' EXIT

# Static count of hardware 'mul' opcodes in a .dump (word-boundary, so it does
# not also match mulh/mulhu/mulhsu).
mul_count() {
  local dump=$1
  local n
  [ -f "$dump" ] || { echo "-"; return; }
  n=$(grep -owc 'mul' "$dump" 2>/dev/null) || n=0
  echo "$n"
}

# Prints: "<exit> <correct> <total> <rate> <clock> <result>"
run_arm() {
  local data=$1
  local stderr_out code=0 result
  set +e
  # The MinGW binary emits CRLF on BOTH stdout and stderr under Windows. Without
  # stripping CR, "$result"="123\r" never equals golden "123", and a "\r" inside
  # "$clock" rewinds the cursor and shreds the table (same fix as in test.sh).
  stderr_out=$(VERBOSE=branch,clock "$BIN" < "$data" 2>&1 >"$STDOUT_TMP" | tr -d '\r')
  code=$?
  set -e
  result=$(tr -d '\r' < "$STDOUT_TMP" 2>/dev/null || echo "")

  local branch_line clock_line
  branch_line=$(echo "$stderr_out" | grep "^branch:" || echo "branch: 0/0 correct (0.00%)")
  clock_line=$(echo "$stderr_out" | grep "^clock:" || echo "clock: 0")
  echo "$code $(echo "$branch_line" | awk '{print $2}' | cut -d/ -f1) \
$(echo "$branch_line" | awk '{print $2}' | cut -d/ -f2) \
$(echo "$branch_line" | awk '{print $4}' | tr -d '()%') \
$(echo "$clock_line" | awk '{print $2}') $result"
}

# ---------------------------------------------------------------- case list ----
cases=()
for _f in "$HERE"/M/${PATTERN}.data "$HERE"/I/${PATTERN}.data; do
  [ -f "$_f" ] || continue
  _bn=$(basename "$_f" .data)
  _dup=0
  for _c in ${cases[@]+"${cases[@]}"}; do
    if [ "$_c" = "$_bn" ]; then _dup=1; fi
  done
  if [ "$_dup" -eq 0 ]; then cases+=("$_bn"); fi
done

# Order cases so pi (minutes per arm) runs last; drop it entirely under QUICK=1.
ordered=()
pi_case=""
for _c in ${cases[@]+"${cases[@]}"}; do
  if [ "$_c" = "pi" ]; then pi_case="$_c"; else ordered+=("$_c"); fi
done
if [ -n "$pi_case" ] && [ "$QUICK" != "1" ]; then
  ordered+=("$pi_case")
fi

# -------------------------------------------------------------------- report ---
printf "%-15s | %-3s | %-4s | %13s | %-8s | %11s | %7s | %5s | %-6s | %s\n" \
  "Case" "Arm" "Exit" "Correct/Total" "Accuracy" "Clock" "Time" "x10" "Golden" "mul"
printf "%-15s-+-%-3s-+-%-4s-+-%13s-+-%-8s-+-%11s-+-%7s-+-%5s-+-%-6s-+-%s\n" \
  "---------------" "---" "----" "-------------" "--------" "-----------" "-------" \
  "-----" "------" "---"

tot_pass=0
tot_count=0
tot_m_clock=0
tot_i_clock=0

for case_name in ${ordered[@]+"${ordered[@]}"}; do
  m_clock=0; i_clock=0
  m_ok=0;    i_ok=0
  have_m=0;  have_i=0

  for arm in M I; do
    data="$HERE/$arm/$case_name.data"
    if [ ! -f "$data" ]; then continue; fi

    start=$(date +%s%N)
    read -r code correct total rate clock result <<< "$(run_arm "$data")"
    end=$(date +%s%N)
    # Must use bash integer arithmetic here: date +%s%N yields ~1.8e18, which
    # exceeds awk's 53-bit float mantissa and silently corrupts the difference.
    elapsed_ms=$(( (end - start) / 1000000 ))
    elapsed=$(awk "BEGIN{printf \"%.2f\", $elapsed_ms/1000}")

    golden_x10=""
    if [ -f "$GOLDEN_DIR/$case_name.golden" ]; then
      read -r golden_x10 _gclk < <(tr -d '\r' < "$GOLDEN_DIR/$case_name.golden") || true
    fi

    if [ "$code" -ne 0 ]; then
      status="CRASH($code)"
    elif [ -n "$golden_x10" ]; then
      if [ "$result" = "$golden_x10" ]; then status="OK"; else status="FAIL"; fi
    else
      status="(no golden)"
    fi

    printf "%-15s | %-3s | %-4s | %5s/%-7s | %-8s | %11s | %7s | %5s | %-6s | %s\n" \
      "$case_name" "$arm" "$code" "$correct" "$total" "${rate}%" "$clock" \
      "${elapsed}s" "$result" "$status" "$(mul_count "$HERE/$arm/$case_name.dump")"

    if [ "$arm" = "M" ]; then
      have_m=1; m_clock=$clock; m_x10=$result; m_code=$code
      if [ "$status" = "OK" ]; then m_ok=1; fi
    else
      have_i=1; i_clock=$clock; i_x10=$result; i_code=$code
      if [ "$status" = "OK" ]; then i_ok=1; fi
    fi
  done

  # ---- per-case verdict: x10 agreement across arms + M-only speedup ----
  verdict=""
  if [ "$have_m" -eq 1 ] && [ "$have_i" -eq 1 ]; then
    if [ "$m_x10" = "$i_x10" ] && [ "$m_code" -eq 0 ] && [ "$i_code" -eq 0 ]; then
      verdict="consistent"
    else
      verdict="X10 MISMATCH (M=$m_x10 I=$i_x10)"
    fi
    if [ "$i_clock" -gt 0 ] && [ "$m_clock" -gt 0 ]; then
      delta=$(awk "BEGIN{printf \"%+.2f\", ($m_clock-$i_clock)*100/$i_clock}")
      speed=$(awk "BEGIN{printf \"%.3f\", $i_clock/$m_clock}")
    else
      delta="n/a"; speed="n/a"
    fi
    printf "  -> %-13s dClock(M/I) = %8s%%   speedup = %7sx   cross-arm x10: %s\n" \
      "$case_name" "$delta" "$speed" "$verdict"
    tot_m_clock=$((tot_m_clock + m_clock))
    tot_i_clock=$((tot_i_clock + i_clock))
  else
    printf "  -> %-13s INCOMPLETE (need both arms)\n" "$case_name"
  fi

  tot_count=$((tot_count + 1))
  if [ "$m_ok" -eq 1 ] && [ "$i_ok" -eq 1 ] && [ "$verdict" = "consistent" ]; then
    tot_pass=$((tot_pass + 1))
  fi
done

if [ "$tot_count" -gt 0 ]; then
  printf -- "-----------------------------------------------------------------------------------------\n"
  if [ "$tot_i_clock" -gt 0 ]; then
    overall=$(awk "BEGIN{printf \"%+.2f\", ($tot_m_clock-$tot_i_clock)*100/$tot_i_clock}")
  else
    overall="n/a"
  fi
  printf "TOTAL: %d/%d cases passed   clock M=%s  I=%s   overall dClock = %s%%\n" \
    "$tot_pass" "$tot_count" "$tot_m_clock" "$tot_i_clock" "$overall"
  [ "$tot_pass" -eq "$tot_count" ] || exit 1
fi
