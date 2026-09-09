#!/usr/bin/env bash
# file-length-gate.sh — per-file length ratchet for the LMMS standards fork.
#
# Source: the adopted code-quality ruleset (KB `adopted-code-quality-gates`):
#   "Per-file: <= 500 lines default (generated tables/fixtures exempt via inline pragma)."
# QA-GATES.md's six gates did not cover it; this closes that gap without ordering a
# rewrite of existing code (the ruleset is enforced as a ratchet — "no retroactive
# rewrite, no new violations").
#
# Policy:
#   - a fork-NEW source already over LIMIT is grandfathered in tests/file-length-baseline.tsv;
#   - a NEW file over LIMIT fails;
#   - a grandfathered file that GROWS by more than TOLERANCE lines fails;
#   - a file that shrinks updates the baseline down (the ratchet only moves one way).
#
# Exemptions: add the repo-relative path to tests/file-length-exempt.txt with a reason
# (generated tables/fixtures, vendored data). Exempt files are not measured.
#
# Usage: bash tests/file-length-gate.sh [--check]
#   default: refresh the baseline (fail on regressions)
#   --check: report only, never write the baseline (CI mode)

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT" || exit 2

LIMIT="${FILE_LINE_LIMIT:-500}"
TOLERANCE="${FILE_LINE_TOLERANCE:-0}"
BASELINE="$HERE/file-length-baseline.tsv"
EXEMPT="$HERE/file-length-exempt.txt"
MODE="ratchet"
[[ "${1:-}" == "--check" ]] && MODE="check"

is_exempt() {
	[[ -f "$EXEMPT" ]] || return 1
	grep -vE '^\s*(#|$)' "$EXEMPT" | cut -f1 | grep -qxF "$1"
}

current="$(mktemp)"
while read -r f; do
	[[ -n "$f" ]] || continue
	[[ -f "$f" ]] || continue
	is_exempt "$f" && continue
	printf '%s\t%s\n' "$f" "$(wc -l < "$f")"
done < <(grep -vE '^\s*(#|$)' "$HERE/fork-sources.txt" | sort) > "$current"

over=$(awk -v lim="$LIMIT" -F'\t' '$2 > lim' "$current" | wc -l)
total=$(wc -l < "$current")
echo "file-length-gate: ${total} fork sources measured; ${over} exceed ${LIMIT} lines"

if [[ "$over" -gt 0 ]]; then
	echo
	printf '  %-6s %s\n' "lines" "file"
	awk -v lim="$LIMIT" -F'\t' '$2 > lim {printf "  %-6s %s\n", $2, $1}' "$current" | sort -rn | head -20
fi
echo

rc=0
if [[ "$MODE" == "check" ]]; then
	echo "PASS (check mode: baseline not written)"
	rm -f "$current"; exit 0
fi

if [[ ! -f "$BASELINE" ]]; then
	{ echo "# per-file line-count baseline (files over ${LIMIT} lines only)"; \
	  echo "# maintained by tests/file-length-gate.sh; do not edit by hand"; \
	  awk -v lim="$LIMIT" -F'\t' '$2 > lim' "$current" | sort -k2,2nr; } > "$BASELINE"
	echo "PASS (baseline initialised)"
	rm -f "$current"; exit 0
fi

declare -A base
while IFS=$'\t' read -r f n; do
	[[ "$f" =~ ^# ]] && continue
	base["$f"]="$n"
done < "$BASELINE"

regressed=0
while IFS=$'\t' read -r f n; do
	[[ "$n" -gt "$LIMIT" ]] || continue
	if [[ -z "${base[$f]:-}" ]]; then
		echo "REGRESSION: new file over ${LIMIT} lines: $f ($n)"; regressed=1
	elif [[ "$n" -gt $(( base[$f] + TOLERANCE )) ]]; then
		echo "REGRESSION: $f grew ${base[$f]} -> $n lines"; regressed=1
	fi
done < "$current"

# files that dropped to or below the limit leave the baseline (ratchet down)
while IFS=$'	' read -r f n; do
	[[ "$f" =~ ^# ]] && continue
	cur=$(awk -F'	' -v p="$f" '$1==p {print $2}' "$current")
	if [[ -z "$cur" ]]; then
		echo "improved: $f is no longer a fork source - dropped from the baseline"
	elif [[ "$cur" -le "$LIMIT" ]]; then
		echo "improved: $f is now at or below ${LIMIT} lines"
	fi
done < "$BASELINE"

if [[ "$regressed" -eq 1 ]]; then
	echo "FAIL: file-length ratchet regressed (baseline unchanged)"; rc=1
else
	{ echo "# per-file line-count baseline (files over ${LIMIT} lines only)"; \
	  echo "# maintained by tests/file-length-gate.sh; do not edit by hand"; \
	  awk -v lim="$LIMIT" -F'\t' '$2 > lim' "$current" | sort -k2,2nr; } > "$BASELINE"
	echo "PASS (ratchet clean; baseline refreshed)"
fi
rm -f "$current"
exit $rc
