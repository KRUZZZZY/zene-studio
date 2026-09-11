#!/usr/bin/env bash
# complexity-gate.sh — Gate 4 (per-method complexity).
#
# Tool: `lizard` (pip install lizard) — reports cyclomatic complexity (CCN) and nesting
# depth (ND) for C/C++. cppcheck 2.13 does not emit complexity for this codebase, so
# lizard is the tool. ND cannot be thresholded by lizard, so it is REPORTED alongside
# over-target functions and reviewed manually. Stated, not hidden.
#
# Policy (matches tests/QA-GATES.md Gate 4):
#   - target: CCN <= 10 per method
#   - this is a RATCHET, not a rewrite order: functions already over the target are
#     grandfathered in tests/complexity-baseline.tsv at their measured CCN; a NEW
#     function over the target, or an existing one whose CCN RISES, fails.
#   - the baseline is keyed by `function@path`, NOT by lizard's `function@line-line@path`
#     location. Keying on the line span meant a function that merely moved or grew
#     orphaned its own baseline entry and re-reported as "new" — a growing function is
#     what the CCN comparison is for. Legacy line-span keys are migrated on read.
#
# Usage:
#   bash tests/complexity-gate.sh                          # ratchet: refresh baseline up, fail on regressions
#   bash tests/complexity-gate.sh --check                  # CI: never writes the baseline, STILL fails on regressions
#   bash tests/complexity-gate.sh --strict                 # also fail if ANY function is over the target
#   bash tests/complexity-gate.sh --reanchor "reason"      # deliberate, recorded baseline refresh
#   bash tests/complexity-gate.sh --scope all              # whole tree (1,095 files) instead of the fork scope

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
SOURCES="$HERE/fork-sources.txt"
BASELINE="$HERE/complexity-baseline.tsv"
CCN_TARGET="${CCN_TARGET:-10}"
TOLERANCE="${COMPLEXITY_TOLERANCE:-0}"   # allowed CCN rise before failing

MODE="ratchet"
REASON=""
SCOPE="${GATE_SCOPE:-fork}"
while [[ $# -gt 0 ]]; do
	case "$1" in
		--check)    MODE="check"; shift ;;
		--strict)   MODE="strict"; shift ;;
		--reanchor) MODE="reanchor"; REASON="${2:-}"; shift 2 ;;
		--scope)    SCOPE="${2:-}"; shift 2 ;;
		*) echo "usage: $0 [--check|--strict|--reanchor \"reason\"] [--scope fork|all]" >&2; exit 2 ;;
	esac
done
case "$SCOPE" in
	fork) SOURCES="$HERE/fork-sources.txt"; BASELINE="$HERE/complexity-baseline.tsv" ;;
	all)  SOURCES="$HERE/all-sources.txt";  BASELINE="$HERE/complexity-baseline-all.tsv"
	      echo "complexity-gate: whole-tree scope (1,095 first-party files; upstream code is grandfathered, see docs/CONVENTIONS.md)" ;;
	*) echo "unknown scope '$SCOPE' (use fork|all)" >&2; exit 2 ;;
esac
if [[ "$MODE" == "reanchor" && -z "${REASON// /}" ]]; then
	echo "usage: $0 --reanchor \"reason\" — an unrecorded re-anchor is not allowed" >&2
	exit 2
fi

if ! python3 -c "import lizard" 2>/dev/null; then
	echo "complexity-gate: lizard is not installed — run: pip install lizard" >&2
	exit 2
fi

# fork sources (skip comments/blank)
mapfile -t FILES < <(grep -vE '^\s*(#|$)' "$SOURCES")
cd "$ROOT" || exit 2

# lizard --csv: nloc,ccn,token,param,length,"location","file","function",...
# Only functions actually in the fork sources are considered (lizard walks includes,
# so filter by the file column).
tmp="$(mktemp)"
python3 -m lizard --csv "${FILES[@]}" > "$tmp" 2>/dev/null

python3 - "$tmp" "$CCN_TARGET" <<'PY' > "${tmp}.over"
import csv, re, sys
csvfile, target = sys.argv[1], int(sys.argv[2])
rows, over = [], []
with open(csvfile, newline="") as fh:
    for r in csv.reader(fh):
        if len(r) < 8 or not r[0].isdigit():
            continue
        nloc, ccn, token, param, length, location, path, func = r[:8]
        rows.append(int(ccn))
        if int(ccn) > target:
            key = re.sub(r"@\d+-\d+", "", location)   # function@path — stable across moves
            over.append((key, int(ccn), int(nloc), int(length), location))
