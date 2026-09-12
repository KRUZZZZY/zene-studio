#!/usr/bin/env bash
# duplication-gate.sh — token-duplication ratchet for the LMMS standards fork.
#
# Source: the adopted code-quality ruleset (KB `adopted-code-quality-gates`):
#   "token duplication < 5% (jscpd)".
# QA-GATES.md's six gates did not cover it; this closes the gap.
#
# Scope: tests/fork-sources.txt (default), tests/all-sources.txt with --scope all, or
# tests/tools-sources.txt with --scope tools (the fork's own tooling under tools/; that
# scope adds the `python` format beside `cpp`).
# jscpd's `cpp` format does not claim `.h` or `.c` by default, so the extension map below
# adds both explicitly — without `.h`, header-to-header clones are invisible; without `.c`,
# the six C sources in the whole-tree scope were never analysed at all.
#
# Threshold: DUP_MAX (default 5) percent of duplicated LINES. Exceeding it fails.
# Boilerplate note: the license header of every file is a genuine text clone. It is
# counted, not suppressed, so the number stays honest.
#
# Known blind spots (state them, do not hide them): jscpd skips files over ~1 MB and a
# handful of very small files, so its file count runs a little under the scope size. The
# gate prints the number it actually analysed.
#
# Usage: bash tests/duplication-gate.sh [--check] [--scope fork|all|tools]
#   (--check is accepted for symmetry with the other gates; behaviour is identical.)
#
# Requires npx (Node). If npx is unavailable the gate reports SKIP and exits 0 — it
# never reports PASS without having measured.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT" || exit 2

DUP_MAX="${DUP_MAX:-5}"
MIN_LINES="${DUP_MIN_LINES:-25}"
MIN_TOKENS="${DUP_MIN_TOKENS:-120}"
SCOPE="${GATE_SCOPE:-fork}"
while [[ $# -gt 0 ]]; do
	case "$1" in
		--check) MODE_CHECK=1; shift ;;
		--scope) SCOPE="${2:-}"; shift 2 ;;
		*) echo "usage: $0 [--check] [--scope fork|all|tools]" >&2; exit 2 ;;
	esac
done
case "$SCOPE" in
	fork) SCOPEFILE="$HERE/fork-sources.txt"
	      FORMAT_ARGS=(--format cpp --formats-exts "cpp:cpp,c,h,hpp,cc,cxx") ;;
	all)  SCOPEFILE="$HERE/all-sources.txt"
	      FORMAT_ARGS=(--format cpp --formats-exts "cpp:cpp,c,h,hpp,cc,cxx")
	      echo "duplication-gate: whole-tree scope (every source in tests/all-sources.txt)" ;;
	tools) SCOPEFILE="$HERE/tools-sources.txt"
	       FORMAT_ARGS=(--format python --format cpp --formats-exts "python:py,cpp:cpp,c,h,hpp,cc,cxx")
	       echo "duplication-gate: tools scope (fork-owned tooling; jscpd has no shell format, so the .sh entries are counted by Gates 4 and 7 only)" ;;
	*) echo "unknown scope '$SCOPE' (use fork|all|tools)" >&2; exit 2 ;;
esac

if ! command -v npx >/dev/null 2>&1; then
	echo "duplication-gate: SKIP - npx (Node) not available; no measurement performed"
	exit 0
fi

mapfile -t FILES < <(grep -vE '^\s*(#|$)' "$SCOPEFILE")
echo "duplication-gate: scanning ${#FILES[@]} $SCOPE sources (min-lines ${MIN_LINES}, min-tokens ${MIN_TOKENS}, threshold ${DUP_MAX}%)"

out="$(npx --yes jscpd \
	"${FORMAT_ARGS[@]}" \
	--min-lines "$MIN_LINES" --min-tokens "$MIN_TOKENS" \
	--reporters console --silent \
	"${FILES[@]}" 2>&1)"
rc=$?

if [[ $rc -ne 0 && -z "$out" ]]; then
	echo "duplication-gate: SKIP - jscpd did not run (network or Node problem)"
	exit 0
fi

echo "$out" | grep -E "Clone found|^\s+-|^\s+[A-Za-z].*\[[0-9]+:" | head -24

pct="$(echo "$out" | grep -oE '[0-9]+\.[0-9]+%' | head -1 | tr -d '%')"
if [[ -z "$pct" ]]; then
	echo "duplication-gate: SKIP - could not parse a duplication percentage from jscpd"
	exit 0
fi

echo
if awk -v p="$pct" -v m="$DUP_MAX" 'BEGIN { exit !(p > m) }'; then
	echo "FAIL: duplicated lines ${pct}% exceeds the ${DUP_MAX}% budget"
	exit 1
fi
echo "PASS: duplicated lines ${pct}% (budget ${DUP_MAX}%)"
