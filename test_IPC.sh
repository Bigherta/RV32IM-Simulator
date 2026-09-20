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
  printf '> Weighted IPC = `Σretired / Σipc-cycles` over the table below (not the arithmetic mean of per-case IPC).\n\n'
  printf '| case | x10 | clock | IPC |\n'
  printf '| --- | ---: | ---: | ---: |\n'
} > "$DOC_TMP"

printf '%-12s | %6s | %12s | %8s\n' 'Case' 'x10' 'Clock' 'IPC'
printf '%-12s-+-%6s-+-%12s-+-%8s\n' '------------' '------' '------------' '--------'

count=0
tot_clock=0
tot_retired=0
tot_ipc_cycles=0
for data in "$CORPUS"/*/*.data; do
  [ -f "$data" ] || continue
  name=$(basename "$data" .data)

  set +e
  VERBOSE=clock "$BIN" < "$data" > "$STDOUT_TMP" 2> "$STDERR_TMP"
  code=$?
  set -e
  if [ "$code" -ne 0 ]; then
    printf '%s: simulator exited with status %d\n' "$name" "$code" >&2
    exit 1
  fi

  x10=$(tr -d '\r\n' < "$STDOUT_TMP")
  clock=$(tr -d '\r' < "$STDERR_TMP" | awk '$1 == "clock:" { print $2; exit }')
  ipc=$(tr -d '\r' < "$STDERR_TMP" | awk '$1 == "ipc:" { print $2; exit }')
  retired=$(tr -d '\r' < "$STDERR_TMP" | sed -n 's/.*retired=\([0-9]*\).*/\1/p')
  ipc_cycles=$(tr -d '\r' < "$STDERR_TMP" | sed -n 's/.*cycles=\([0-9]*\).*/\1/p')

  if [[ ! "$x10" =~ ^[0-9]+$ ]]; then
    printf '%s: invalid x10 output: %s\n' "$name" "$x10" >&2
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

  printf '%-12s | %6s | %12s | %8s\n' "$name" "$x10" "$clock" "$ipc"
  printf '| %s | %s | %s | %s |\n' "$name" "$x10" "$clock" "$ipc" >> "$DOC_TMP"
  count=$((count + 1))
  tot_clock=$((tot_clock + clock))
  tot_retired=$((tot_retired + retired))
  tot_ipc_cycles=$((tot_ipc_cycles + ipc_cycles))
done

if [ "$count" -eq 0 ]; then
  printf 'No RV32IM IPC images found under %s\n' "$CORPUS" >&2
  exit 1
fi

weighted_ipc=$(awk -v r="$tot_retired" -v c="$tot_ipc_cycles" 'BEGIN {
  if (c == 0) print "0.000000"; else printf "%.6f", r / c
}')
{
  printf '\n'
  printf '> Weighted IPC = `Σretired / Σipc-cycles` = **%s** (%d/%d); total `clock` = %d.\n' \
    "$weighted_ipc" "$tot_retired" "$tot_ipc_cycles" "$tot_clock"
} >> "$DOC_TMP"

chmod 644 "$DOC_TMP"
mv "$DOC_TMP" "$OUTPUT"
printf '\nRecorded %d cases in %s  weighted IPC=%s (%d/%d)  total clock=%d\n' \
  "$count" "$OUTPUT" "$weighted_ipc" "$tot_retired" "$tot_ipc_cycles" "$tot_clock"