over.sort(key=lambda x: -x[1])
for key, ccn, nloc, length, location in over:
    print(f"{key}\t{ccn}\t{nloc}\t{length}\t{location}")
print(f"#TOTAL\t{len(rows)}\t{len(over)}", file=sys.stderr)
PY

total="${#FILES[@]}"
total="$(python3 -m lizard --csv "${FILES[@]}" 2>/dev/null | grep -cE '^[0-9]+,' || true)"
over_count="$(grep -c $'^' "${tmp}.over" 2>/dev/null || echo 0)"

echo "complexity-gate: ${total} functions in fork sources; ${over_count} exceed CCN ${CCN_TARGET}"
echo
if [[ "$over_count" -gt 0 ]]; then
	echo "Functions over the CCN target (grandfathered if in the baseline):"
	printf '  %-8s %-6s %-6s %s\n' "CCN" "NLOC" "LEN" "function"
	while IFS=$'\t' read -r key ccn nloc length location; do
		printf '  %-8s %-6s %-6s %s\n' "$ccn" "$nloc" "$length" "$location"
	done < "${tmp}.over"
	echo
fi

# nesting depth: lizard prints ND only inside its warnings; surface it as context.
echo "Nesting depth (lizard cannot threshold it — reported for manual review):"
python3 -m lizard -C $((CCN_TARGET + 1)) --warnings_only "${FILES[@]}" 2>/dev/null \
	| sed -E 's/^([^:]+):[0-9]+: warning: /\1: /' | head -20
echo

# --- baseline -----------------------------------------------------------------
# Read with migration: a stored key may still carry lizard's line span (legacy format).
declare -A base
if [[ -f "$BASELINE" ]]; then
	while IFS=$'	' read -r rawkey ccn rest; do
		[[ "$rawkey" =~ ^[[:space:]]*# ]] && continue
		[[ -z "${rawkey// /}" ]] && continue
		key="$(sed -E 's/@[0-9]+-[0-9]+//' <<< "$rawkey")"
		# A qualified name can legitimately appear more than once in a file (same name in
		# two scopes/overloads). Keyed by name@path those collapse, so keep the WORST
		# grandfathered value — otherwise the lower entry silently shadows the higher one
		# and the next run reports a false "CCN rose".
		if [[ -z "${base[$key]:-}" || "$ccn" -gt "${base[$key]}" ]]; then
			base["$key"]="$ccn"
		fi
	done < "$BASELINE"
fi

write_baseline() {
	{ echo "# per-method CCN baseline for fork-NEW code (functions over the target only)";
	  echo "# keyed by function@path — maintained by tests/complexity-gate.sh; do not edit by hand";
	  awk -F'	' '{ if ($2+0 > max[$1]) max[$1] = $2+0 }
	              END { for (k in max) printf "%s	%d\n", k, max[k] }' "${tmp}.over" \
		| sort -k2,2nr; } > "$BASELINE"
}

regressed=0
while IFS=$'\t' read -r key ccn nloc length location; do
	if [[ -z "${base[$key]:-}" ]]; then
		echo "REGRESSION: new function over target: $location (CCN $ccn)"
		regressed=1
	elif [[ "$ccn" -gt $(( ${base[$key]} + TOLERANCE )) ]]; then
		echo "REGRESSION: $location CCN rose ${base[$key]} -> $ccn"
		regressed=1
	fi
done < "${tmp}.over"

rc=0
case "$MODE" in
strict)
	if [[ "$over_count" -gt 0 ]]; then
		echo "FAIL (strict): ${over_count} function(s) exceed CCN ${CCN_TARGET}"; rc=1
	else
		echo "PASS (strict): every function is at or below CCN ${CCN_TARGET}"
	fi
	;;
reanchor)
	write_baseline
	echo "RE-ANCHORED: baseline rewritten from the current tree (${over_count} over-target function(s))"
	echo "reason: ${REASON}"
	;;
check|ratchet)
	if [[ "$regressed" -eq 1 ]]; then
		if [[ "$MODE" == "check" ]]; then
			echo "FAIL: complexity ratchet regressed (check mode: baseline not written)"; rc=1
		else
			echo "FAIL: complexity ratchet regressed (baseline unchanged)"; rc=1
		fi
	elif [[ "$MODE" == "check" ]]; then
		echo "PASS (check mode: no regressions; baseline not written)"
	elif [[ ! -f "$BASELINE" ]]; then
		write_baseline
		echo "PASS (baseline initialised)"
	else
		write_baseline
		echo "PASS (ratchet clean; baseline refreshed)"
	fi
	;;
esac
rm -f "$tmp" "${tmp}.over"
exit $rc
