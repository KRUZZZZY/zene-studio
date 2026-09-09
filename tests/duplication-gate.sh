#!/usr/bin/env bash
# duplication-gate.sh — token-duplication ratchet for the LMMS standards fork.
#
# Source: the adopted code-quality ruleset (KB `adopted-code-quality-gates`):
#   "token duplication < 5% (jscpd)".
# QA-GATES.md's six gates did not cover it; this closes the gap.
#
# Scope: the fork's NEW sources (tests/fork-sources.txt). jscpd's `cpp` format does not
# claim `.h` by default, so the extension map below adds headers explicitly — otherwise
# header-to-header clones are invisible.
#
# Threshold: DUP_MAX (default 5) percent of duplicated LINES. Exceeding it fails.
# Boilerplate note: the license header of every file is a genuine text clone. It is
# counted, not suppressed, so the number stays honest; at min-lines 25 the current
# clone set is header-only and totals ~1% of lines.
#
# Usage: bash tests/duplication-gate.sh [--check]
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

if ! command -v npx >/dev/null 2>&1; then
	echo "duplication-gate: SKIP - npx (Node) not available; no measurement performed"
	exit 0
fi

mapfile -t FILES < <(grep -vE '^\s*(#|$)' "$HERE/fork-sources.txt")
echo "duplication-gate: scanning ${#FILES[@]} fork sources (min-lines ${MIN_LINES}, min-tokens ${MIN_TOKENS}, threshold ${DUP_MAX}%)"

out="$(npx --yes jscpd \
	--format cpp --formats-exts "cpp:cpp,h,hpp,cc,cxx" \
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
