# Sourced by each finding's run.sh (bash 3.2 or later; Linux and macOS).
#
#   ev_init FINDING "TITLE"        start: output folder, platform, empty tables
#   ev_mpl NAME [single]           print the path of the compiler for build NAME (common/refs.sh): $MPL_<NAME>
#                                  if set (MPL_BASE, MPL_WEAK, ...), else $MPL when the finding uses one build
#                                  ("single"), else common/build.sh builds it (cached)
#   ev_compile MPL OUT MLB [FLAG...]   compile a program (with segvbt.c, for a backtrace on SIGSEGV/SIGBUS)
#   ev_run ROW OK BIN [ARG...]     run once under a time and memory limit and classify the run:
#                                    right    exit 0 and the output matches the regular expression OK
#                                    wrong    exit 0 without it
#                                    crashed  a signal or another exit code (with the signal, and the faulting
#                                             frames or the failed assertion when the output has them)
#                                    hung     still running at the time limit (EV_TLIMIT, 120 s), then killed
#                                  Extra environment for the program: EV_ENV="NAME=value ...".
#   ev_row ROW "EXPECTED"          add ROW's tally to the table
#   ev_note "TEXT"                 a line under the table
#   ev_finish                      write OUT/results.md and OUT/results.tsv, print the table, and append it to
#                                  $GITHUB_STEP_SUMMARY on CI
#
# Settings: EV_OUT (output folder; default FINDING/out), EV_TLIMIT (s per run), EV_MEM_MB (limit on resident
# memory per run, default 5000), EV_PLATFORM (the platform's label), EV_BUILDS (where build.sh puts compilers;
# default .builds/ at the top of the pack).

EV_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
. "$EV_ROOT/common/refs.sh"

EV_TLIMIT=${EV_TLIMIT:-120}
EV_MEM_MB=${EV_MEM_MB:-5000}
EV_ENV=${EV_ENV:-}

ev_cores() { getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu; }
ev_now_ms() {
  if [ -n "${EPOCHREALTIME:-}" ]; then local t=${EPOCHREALTIME/[.,]/}; echo $(( t / 1000 ))
  else perl -MTime::HiRes=time -e 'printf "%d\n", time * 1000'; fi
}
ev_say() { echo "$*" >&2; }

ev_init() {
  EV_FINDING=$1; EV_TITLE=$2
  EV_PLATFORM=${EV_PLATFORM:-$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)}
  EV_O=${EV_OUT:-$EV_ROOT/$EV_FINDING/out}
  rm -rf "$EV_O"; mkdir -p "$EV_O/bin" "$EV_O/runs"
  EV_O=$(cd "$EV_O" && pwd)
  : > "$EV_O/runs.tsv"; : > "$EV_O/rows.tsv"; : > "$EV_O/notes.txt"; : > "$EV_O/builds.txt"
  EV_T0=$(date +%s)
  ev_say "== $EV_FINDING on $EV_PLATFORM ($(ev_cores) cores); output in $EV_O"
}

ev_mpl() {
  local name=$1 single=${2:-} var val commit out
  var=MPL_$(echo "$name" | tr '[:lower:]' '[:upper:]')
  eval "val=\${$var:-}"
  if [ -z "$val" ] && [ "$single" = single ]; then val=${MPL:-}; fi
  commit=$(ref_commit "$name")
  if [ -n "$val" ]; then
    [ -x "$val" ] || { ev_say "ev_mpl: $val is not an executable"; return 1; }
    echo "\`$name\`: \`$(ref_branch "$name")\` ${commit:0:9}, compiler given by the caller (its commit is not checked)" >> "$EV_O/builds.txt"
    echo "$val"; return 0
  fi
  out=${EV_BUILDS:-$EV_ROOT/.builds}/$commit
  "$EV_ROOT/common/build.sh" "$name" "$out" >&2 || return 1
  echo "\`$name\`: \`$(ref_branch "$name")\` ${commit:0:9}, built by common/build.sh" >> "$EV_O/builds.txt"
  echo "$out/bin/mpl"
}

# ev_compile MPL OUT MLB [FLAG...]: MLB is a path; the binary is EV_O/bin/OUT
ev_compile() {
  local mpl=$1 out=$2 mlb=$3; shift 3
  ( cd "$(dirname "$mlb")" && "$mpl" "$@" -output "$EV_O/bin/$out" "$(basename "$mlb")" "$EV_ROOT/common/segvbt.c" ) \
    > "$EV_O/bin/$out.compile.log" 2>&1 \
    || { ev_say "compile failed: $out; the end of $EV_O/bin/$out.compile.log:"; tail -20 "$EV_O/bin/$out.compile.log" >&2; return 1; }
}

