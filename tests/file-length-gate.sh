#!/usr/bin/env bash
# file-length-gate.sh — Gate 7: per-file length ratchet.
#
# Source: the adopted code-quality ruleset (KB `adopted-code-quality-gates`):
#   "Per-file: <= 500 lines default (generated tables/fixtures exempt via inline pragma)."
# Enforced as a ratchet — "no retroactive rewrite, no new violations".
#
# Policy:
#   - a fork-NEW source already over LIMIT is grandfathered in tests/file-length-baseline.tsv;
#   - a NEW file over LIMIT fails;
#   - a grandfathered file that GROWS by more than TOLERANCE lines fails;
#   - a file that shrinks updates the baseline down (the ratchet only moves one way).
#
# Exemptions: add the repo-relative path to tests/file-length-exempt.txt with a
# reason, tab-separated (generated tables/fixtures, vendored data). Exempt files
# are not measured in any scope. A blank reason exits 2 - an exemption without a
# stated reason is not honoured, the same fail-closed rule the divergence ledger
# and the coverage entry floor follow.
#
# Usage:
#   bash tests/file-length-gate.sh                        # ratchet: refresh the baseline, fail on regressions
#   bash tests/file-length-gate.sh --check                # CI: never writes the baseline, STILL fails on regressions
#   bash tests/file-length-gate.sh --reanchor "reason"    # deliberate, recorded baseline refresh
#   bash tests/file-length-gate.sh --scope all            # whole tree (every source in tests/all-sources.txt)
#   bash tests/file-length-gate.sh --scope tools          # the fork's own tooling under tools/ (own baseline)
#
# NOTE (2026-09-11): `--check` used to print PASS unconditionally, so this ratchet could
# not fail in CI even when red locally. It now performs the same comparison and exits 1 on
# a regression; "report only" means "never write the baseline".

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT" || exit 2

LIMIT="${FILE_LINE_LIMIT:-500}"
TOLERANCE="${FILE_LINE_TOLERANCE:-0}"
BASELINE="$HERE/file-length-baseline.tsv"
EXEMPT="$HERE/file-length-exempt.txt"

MODE="ratchet"
REASON=""
SCOPE="${GATE_SCOPE:-fork}"
while [[ $# -gt 0 ]]; do
	case "$1" in
		--check)    MODE="check"; shift ;;
		--reanchor) MODE="reanchor"; REASON="${2:-}"; shift 2 ;;
		--scope)    SCOPE="${2:-}"; shift 2 ;;
		*) echo "usage: $0 [--check|--reanchor \"reason\"] [--scope fork|all]" >&2; exit 2 ;;
	esac
done
case "$SCOPE" in
	fork) SOURCES="$HERE/fork-sources.txt" ;;
	all)  SOURCES="$HERE/all-sources.txt"; BASELINE="$HERE/file-length-baseline-all.tsv"
	      echo "file-length-gate: whole-tree scope (every source in tests/all-sources.txt; upstream files are grandfathered, see docs/CONVENTIONS.md)" ;;
	tools) SOURCES="$HERE/tools-sources.txt"; BASELINE="$HERE/file-length-baseline-tools.tsv"
	      echo "file-length-gate: tools scope (fork-owned developer tooling; its own baseline, separate from the product ratchets)" ;;
	*) echo "unknown scope '$SCOPE' (use fork|all|tools)" >&2; exit 2 ;;
esac
if [[ "$MODE" == "reanchor" && -z "${REASON// /}" ]]; then
	echo "usage: $0 --reanchor \"reason\" — an unrecorded re-anchor is not allowed" >&2
	exit 2
fi

is_exempt() {
	[[ -f "$EXEMPT" ]] || return 1
	grep -vE '^\s*(#|$)' "$EXEMPT" | cut -f1 | grep -qxF "$1"
}

