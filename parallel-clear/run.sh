#!/usr/bin/env bash
# The parallel suspect clear against a local collection: t3 on the base and on weak cells, alternating.
# Usage: ./run.sh    Settings: RUNS (5 per row), MPL_BASE, MPL_WEAK, and those of common/lib.sh.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../common/lib.sh"
RUNS=${RUNS:-5}
P=4; [ "$(ev_cores)" -ge 4 ] || P=3      # the 4-processor rows use 3 on a 3-core machine
R2="collection-threshold-ratio 2.0 min-collection-size 64K"

ev_init parallel-clear "The parallel suspect clear against a local collection: the base against weak cells"
BASE=$(ev_mpl base) || exit 1
WEAK=$(ev_mpl weak) || exit 1
ev_compile "$BASE" t3.base "$HERE/t3.mlb" || exit 1
ev_compile "$WEAK" t3.weak "$HERE/t3.mlb" || exit 1

# cells: "label|procs|runtime options|program arguments"
CELLS=(
  "plain, procs 1, early collections|1|$R2|plain 12000 1000 2000 1000 left"
  "plain, procs 2|2||plain 12000 1000 2000 1000 left"
  "plain, procs $P|$P||plain 12000 1000 2000 1000 left"
  "half, procs 1, early collections|1|$R2|half 12000 1000 2000 1000 left"
  "half, procs 2|2||half 12000 1000 2000 1000 left"
  "tree, procs $P|$P||plain 200000 40 100000 1000 tree"
  "big, procs 2|2||big 10500 30 2000 1000 left"
)
for c in "${CELLS[@]}"; do
  IFS='|' read -r lab p rts args <<< "$c"
  for i in $(seq 1 "$RUNS"); do
    for b in base weak; do
      # shellcheck disable=SC2086
      ev_run "$b, $lab" ' bad 0$' "$EV_O/bin/t3.$b" @mpl procs "$p" $rts gc-summary -- $args
    done
  done
  ev_row "base, $lab" "mostly right; on Linux some crashes in the plain rows with 2 or more processors (2 of 6 on our 32-core machine)"
  ev_row "weak, $lab" "all right"
done

# weak cells count the entries they refuse to use; the parallel clear's are the stale ones of this finding
count() { cat "$EV_O"/runs/weak*.txt 2>/dev/null | grep -a "^suspect entries dropped $1" \
  | sed -E 's/.*parallel clear ([0-9]+).*/\1/' | awk '{ s += $1 } END { print s + 0 }'; }
ev_row_text "weak: stale entries its parallel clear met and dropped, all runs" \
  "$(count dead) pointing into freed chunks, $(count forwarded) at forwarded objects" \
  "above 0: each is a write the base makes into freed or moved memory (about 200,000 in one plain run at 2 processors)"
ev_note "Program: \`t3 VARIANT N ROUNDS POST GRAIN SHAPE\` (see t3.sml), $RUNS runs per row, the two builds alternating; \"early collections\" is \`@mpl $R2\`."
ev_note "The base's stray writes mostly land where nothing shows, so its rows are mostly right; the last row counts, in weak cells' own statistics (\`@mpl gc-summary\`), how often the situation arose."
ev_finish
