#!/bin/bash
# A/B harness for the RV32M corpus (data/testcases_rv32im/).
#
#   M arm = -march=rv32im   -> mul/mulh/... AND div/divu/rem/remu inline HARDWARE
#                              (link line drops libdiv.S)
#   I arm = -march=rv32i    -> everything compiled into soft routines
#                              (__mulsi3 for '*', libdiv.S for '/' and '%')
#
# Both arms share crt0_course.S / link_course.ld / optimization flags, so the clock
# delta is the clean RV32M gain (multiplication + division hardware).
#
# Per case/arm it reports -- the same set of columns as docs/benchmarks.md, plus the
# wall-clock runtime:
#   Exit | Clock | Time | x10 | Golden
#   | Br% | Cond% | Jal% | Jalr%    (branch prediction, four subtypes)
#   | I$% | D$%                      (L1I / L1D hit rate)
#   | mul | div                      (static hardware-op count in the .dump)
#
# Per-case verdict line: dClock(M/I)%, I/M speedup, both runtimes, both brAcc,
# and cross-arm x10 agreement. TOTAL line sums clock + runtime over all cases
# and reports the weighted branch accuracy of each arm (total correct/total).
#
# NOTE on branch accuracy: the cond/jal/jalr/branch percentages describe THIS arm's own
#   control flow. The two arms compile to different code (software routines add many extra
#   branches), so cross-arm accuracy figures are NOT directly comparable -- validating the
#   predictor means running the SAME image and comparing against docs/benchmarks.md.
# NOTE on golden: docs/benchmarks.md records the clock of the COURSE rv32i image, whose
#   layout differs from these self-built images. Only its x10 column is used here.
# NOTE on magic: data/testcases/magic.c evaluates `make[x-1][j]` with x==0 behind a
#   short-circuit guard -- undefined behaviour that gcc 13.2 -O2 turns into an illegal
#   load address (DCache PrRd assertion, addr=13). The corpus is therefore built with
#   -O1 (BOTH arms, so the A/B stays fair). -O0/-O1/-Os all give x10=106.
#
# Usage: ./test_M.sh [pattern]
#   pattern    : glob filter on case name (e.g. "gcd", "pi")
#   env QUICK=1: skip pi (each arm costs minutes even under WSL)
#   env BP_BIN=: override simulator binary (default ./code at repo root)
# Exit code is non-zero if any case failed.
#
# Recommended: run under WSL with a native ELF binary (see AGENTS.md); process
# startup there is ~0.008s vs ~6.4s for the MinGW binary under MSYS2.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"     # repo root (script lives there)
ROOT="$HERE"                                             # repo root
CORPUS="$ROOT/data/testcases_rv32im"                     # M/ + I/ arms live here
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
# Golden data = the markdown table in docs/benchmarks.md (column "result" = x10,
# column "cycles" = reference clock). GOLDEN_MD may be overridden by env.
GOLDEN_MD="${GOLDEN_MD:-docs/benchmarks.md}"

# "|| true" keeps a failing cleanup from overwriting the harness's exit status.
STDOUT_TMP=$(mktemp)
trap 'rm -f "$STDOUT_TMP" 2>/dev/null || true' EXIT

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

# Static count of hardware mul opcodes (-w keeps 'mul' from matching mulh/mulhu/mulhsu).
mul_count() {
  local dump=$1 n
  [ -f "$dump" ] || { echo "-"; return; }
  n=$(grep -owc 'mul' "$dump" 2>/dev/null) || n=0
  echo "$n"
}

# Static count of hardware div/rem opcodes (-w keeps 'rem' from matching 'remu').
div_count() {
  local dump=$1 n
  [ -f "$dump" ] || { echo "-"; return; }
  n=$(grep -owE 'div|divu|rem|remu' "$dump" 2>/dev/null | wc -l) || n=0
  echo "$n"
}

# Percent with up to 4 decimals, trailing zeros stripped (0 when the denominator is 0).
# Matches the cond-%/jal-%/jalr-%/branch-% convention in docs/benchmarks.md.
pct() {
  awk -v c="${1:-0}" -v t="${2:-0}" 'BEGIN{
    if (t == 0) { print "0"; exit }
    s = sprintf("%.4f", c * 100 / t)
    sub(/0+$/, "", s); sub(/\.$/, "", s)
    print s
  }'
}