ev__tree() { local p; for p in $(pgrep -P "$1" 2>/dev/null); do echo "$p"; ev__tree "$p"; done; }
ev__rss_mb() { ps -o rss= -p "$1" 2>/dev/null | awk '{ s += $1 } END { print int(s / 1024) }'; }

# The frames of the faulting thread, innermost first, from segvbt.c's output (glibc and macOS formats).
ev__frames() {
  local out=$1 bin=$2
  awk '/^segv-bt: /{f=1; next} f' "$out" | head -14 | while IFS= read -r l; do
    local name="" off="" mod in
    case $l in
      *"("*")"*"["*)
        mod=${l%%(*}; in=${l#*(}; in=${in%%)*}
        name=${in%%+*}; off=${in#*+}; [ "$off" = "$in" ] && off=""
        if [ -z "$name" ] && [ "$mod" = "$bin" ] && command -v addr2line >/dev/null; then
          name=$(addr2line -f -e "$bin" "$off" 2>/dev/null | head -1)
        fi
        { [ -z "$name" ] || [ "$name" = "??" ]; } && name="$(basename "$mod")+$off" ;;
      *) name=$(echo "$l" | awk '{ print $4 }') ;;
    esac
    echo "$name"
  done | awk 'BEGIN { skip = 1 }
      skip && ($0 ~ /segvbtHandler|_sigtramp|__restore_rt|__kernel_rt_sigreturn|^libc\.so\.6\+|^linux-vdso|^libsystem_platform/) { next }
      { skip = 0; print }' | head -4 | tr '\n' ' ' | sed 's/ $//; s/ / < /g'
}

ev__signame() {  # SIGBUS is 7 on Linux and 10 on macOS
  local bus=7; [ "$(uname -s)" = Darwin ] && bus=10
  case $1 in
    4) echo SIGILL;; 6) echo SIGABRT;; 8) echo SIGFPE;; 9) echo SIGKILL;; 11) echo SIGSEGV;; 15) echo SIGTERM;;
    "$bus") echo SIGBUS;; *) echo "signal $1";;
  esac
}

ev_run() {
  local row=$1 ok=$2 bin=$3; shift 3
  local n out pid s0 status="" rc ms mb p fa fr as sig
  n=$(awk -F'\t' -v r="$row" '$1 == r' "$EV_O/runs.tsv" | grep -c . || true); n=$((n + 1))
  out=$EV_O/runs/$(echo "$row" | tr -c 'A-Za-z0-9._\n-' '_').$n.txt
  rm -f "$out.rc"
  # shellcheck disable=SC2086
  ( t0=$(ev_now_ms); env $EV_ENV "$bin" "$@" > "$out" 2>&1 < /dev/null; rc=$?; t1=$(ev_now_ms)
    echo "$rc $((t1 - t0))" > "$out.rc" ) 2>/dev/null &
  pid=$!; s0=$(date +%s)
  while kill -0 "$pid" 2>/dev/null; do
    if [ $(( $(date +%s) - s0 )) -ge "$EV_TLIMIT" ]; then status=hung
    else
      mb=$(ev__rss_mb "$(echo $(ev__tree "$pid") | tr ' ' ',')")
      [ "${mb:-0}" -gt "$EV_MEM_MB" ] && status=memory
    fi
    if [ -n "$status" ]; then for p in $(ev__tree "$pid"); do kill -9 "$p" 2>/dev/null; done; break; fi
    sleep 0.2
  done
  wait "$pid" 2>/dev/null
  read -r rc ms < "$out.rc" 2>/dev/null || { rc=255; ms=0; }
  rm -f "$out.rc"
  EV_DETAIL=""
  if [ "$status" = hung ]; then EV_RES=hung; EV_DETAIL="killed at ${EV_TLIMIT} s"
  elif [ "$status" = memory ]; then EV_RES=crashed; EV_DETAIL="killed over ${EV_MEM_MB} MB"
  elif [ "$rc" = 0 ] && grep -a -q -E "$ok" "$out"; then EV_RES=right
  elif [ "$rc" = 0 ]; then EV_RES=wrong; EV_DETAIL=$(tail -1 "$out" | cut -c1-100)
  else
    EV_RES=crashed
    if [ "$rc" -gt 128 ]; then sig=$(ev__signame $((rc - 128))); else sig="exit $rc"; fi
    as=$(grep -a -m1 'Assertion' "$out" | sed -E 's/^.*Assertion/Assertion/; s/, function .*//; s/[`'"'"']//g' | cut -c1-90)
    fa=$(grep -a -m1 '^segv-bt: ' "$out" | sed -E 's/.* at //')
    fr=$(ev__frames "$out" "$bin")
    if [ -n "$as" ]; then EV_DETAIL="$sig: $as"
    elif [ -n "$fr" ]; then EV_DETAIL="$sig in $fr"
    else EV_DETAIL="$sig"; fi
    [ -n "$fa" ] && echo "$row run $n: $sig, fault address $fa, frames: ${fr:-none}" >> "$EV_O/crashes.txt"
  fi
  EV_MS=$ms
  printf '%s\t%s\t%s\t%s\n' "$row" "$EV_RES" "$ms" "$EV_DETAIL" >> "$EV_O/runs.tsv"
  ev_say "  $row run $n: $EV_RES${EV_DETAIL:+ ($EV_DETAIL)}, $ms ms"
}

