#!/usr/bin/env bash
# Put the platforms side by side: one table per finding, one column per platform.
# Usage: common/combine.sh DIR [OUT.md]
#   DIR  holds each platform's results.tsv files, anywhere below it (the CI's downloaded artifacts, or
#        the pack itself after local runs). Prints the tables, writes OUT.md (default DIR/combined.md),
#        and appends to $GITHUB_STEP_SUMMARY on CI.
set -u
DIR=${1:?usage: common/combine.sh DIR [OUT.md]}
OUT=${2:-$DIR/combined.md}
FINDINGS="stale-suspects parallel-clear heap-placement ebr-false-sharing write-barrier-depth"
title() {
  case $1 in
    stale-suspects) echo "Stale suspect entries (issues #260, #261): the base against weak cells" ;;
    parallel-clear) echo "The parallel suspect clear against a local collection" ;;
    heap-placement) echo "Heap placement: bit 30 of the heap's addresses clear or set" ;;
    ebr-false-sharing) echo "EBR false sharing: EBR's blocks from malloc against 128-byte aligned (wall time; indicative on CI)" ;;
    write-barrier-depth) echo "Write barrier: an unpin depth deeper than the writing thread (upstream main)" ;;
  esac
}
ALL=$(mktemp)
find "$DIR" -name results.tsv -print0 | xargs -0 cat > "$ALL" 2>/dev/null
{
  echo "## Results by platform"
  echo
  if [ ! -s "$ALL" ]; then echo "No results found under $DIR."; fi
  for f in $FINDINGS; do
    awk -F'\t' -v f="$f" '$1 == f' "$ALL" | grep -q . || continue
    echo "### $(title "$f")"
    echo
    awk -F'\t' -v f="$f" '
      $1 == f {
        if (!($2 in seenr)) { seenr[$2] = 1; rows[++nr] = $2 }
        if (!($3 in seenp)) { seenp[$3] = 1; plats[++np] = $3 }
        cell[$2, $3] = $4 ($5 != "-" && $5 != "" ? "; median " $5 : "")
        if ($6 != "") expct[$2] = $6
      }
      END {
        # platforms in a fixed order: Linux x86-64, Linux Arm, macOS, then any other
        n = 0
        split("linux-x86_64 linux-aarch64 darwin-arm64", pref, " ")
        for (i = 1; i <= 3; i++) if (pref[i] in seenp) { ord[++n] = pref[i]; done[pref[i]] = 1 }
        for (i = 1; i <= np; i++) if (!(plats[i] in done)) ord[++n] = plats[i]
        printf "| Run |"; for (i = 1; i <= n; i++) printf " `%s` |", ord[i]; print " Expected |"
        printf "|---|"; for (i = 1; i <= n; i++) printf "---|"; print "---|"
        for (r = 1; r <= nr; r++) {
          printf "| %s |", rows[r]
          for (i = 1; i <= n; i++) printf " %s |", ((rows[r], ord[i]) in cell ? cell[rows[r], ord[i]] : "-")
          printf " %s |\n", expct[rows[r]]
        }
        print ""
      }' "$ALL"
  done
  echo "A \"-\" means that platform did not run that row (the processor counts follow the machine's cores) or its job failed."
} > "$OUT"
rm -f "$ALL"
cat "$OUT"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then cat "$OUT" >> "$GITHUB_STEP_SUMMARY"; fi
