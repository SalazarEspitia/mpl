#!/usr/bin/env bash
# The write barrier's entangled branch can pin deeper than the writing thread (assign.c:217), on upstream main.
#   wb217    with the debug runtime: the assertion at hierarchical-heap.c:572
#   wb217b   release runtime: "write" (with the store) crashes or hangs, "nowrite" (the control) is right
# Usage: ./run.sh    Settings: RUNS (5 per row; the debug row runs 3), PROCS (4), MPL or MPL_MAIN (an existing
# compiler built from upstream main), and those of common/lib.sh.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../common/lib.sh"
RUNS=${RUNS:-5}
DRUNS=${DRUNS:-3}
PROCS=${PROCS:-4}
EV_TLIMIT=${WB_TLIMIT:-60}

ev_init write-barrier-depth "Write barrier: an unpin depth deeper than the writing thread (upstream main)"
MAIN=$(ev_mpl main single) || exit 1
ev_compile "$MAIN" wb217.dbg "$HERE/wb217.mlb" -debug-runtime true || exit 1
ev_compile "$MAIN" wb217b "$HERE/wb217b.mlb" || exit 1

OKB='wrong sums 0, list still sums right: true'
for i in $(seq 1 "$DRUNS"); do ev_run "wb217, debug runtime" '^end$' "$EV_O/bin/wb217.dbg" @mpl procs "$PROCS" --; done
for i in $(seq 1 "$RUNS"); do
  ev_run "wb217b nowrite, release (control)" "$OKB" "$EV_O/bin/wb217b" @mpl procs "$PROCS" -- nowrite
  ev_run "wb217b write, release" "$OKB" "$EV_O/bin/wb217b" @mpl procs "$PROCS" -- write
done
ev_row "wb217, debug runtime" "every run stops on the assertion \`depth <= thread->currentDepth\` (macOS arm64: 3 of 3)"
ev_row "wb217b nowrite, release (control)" "all right (macOS arm64: 5 of 5)"
ev_row "wb217b write, release" "crashes or hangs (macOS arm64: 5 of 5 SIGSEGV)"
missed=$(cat "$EV_O"/runs/*.txt | grep -a -c 'timeout (' || true)
ev_note "procs $PROCS. The programs need one particular steal (task W must run beside task T); runs that printed a timeout instead, so did not reach the situation: $missed."
ev_note "Until this pack ran, these results were from macOS arm64 only; the Linux columns are new."
ev_finish