# ev_median ROW [RESULT]: median wall time (ms) of ROW's runs (those with RESULT, default right), or "-"
ev_median() {
  awk -F'\t' -v r="$1" -v w="${2:-right}" '$1 == r && $2 == w { print $3 }' "$EV_O/runs.tsv" | sort -n \
    | awk '{ a[NR] = $1 } END { if (NR == 0) print "-"; else if (NR % 2) print a[(NR + 1) / 2]; else print int((a[NR / 2] + a[NR / 2 + 1]) / 2) }'
}

# ev_tally ROW: "R right of N; C crashed (kind xK, ...); W wrong; H hung" (zero counts left out)
ev_tally() {
  awk -F'\t' -v r="$1" '
    $1 == r { n++; c[$2]++; if ($2 == "crashed") { k = $4; sub(/ < .*/, "", k); if (!(k in d)) order[++nk] = k; d[k]++ } }
    END {
      if (n == 0) { print "not run"; exit }
      s = (c["right"] + 0) " right of " n
      if (c["crashed"]) { s = s "; " c["crashed"] " crashed ("; for (i = 1; i <= nk; i++) s = s (i > 1 ? ", " : "") order[i] (d[order[i]] > 1 ? " x" d[order[i]] : ""); s = s ")" }
      if (c["wrong"]) s = s "; " c["wrong"] " wrong"
      if (c["hung"]) s = s "; " c["hung"] " hung"
      print s
    }' "$EV_O/runs.tsv"
}

ev_row() {
  local row=$1 expected=${2:-} med
  med=$(ev_median "$row"); [ "$med" = "-" ] || med=$(awk -v m="$med" 'BEGIN { printf "%.2f s", m / 1000 }')
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$EV_FINDING" "$row" "$EV_PLATFORM" "$(ev_tally "$row")" "$med" "$expected" >> "$EV_O/rows.tsv"
}
# ev_row_text ROW "RESULT" "EXPECTED": a row whose result the finding's script computes itself
ev_row_text() { printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$EV_FINDING" "$1" "$EV_PLATFORM" "$2" "-" "${3:-}" >> "$EV_O/rows.tsv"; }
ev_note() { echo "$*" >> "$EV_O/notes.txt"; }

ev_finish() {
  local md=$EV_O/results.md
  {
    echo "### $EV_TITLE"
    echo
    echo "Platform: \`$EV_PLATFORM\` ($(uname -srm), $(ev_cores) cores), $(date -u '+%Y-%m-%d %H:%M UTC'), $(( $(date +%s) - EV_T0 )) s."
    echo
    echo "| Run | Result | Median time of the right runs | Expected |"
    echo "|---|---|---|---|"
    awk -F'\t' '{ printf "| %s | %s | %s | %s |\n", $2, $4, $5, $6 }' "$EV_O/rows.tsv"
    echo
    if [ -s "$EV_O/notes.txt" ]; then sed 's/^/- /' "$EV_O/notes.txt"; echo; fi
    echo "Builds:"
    sort -u "$EV_O/builds.txt" | sed 's/^/- /'
    if [ -s "$EV_O/crashes.txt" ]; then
      echo
      echo "<details><summary>Crashes with a backtrace ($(grep -c . "$EV_O/crashes.txt"))</summary>"
      echo
      echo '```'
      head -60 "$EV_O/crashes.txt"
      echo '```'
      echo "</details>"
    fi
    echo
  } > "$md"
  cp "$EV_O/rows.tsv" "$EV_O/results.tsv"
  cat "$md"
  if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then cat "$md" >> "$GITHUB_STEP_SUMMARY"; fi
  rm -rf "$EV_O/bin"
  ev_say "== done: $md"
}
