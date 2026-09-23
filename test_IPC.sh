#!/bin/bash
# Run the RV32IM IPC corpus and regenerate docs/ipc_benchmarks.md.
# Usage: BP_BIN=/path/to/code ./test_IPC.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORPUS="$ROOT/data/testcases_ipc"
OUTPUT="$ROOT/docs/ipc_benchmarks.md"
BIN_INPUT="${BP_BIN:-$ROOT/code}"

case "$BIN_INPUT" in
  /*) BIN="$BIN_INPUT" ;;
  *) BIN="$ROOT/$BIN_INPUT" ;;
esac

if [ ! -x "$BIN" ]; then
  printf 'Simulator is not executable: %s\n' "$BIN" >&2
  exit 1
fi

STDOUT_TMP=$(mktemp)
STDERR_TMP=$(mktemp)
DOC_TMP=$(mktemp "$ROOT/docs/.ipc_benchmarks.XXXXXX")
trap 'rm -f "$STDOUT_TMP" "$STDERR_TMP" "$DOC_TMP"' EXIT

{
  printf '# RV32IM IPC Benchmarks\n\n'
  printf '> Corpus: `data/testcases_ipc/`; ISA: RV32IM. `clock` is the end-to-end cycle count, while IPC uses the simulator core interval frozen when HALT commits.\n'
  printf '> Geomean IPC = geometric mean of per-case IPC (`retired / ipc-cycles`) over the table below.\n'
  printf '> `RESULT_FULL=1` prints full 32-bit x10. `VERBOSE=cftrace,profile` adds host-only control-flow fetch/execute/commit records and a commit-filtered dynamic mix on stderr.\n\n'
  printf '| case | x10 | clock | retired | ipc-cycles | IPC |\n'
  printf '| --- | ---: | ---: | ---: | ---: | ---: |\n'
} > "$DOC_TMP"

printf '%-12s | %10s | %12s | %12s | %12s | %8s\n' 'Case' 'x10' 'Clock' 'Retired' 'IPC cycles' 'IPC'
printf '%-12s-+-%10s-+-%12s-+-%12s-+-%12s-+-%8s\n' '------------' '----------' '------------' '------------' '------------' '--------'

count=0
tot_clock=0
tot_retired=0
tot_ipc_cycles=0
sum_log_ipc=0
for data in "$CORPUS"/*/*.data; do
  [ -f "$data" ] || continue
  name=$(basename "$data" .data)

  set +e
  RESULT_FULL=1 VERBOSE=clock "$BIN" < "$data" > "$STDOUT_TMP" 2> "$STDERR_TMP"
  code=$?
  set -e
  if [ "$code" -ne 0 ]; then
    printf '%s: simulator exited with status %d\n' "$name" "$code" >&2
    exit 1
  fi

  x10_raw=$(tr -d '\r\n' < "$STDOUT_TMP")
  printf -v x10 '%08x' "$x10_raw"
  clock=$(tr -d '\r' < "$STDERR_TMP" | awk '$1 == "clock:" { print $2; exit }')
  ipc=$(tr -d '\r' < "$STDERR_TMP" | awk '$1 == "ipc:" { print $2; exit }')
  retired=$(tr -d '\r' < "$STDERR_TMP" | sed -n 's/.*retired=\([0-9]*\).*/\1/p')
  ipc_cycles=$(tr -d '\r' < "$STDERR_TMP" | sed -n 's/.*cycles=\([0-9]*\).*/\1/p')

  if [[ ! "$x10_raw" =~ ^[0-9]+$ ]]; then
    printf '%s: invalid x10 output: %s\n' "$name" "$x10_raw" >&2
    exit 1
  fi
  if [ "$x10_raw" -ne 0 ]; then
    printf '%s: self-check failed: x10=%s (expected 0)\n' "$name" "$x10" >&2
    exit 1
  fi
  if [[ ! "$clock" =~ ^[0-9]+$ ]]; then
    printf '%s: missing or invalid clock statistic\n' "$name" >&2
    exit 1
  fi
  if [[ ! "$ipc" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    printf '%s: missing or invalid IPC statistic\n' "$name" >&2
    exit 1
  fi
  if [[ ! "$retired" =~ ^[0-9]+$ ]]; then
    printf '%s: missing or invalid retired statistic\n' "$name" >&2
    exit 1
  fi
  if [[ ! "$ipc_cycles" =~ ^[0-9]+$ ]]; then
    printf '%s: missing or invalid ipc-cycles statistic\n' "$name" >&2
    exit 1
  fi

  printf '%-12s | %10s | %12s | %12s | %12s | %8s\n' \
    "$name" "$x10" "$clock" "$retired" "$ipc_cycles" "$ipc"
  printf '| %s | %s | %s | %s | %s | %s |\n' \
    "$name" "$x10" "$clock" "$retired" "$ipc_cycles" "$ipc" >> "$DOC_TMP"
  count=$((count + 1))
  tot_clock=$((tot_clock + clock))
  tot_retired=$((tot_retired + retired))
  tot_ipc_cycles=$((tot_ipc_cycles + ipc_cycles))
  sum_log_ipc=$(awk -v s="$sum_log_ipc" -v r="$retired" -v c="$ipc_cycles" 'BEGIN { print s + log(r / c) }')
done

if [ "$count" -eq 0 ]; then
  printf 'No RV32IM IPC images found under %s\n' "$CORPUS" >&2
  exit 1
fi

geomean_ipc=$(awk -v s="$sum_log_ipc" -v n="$count" 'BEGIN {
  if (n == 0) print "0.000000"; else printf "%.6f", exp(s / n)
}')
{
  printf '\n'
  printf '> Geomean IPC = geometric mean of per-case IPC = **%s** over %d cases; totals retired=%d ipc-cycles=%d clock=%d.\n' \
    "$geomean_ipc" "$count" "$tot_retired" "$tot_ipc_cycles" "$tot_clock"
} >> "$DOC_TMP"

chmod 644 "$DOC_TMP"
mv "$DOC_TMP" "$OUTPUT"
printf '\nRecorded %d cases in %s  geomean IPC=%s over %d cases  totals retired=%d ipc-cycles=%d clock=%d\n' \
  "$count" "$OUTPUT" "$geomean_ipc" "$count" "$tot_retired" "$tot_ipc_cycles" "$tot_clock"
