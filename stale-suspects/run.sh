#!/usr/bin/env bash
# Stale suspect entries (issues #260 and #261): msvspots on the base and on weak cells, alternating.
# Usage: ./run.sh    Settings: RUNS (10 per row), PROCS ("2 4"; "2 3" under 4 cores), MPL_BASE, MPL_WEAK
# (existing compilers; otherwise common/build.sh builds them), and those of common/lib.sh.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../common/lib.sh"
RUNS=${RUNS:-10}
if [ "$(ev_cores)" -ge 4 ]; then PROCS=${PROCS:-2 4}; else PROCS=${PROCS:-2 3}; fi
FREQ="cc-seq-threshold-ratio 1.1 cc-threshold-ratio 1.1 min-cc-size 64K collection-threshold-ratio 1.5 min-collection-size 64K"

ev_init stale-suspects "Stale suspect entries (issues #260, #261): the base against weak cells"
BASE=$(ev_mpl base) || exit 1
WEAK=$(ev_mpl weak) || exit 1
ev_compile "$BASE" msvspots.base "$HERE/msvspots.mlb" || exit 1
ev_compile "$WEAK" msvspots.weak "$HERE/msvspots.mlb" || exit 1

# cells: "label|procs|runtime options"
CELLS=()
for p in $PROCS; do CELLS+=("procs $p|$p|"); last=$p; done
CELLS+=("procs $last, frequent collections|$last|$FREQ")

for c in "${CELLS[@]}"; do
  IFS='|' read -r lab p rts <<< "$c"
  for i in $(seq 1 "$RUNS"); do
    for b in base weak; do
      # shellcheck disable=SC2086
      ev_run "$b, $lab" ' bad 0$' "$EV_O/bin/msvspots.$b" @mpl procs "$p" $rts -- safe 2 3 0 20000 ewt
    done
  done
  ev_row "base, $lab" "Linux: crashes in most runs (32 of 80 right on our 16-core (32-thread) x86-64 machine); macOS: mostly right"
  ev_row "weak, $lab" "all right (80 of 80 on that machine)"
done
ev_note "Program: \`msvspots safe 2 3 0 20000 ewt\`, $RUNS runs per row, the two builds alternating; \"frequent collections\" is \`@mpl $FREQ\`."
ev_note "A run is right when it exits with 0 and prints \`bad 0\`. The base's crashes land in several places (the suspect clear, the concurrent collector reading a changed pointer, program code): one cause, see the README."
ev_finish