# Prints: "<exit> <bc> <bt> <brate> <cond_c> <cond_t> <jal_c> <jal_t> <jalr_c> <jalr_t>
#          <clock> <icache%> <dcache%> <result>"
run_arm() {
  local data=$1
  local stderr_out code=0 result
  set +e
  # The MinGW binary emits CRLF on BOTH stdout and stderr under Windows. Without
  # stripping CR, "$result"="123\r" never equals golden "123", and a "\r" inside
  # "$clock" rewinds the cursor and shreds the table (same fix as in test.sh).
  stderr_out=$(VERBOSE=branch,clock,icache,dcache "$BIN" < "$data" 2>&1 >"$STDOUT_TMP" | tr -d '\r')
  code=$?
  set -e
  result=$(tr -d '\r' < "$STDOUT_TMP" 2>/dev/null || echo "")

  local branch_line clock_line type_line ic_line dc_line
  branch_line=$(echo "$stderr_out" | grep "^branch:" || echo "branch: 0/0 correct (0.00%)")
  clock_line=$(echo "$stderr_out" | grep "^clock:" || echo "clock: 0")
  type_line=$(echo "$stderr_out" | grep "^branch-type:" || echo "")
  ic_line=$(echo "$stderr_out" | grep "^icache:" || echo "hit-rate=0%")
  dc_line=$(echo "$stderr_out" | grep "^dcache:" || echo "hit-rate=0%")

  local cond_c cond_t jal_c jal_t jalr_c jalr_t
  cond_c=$(echo "$type_line" | sed -n 's/.*cond=\([0-9]*\)\/.*/\1/p')
  cond_t=$(echo "$type_line" | sed -n 's/.*cond=[0-9]*\/\([0-9]*\).*/\1/p')
  jal_c=$(echo "$type_line" | sed -n 's/.*[^r]jal=\([0-9]*\)\/.*/\1/p')
  jal_t=$(echo "$type_line" | sed -n 's/.*[^r]jal=[0-9]*\/\([0-9]*\).*/\1/p')
  jalr_c=$(echo "$type_line" | sed -n 's/.*jalr=\([0-9]*\)\/.*/\1/p')
  jalr_t=$(echo "$type_line" | sed -n 's/.*jalr=[0-9]*\/\([0-9]*\).*/\1/p')

  local ic dc
  ic=$(echo "$ic_line" | sed -n 's/.*hit-rate=\([0-9.]*\)%.*/\1/p')
  dc=$(echo "$dc_line" | sed -n 's/.*hit-rate=\([0-9.]*\)%.*/\1/p')

  echo "$code $(echo "$branch_line" | awk '{print $2}' | cut -d/ -f1) \
$(echo "$branch_line" | awk '{print $2}' | cut -d/ -f2) \
$(echo "$branch_line" | awk '{print $4}' | tr -d '()%') \
${cond_c:-0} ${cond_t:-0} ${jal_c:-0} ${jal_t:-0} ${jalr_c:-0} ${jalr_t:-0} \
$(echo "$clock_line" | awk '{print $2}') ${ic:-0} ${dc:-0} $result"
}

# ---------------------------------------------------------------- case list ----
cases=()
for _f in "$CORPUS"/M/${PATTERN}.data "$CORPUS"/I/${PATTERN}.data; do
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
printf "%-15s | %-3s | %-4s | %11s | %7s | %6s | %-6s | %-7s | %-7s | %-7s | %-7s | %-6s | %-6s | %3s | %3s\n" \
  "Case" "Arm" "Exit" "Clock" "Time" "x10" "Golden" "Br%" "Cond%" "Jal%" "Jalr%" "I$%" "D$%" "mul" "div"
printf "%-15s-+-%-3s-+-%-4s-+-%11s-+-%7s-+-%6s-+-%-6s-+-%-7s-+-%-7s-+-%-7s-+-%-7s-+-%-6s-+-%-6s-+-%3s-+-%3s\n" \
  "---------------" "---" "----" "-----------" "-------" "------" "------" "-------" \
  "-------" "-------" "-------" "------" "------" "---" "---"

tot_pass=0
tot_count=0
tot_m_clock=0
tot_i_clock=0
tot_m_ms=0
tot_i_ms=0
tot_m_brc=0; tot_m_brt=0
tot_i_brc=0; tot_i_brt=0

