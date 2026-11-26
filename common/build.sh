#!/usr/bin/env bash
# Build MPL at one commit of the fork, the way upstream's CI does (.github/workflows/ci.yml: mlton,
# libgmp-dev, gcc and make from apt, then `make --silent`). On macOS it follows upstream's README:
# `brew install mlton make gmp`, then `gmake WITH_GMP_DIR=$(brew --prefix gmp)`.
#
# Usage: common/build.sh REF OUTDIR
#   REF     a name from common/refs.sh (base, weak, hint, main, ebr), a full 40-digit commit, or a branch
#   OUTDIR  where the compiler goes: OUTDIR/bin/mpl and OUTDIR/lib (it can be moved afterwards)
#
# Cached per commit: if OUTDIR already holds a finished build of the same commit, nothing is done.
# Settings: MPL_REPO (default: the fork), JOBS (make -j; default: none, as upstream), KEEP_SRC=1 (keep
# the source tree, OUTDIR.src). The build log is OUTDIR.build.log. Needs MLton, GMP, gcc and GNU make.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/refs.sh"
REF=${1:?usage: common/build.sh REF OUTDIR}
OUT=${2:?usage: common/build.sh REF OUTDIR}

commit=$(ref_commit "$REF")
if [ -z "$commit" ]; then
  case $REF in
    *[!0-9a-f]*) commit="" ;;
    *) [ ${#REF} = 40 ] && commit=$REF ;;
  esac
fi
if [ -z "$commit" ]; then
  commit=$(git ls-remote "$EV_REPO" "refs/heads/$REF" | awk '{ print $1 }')
  [ -n "$commit" ] || { echo "build.sh: $REF is not a name in refs.sh, a full commit, or a branch of $EV_REPO" >&2; exit 2; }
fi

if [ -x "$OUT/bin/mpl" ] && [ "$(cat "$OUT/.commit" 2>/dev/null)" = "$commit" ]; then
  echo "build.sh: $REF ($commit) is already built in $OUT"
  exit 0
fi

MAKE=make
MAKEVARS=()
if [ "$(uname -s)" = Darwin ]; then
  command -v gmake >/dev/null || { echo "build.sh: GNU make (gmake) not found: brew install mlton make gmp" >&2; exit 2; }
  MAKE=gmake
  MAKEVARS=("WITH_GMP_DIR=${GMP:-$(brew --prefix gmp)}")
fi
command -v mlton >/dev/null || { echo "build.sh: mlton not found (Linux: apt-get install mlton libgmp-dev gcc make; macOS: brew install mlton make gmp)" >&2; exit 2; }

mkdir -p "$(dirname "$OUT")"
OUT=$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")
SRC=$OUT.src
LOG=$OUT.build.log
rm -rf "$SRC" "$OUT"
mkdir -p "$SRC"
echo "build.sh: fetching $commit from $EV_REPO"
git init -q "$SRC"
git -C "$SRC" fetch -q --depth 1 "$EV_REPO" "$commit"
git -C "$SRC" checkout -q FETCH_HEAD
[ "$(git -C "$SRC" rev-parse HEAD)" = "$commit" ] || { echo "build.sh: fetched the wrong commit" >&2; exit 1; }

echo "build.sh: building (log: $LOG)"
t0=$(date +%s)
if ! (cd "$SRC" && "$MAKE" --silent ${JOBS:+-j"$JOBS"} ${MAKEVARS[@]+"${MAKEVARS[@]}"}) > "$LOG" 2>&1; then
  echo "build.sh: the build failed; the end of $LOG:" >&2
  tail -40 "$LOG" >&2
  exit 1
fi
[ -x "$SRC/build/bin/mpl" ] || { echo "build.sh: no build/bin/mpl after make" >&2; exit 1; }
mkdir -p "$OUT"
cp -R "$SRC/build/." "$OUT/"
echo "$commit" > "$OUT/.commit"
[ -n "${KEEP_SRC:-}" ] || rm -rf "$SRC"
echo "build.sh: built $REF ($commit) in $(( $(date +%s) - t0 )) s: $OUT/bin/mpl"
