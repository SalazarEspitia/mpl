#!/usr/bin/env bash
# False sharing in EBR's allocations: wall time of msvspots on pairs of builds that differ only in how EBR's
# three blocks are allocated (malloc against 128-byte aligned), alternating, medians of the right runs.
#   pair "main": upstream main against main + the change (gc-ebr-align). msvspots crashes in many runs on
#                main (issue #260), so this pair may have few right runs to time.
#   pair "weak": the weak-cells branch one commit before the change against the branch's tip, which has it.
#                msvspots runs right on both.
# Usage: ./run.sh    Settings: RUNS (5 per build and row), PROCS ("2 3 4", up to the machine's cores), PAIRS
# ("main weak"), MPL_MAIN, MPL_EBR, MPL_WEAK0, MPL_WEAK (existing compilers), and those of common/lib.sh.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../common/lib.sh"
RUNS=${RUNS:-5}
if [ -z "${PROCS:-}" ]; then PROCS=""; for p in 2 3 4; do [ "$p" -le "$(ev_cores)" ] && PROCS="$PROCS $p"; done; fi

ev_init ebr-false-sharing "EBR false sharing: EBR's blocks from malloc against 128-byte aligned (wall time)"
EXPECT_weak="aligned faster. Quiet 16-core (32-thread) x86-64 Linux: 23% at procs 3, 27% at procs 4 (procs 2 not measured); macOS arm64: about 14% at procs 4"
EXPECT_main="not measured before with this program; few right runs expected (issue #260)"
for pair in ${PAIRS:-main weak}; do
  case $pair in
    main) na_=main; nb_=ebr; lab="main"; EXPECT=$EXPECT_main ;;
    weak) na_=weak0; nb_=weak; lab="weak cells"; EXPECT=$EXPECT_weak ;;
    *) ev_say "unknown pair $pair"; exit 2 ;;
  esac
  A=$(ev_mpl "$na_") || exit 1
  B=$(ev_mpl "$nb_") || exit 1
  ev_compile "$A" "msvspots.$na_" "$HERE/../stale-suspects/msvspots.mlb" || exit 1
  ev_compile "$B" "msvspots.$nb_" "$HERE/../stale-suspects/msvspots.mlb" || exit 1
  for p in $PROCS; do
    ra="$lab, malloc, procs $p"; rb="$lab, aligned, procs $p"
    for i in $(seq 1 "$RUNS"); do
      ev_run "$ra" ' bad 0$' "$EV_O/bin/msvspots.$na_" @mpl procs "$p" -- safe 2 3 0 20000 ewt
      ev_run "$rb" ' bad 0$' "$EV_O/bin/msvspots.$nb_" @mpl procs "$p" -- safe 2 3 0 20000 ewt
    done
    ma=$(ev_median "$ra"); mb=$(ev_median "$rb")
    na=$(awk -F'\t' -v r="$ra" '$1 == r && $2 == "right"' "$EV_O/runs.tsv" | grep -c . || true)
    nb=$(awk -F'\t' -v r="$rb" '$1 == r && $2 == "right"' "$EV_O/runs.tsv" | grep -c . || true)
    if [ "$ma" = "-" ] || [ "$mb" = "-" ]; then res="no right run to time (right runs: malloc $na of $RUNS, aligned $nb of $RUNS)"
    else res=$(awk -v a="$ma" -v b="$mb" -v na="$na" -v nb="$nb" -v n="$RUNS" 'BEGIN {
        d = (a - b) * 100 / a
        printf "malloc %.2f s, aligned %.2f s: aligned %.0f%% %s (right runs: %d and %d of %d)", a / 1000, b / 1000, (d < 0 ? -d : d), (d < 0 ? "slower" : "faster"), na, nb, n }')
    fi
    ev_row_text "$lab, procs $p" "$res" "$EXPECT"
  done
done
ev_note "Program: \`msvspots safe 2 3 0 20000 ewt\`, $RUNS runs per build and row, alternating; medians of wall time."
ev_note "Timing on a shared CI runner is noisy: read these numbers as an indication, and run it on a quiet machine with at least as many cores as processors for a measurement."
ev_finish
