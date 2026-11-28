#!/bin/sh
# fetch-rune.sh - clone the Rune UI library at its pinned revision into rune/vendor/rune.
#
# Rune (https://github.com/anomalyco/rune, MIT) is fetched by SHA, never vendored: rune/vendor/
# is git-ignored and rune/app/Cargo.toml reaches the library through path dependencies into it.
# This script is the single place the pin lives; hosted CI (.github/workflows/rune.yml) and
# developers both run it.
#
#   sh rune/fetch-rune.sh              # clone (about 180 MB, a shallow fetch of one commit)
#   sh rune/fetch-rune.sh --print-rev  # print the pinned revision and exit
#
# An existing checkout is accepted only when it is at the pinned revision with no local changes;
# anything else is an error, and the checkout is left as it is for a person to look at.
set -eu

RUNE_REV=b53fe7236ab11225108f6aee837c2d9942f63991
RUNE_REPO=https://github.com/anomalyco/rune.git

if [ "${1:-}" = "--print-rev" ]; then
	echo "$RUNE_REV"
	exit 0
fi
if [ $# -gt 0 ]; then
	echo "usage: sh $0 [--print-rev]" >&2
	exit 2
fi

here=$(cd "$(dirname "$0")" && pwd)
dest="$here/vendor/rune"

if [ -e "$dest" ]; then
	have=$(git -C "$dest" rev-parse HEAD 2>/dev/null || echo "not a git checkout")
	if [ "$have" != "$RUNE_REV" ]; then
		echo "error: $dest is at '$have', not the pinned $RUNE_REV; move it away and re-run" >&2
		exit 1
	fi
	if [ -n "$(git -C "$dest" status --porcelain)" ]; then
		echo "error: $dest is at the pinned revision but has local changes (Rune is never patched here)" >&2
		exit 1
	fi
	echo "rune: already at $RUNE_REV ($dest)"
	exit 0
fi

mkdir -p "$here/vendor"
partial="$here/vendor/.rune-partial-$$"
git init -q "$partial"
git -C "$partial" remote add origin "$RUNE_REPO"
git -C "$partial" fetch -q --depth 1 origin "$RUNE_REV"
git -C "$partial" checkout -q --detach FETCH_HEAD
got=$(git -C "$partial" rev-parse HEAD)
if [ "$got" != "$RUNE_REV" ]; then
	echo "error: fetched $got, expected $RUNE_REV ($partial left for inspection)" >&2
	exit 1
fi
mv "$partial" "$dest"
git -C "$dest" log -1 --format='rune %H %cI'
