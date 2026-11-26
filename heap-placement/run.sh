#!/usr/bin/env bash
# Where the heap sits decides whether the stale-entry bug shows: msvspots on the base plus a measurement-only
# MPL_HEAP_HINT, with the heap at its default place, in a window where bit 30 of every address is clear, and in
# one where it is set, alternating.
# Usage: ./run.sh    Settings: RUNS (15 per row), PROCS (4; 3 under 4 cores), MPL or MPL_HINT (an existing
# compiler built from the heapaddr-hint branch), and those of common/lib.sh.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../common/lib.sh"
RUNS=${RUNS:-15}
if [ "$(ev_cores)" -ge 4 ]; then PROCS=${PROCS:-4}; else PROCS=${PROCS:-3}; fi
CLEAR=0x300000000000
SET=0x300040000000

ev_init heap-placement "Heap placement: the same build with bit 30 of the heap's addresses clear or set"
HINT=$(ev_mpl hint single) || exit 1
ev_compile "$HINT" msvspots.hint "$HERE/../stale-suspects/msvspots.mlb" || exit 1

R_DEF="default placement"
R_CLR="bit 30 clear (MPL_HEAP_HINT=$CLEAR)"
R_SET="bit 30 set (MPL_HEAP_HINT=$SET)"
for i in $(seq 1 "$RUNS"); do
  EV_ENV="" ev_run "$R_DEF" ' bad 0$' "$EV_O/bin/msvspots.hint" @mpl procs "$PROCS" -- safe 2 3 0 20000 ewt
  EV_ENV="MPL_HEAP_HINT=$CLEAR" ev_run "$R_CLR" ' bad 0$' "$EV_O/bin/msvspots.hint" @mpl procs "$PROCS" -- safe 2 3 0 20000 ewt
  EV_ENV="MPL_HEAP_HINT=$SET" ev_run "$R_SET" ' bad 0$' "$EV_O/bin/msvspots.hint" @mpl procs "$PROCS" -- safe 2 3 0 20000 ewt
done
EV_ENV=""
ev_row "$R_DEF" "Linux: in between (24 of 50 right on our 16-core (32-thread) x86-64 machine); macOS: right (5 of 5)"
ev_row "$R_CLR" "all right (Linux 50 of 50, macOS 5 of 5)"
ev_row "$R_SET" "crashes in most runs (Linux 4 of 50 right, macOS 2 of 5)"
ignored=$(cat "$EV_O"/runs/*.txt | grep -a -c 'heap-hint: \(HINT IGNORED\|WINDOW FULL\)' || true)
ev_note "Program: \`msvspots safe 2 3 0 20000 ewt\`, procs $PROCS, $RUNS runs per row, alternating. One build; only the environment variable differs."
ev_note "Runs in which the kernel did not honour the window (\"HINT IGNORED\" or \"WINDOW FULL\" on stderr): $ignored."
ev_finish