# Exemptions are fail-closed: a path without a stated reason is refused rather
# than honoured, so the file cannot become a silent mute button for the ratchet.
if [[ -f "$EXEMPT" ]]; then
	while IFS=$'	' read -r ex_path ex_why; do
		[[ "$ex_path" =~ ^[[:space:]]*# ]] && continue
		[[ -z "${ex_path// /}" ]] && continue
		if [[ -z "${ex_why// /}" ]]; then
			echo "exempt error: tests/file-length-exempt.txt entry '$ex_path' has no reason" >&2
			echo "an exemption must say why the file should not be measured (this gate refuses a blank one)" >&2
			exit 2
		fi
	done < "$EXEMPT"
fi

current="$(mktemp)"
while read -r f; do
	[[ -n "$f" ]] || continue
	[[ -f "$f" ]] || continue
	is_exempt "$f" && continue
	printf '%s\t%s\n' "$f" "$(wc -l < "$f")"
done < <(grep -vE '^\s*(#|$)' "$SOURCES" | sort) > "$current"

over=$(awk -v lim="$LIMIT" -F'\t' '$2 > lim' "$current" | wc -l)
total=$(wc -l < "$current")
echo "file-length-gate: ${total} ${SCOPE}-scope sources measured; ${over} exceed ${LIMIT} lines"

if [[ "$over" -gt 0 ]]; then
	echo
	printf '  %-6s %s\n' "lines" "file"
	awk -v lim="$LIMIT" -F'\t' '$2 > lim {printf "  %-6s %s\n", $2, $1}' "$current" | sort -rn | head -20
fi
echo

write_baseline() {
	{ echo "# per-file line-count baseline (files over ${LIMIT} lines only)";
	  echo "# maintained by tests/file-length-gate.sh; do not edit by hand";
	  awk -v lim="$LIMIT" -F'\t' '$2 > lim' "$current" | sort -k2,2nr; } > "$BASELINE"
}

# --- compare against the baseline --------------------------------------------
regressed=0
if [[ ! -f "$BASELINE" ]]; then
	if [[ "$MODE" == "check" ]]; then
		echo "FAIL: no baseline at ${BASELINE#"$ROOT"/} (check mode: nothing written)"; exit 1
	fi
	write_baseline
	echo "PASS (baseline initialised)"
	rm -f "$current"; exit 0
fi

declare -A base
while IFS=$'\t' read -r f n; do
	[[ "$f" =~ ^# ]] && continue
	[[ -z "${f// /}" ]] && continue
	base["$f"]="$n"
done < "$BASELINE"

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
	[[ -z "${f// /}" ]] && continue
	if is_exempt "$f"; then
		echo "improved: $f is exempt (tests/file-length-exempt.txt) - not measured, dropped from the baseline"
		continue
	fi
	cur=$(awk -F'	' -v p="$f" '$1==p {print $2}' "$current")
	if [[ -z "$cur" ]]; then
		echo "improved: $f is no longer a fork source - dropped from the baseline"
	elif [[ "$cur" -le "$LIMIT" ]]; then
		echo "improved: $f is now at or below ${LIMIT} lines"
	fi
done < "$BASELINE"

rc=0
case "$MODE" in
reanchor)
	write_baseline
	echo "RE-ANCHORED: baseline rewritten from the current tree (${over} file(s) over ${LIMIT} lines)"
	echo "reason: ${REASON}"
	;;
check)
	if [[ "$regressed" -eq 1 ]]; then
		echo "FAIL: file-length ratchet regressed (check mode: baseline not written)"; rc=1
	else
		echo "PASS (check mode: no regressions; baseline not written)"
	fi
	;;
ratchet)
	if [[ "$regressed" -eq 1 ]]; then
		echo "FAIL: file-length ratchet regressed (baseline unchanged)"; rc=1
	else
		write_baseline
		echo "PASS (ratchet clean; baseline refreshed)"
	fi
	;;
esac
rm -f "$current"
exit $rc