for case_name in ${ordered[@]+"${ordered[@]}"}; do
  m_clock=0; i_clock=0
  m_ok=0;    i_ok=0
  have_m=0;  have_i=0

  for arm in M I; do
    data="$CORPUS/$arm/$case_name.data"
    if [ ! -f "$data" ]; then continue; fi

    start=$(date +%s%N)
    read -r code correct total rate cond_c cond_t jal_c jal_t jalr_c jalr_t \
      clock ic_rate dc_rate result <<< "$(run_arm "$data")"
    end=$(date +%s%N)
    # Must use bash integer arithmetic here: date +%s%N yields ~1.8e18, which
    # exceeds awk's 53-bit float mantissa and silently corrupts the difference.
    elapsed_ms=$(( (end - start) / 1000000 ))
    elapsed=$(awk "BEGIN{printf \"%.2f\", $elapsed_ms/1000}")

    br_pct=$(pct "$correct" "$total")
    cond_pct=$(pct "$cond_c" "$cond_t")
    jal_pct=$(pct "$jal_c" "$jal_t")
    jalr_pct=$(pct "$jalr_c" "$jalr_t")

    golden_x10=""
    read -r golden_x10 _gclk <<< "$(golden_for_case "$case_name")" || true

    if [ "$code" -ne 0 ]; then
      status="CRASH($code)"
    elif [ -n "$golden_x10" ]; then
      if [ "$result" = "$golden_x10" ]; then status="OK"; else status="FAIL"; fi
    else
      status="(no golden)"
    fi

    printf "%-15s | %-3s | %-4s | %11s | %6ss | %6s | %-6s | %-7s | %-7s | %-7s | %-7s | %-6s | %-6s | %3s | %3s\n" \
      "$case_name" "$arm" "$code" "$clock" "$elapsed" "$result" "$status" \
      "$br_pct" "$cond_pct" "$jal_pct" "$jalr_pct" "$ic_rate" "$dc_rate" \
      "$(mul_count "$CORPUS/$arm/$case_name.dump")" \
      "$(div_count "$CORPUS/$arm/$case_name.dump")"

    if [ "$arm" = "M" ]; then
      have_m=1; m_clock=$clock; m_x10=$result; m_code=$code; m_br=$br_pct; m_ms=$elapsed_ms
      tot_m_brc=$((tot_m_brc + correct)); tot_m_brt=$((tot_m_brt + total))
      if [ "$status" = "OK" ]; then m_ok=1; fi
    else
      have_i=1; i_clock=$clock; i_x10=$result; i_code=$code; i_br=$br_pct; i_ms=$elapsed_ms
      tot_i_brc=$((tot_i_brc + correct)); tot_i_brt=$((tot_i_brt + total))
      if [ "$status" = "OK" ]; then i_ok=1; fi
    fi
  done

  # ---- per-case verdict: x10 agreement across arms + RV32M gain + runtimes ----
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
    rt_m=$(awk "BEGIN{printf \"%.2f\", $m_ms/1000}")
    rt_i=$(awk "BEGIN{printf \"%.2f\", $i_ms/1000}")
    printf "  -> %-13s dClock(M/I) = %8s%%   speedup = %7sx   runtime M=%ss I=%ss   brAcc: M=%s%%  I=%s%%   cross-arm x10: %s\n" \
      "$case_name" "$delta" "$speed" "$rt_m" "$rt_i" "$m_br" "$i_br" "$verdict"
    tot_m_clock=$((tot_m_clock + m_clock))
    tot_i_clock=$((tot_i_clock + i_clock))
    tot_m_ms=$((tot_m_ms + m_ms))
    tot_i_ms=$((tot_i_ms + i_ms))
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
  rt_m=$(awk "BEGIN{printf \"%.2f\", $tot_m_ms/1000}")
  rt_i=$(awk "BEGIN{printf \"%.2f\", $tot_i_ms/1000}")
  m_brtot=$(pct "$tot_m_brc" "$tot_m_brt")
  i_brtot=$(pct "$tot_i_brc" "$tot_i_brt")
  br_delta=$(awk "BEGIN{printf \"%+.4f\", $m_brtot - $i_brtot}")
  printf "TOTAL: %d/%d cases passed   clock M=%s  I=%s   overall dClock = %s%%   runtime M=%ss  I=%ss\n" \
    "$tot_pass" "$tot_count" "$tot_m_clock" "$tot_i_clock" "$overall" "$rt_m" "$rt_i"
  printf "TOTAL: branch M=%s%% (%d/%d)   I=%s%% (%d/%d)   dBr = %s pp\n" \
    "$m_brtot" "$tot_m_brc" "$tot_m_brt" "$i_brtot" "$tot_i_brc" "$tot_i_brt" "$br_delta"
  [ "$tot_pass" -eq "$tot_count" ] || exit 1
fi
