#!/usr/bin/env bash
# complexity-gate.sh — Gate 4 (per-method complexity) for the LMMS standards fork.
#
# Gate 4 was documented as "advisory, cppcheck not installed". It is now WIRED:
# it uses `lizard` (pip install lizard), which reports cyclomatic complexity (CCN)
# and nesting depth (ND) for C/C++. cppcheck 2.13 does NOT emit complexity for
# this codebase, so lizard is the tool.
#
# Policy (matches tests/QA-GATES.md Gate 4):
#   - target: CCN <= 10 per method, nesting depth <= 3
#   - this is a RATCHET, not a rewrite order: functions already over the target are
#     grandfathered in tests/complexity-baseline.tsv at their measured CCN; a NEW
#     function over the target, or an existing one whose CCN RISES, fails.
#   - lizard cannot threshold on nesting depth, so ND is REPORTED for over-target
#     functions (it is printed alongside) and reviewed manually. Stated, not hidden.
#
# Usage:
#   bash tests/complexity-gate.sh                 # ratchet: update baseline up, fail on regressions
#   bash tests/complexity-gate.sh --check         # CI: report only, never write the baseline
#   bash tests/complexity-gate.sh --strict        # also fail if ANY function is over the target

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
SOURCES="$HERE/fork-sources.txt"
BASELINE="$HERE/complexity-baseline.tsv"
CCN_TARGET="${CCN_TARGET:-10}"
TOLERANCE="${COMPLEXITY_TOLERANCE:-0}"   # allowed CCN rise before failing

MODE="ratchet"
[[ "${1:-}" == "--check"  ]] && MODE="check"
[[ "${1:-}" == "--strict" ]] && MODE="strict"

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
import csv, sys
csvfile, target = sys.argv[1], int(sys.argv[2])
rows = []
with open(csvfile, newline="") as fh:
    for r in csv.reader(fh):
        if len(r) < 8 or not r[0].isdigit():
            continue
        nloc, ccn, token, param, length, location, path, func = r[:8]
        rows.append((int(ccn), int(nloc), int(length), location, path, func))
over = [x for x in rows if x[0] > target]
over.sort(reverse=True)
for ccn, nloc, length, location, path, func in over:
    print(f"{location}\t{ccn}\t{nloc}\t{length}")
print(f"#TOTAL\t{len(rows)}\t{len(over)}", file=sys.stderr)
PY

total="$(python3 -m lizard --csv "${FILES[@]}" 2>/dev/null | grep -cE '^[0-9]+,' || true)"
over_count="$(grep -c $'^' "${tmp}.over" 2>/dev/null || echo 0)"

echo "complexity-gate: ${total} functions in fork sources; ${over_count} exceed CCN ${CCN_TARGET}"
echo
if [[ "$over_count" -gt 0 ]]; then
	echo "Functions over the CCN target (grandfathered if in the baseline):"
	printf '  %-8s %-6s %-6s %s\n' "CCN" "NLOC" "LEN" "function"
	while IFS=$'\t' read -r loc ccn nloc length; do
		printf '  %-8s %-6s %-6s %s\n' "$ccn" "$nloc" "$length" "$loc"
	done < "${tmp}.over"
	echo
fi

# nesting depth: lizard prints ND only inside its warnings; surface it as context.
echo "Nesting depth (lizard cannot threshold it — reported for manual review):"
python3 -m lizard -C $((CCN_TARGET + 1)) --warnings_only "${FILES[@]}" 2>/dev/null \
	| sed -E 's/^([^:]+):[0-9]+: warning: /\1: /' | head -20
echo

rc=0
case "$MODE" in
strict)
	if [[ "$over_count" -gt 0 ]]; then
		echo "FAIL (strict): ${over_count} function(s) exceed CCN ${CCN_TARGET}"; rc=1
	else
		echo "PASS (strict): every function is at or below CCN ${CCN_TARGET}"
	fi
	;;
check)
	echo "PASS (check mode: baseline not written)"
	;;
ratchet)
	# compare against the baseline: a NEW over-target function, or a CCN rise, fails.
	if [[ ! -f "$BASELINE" ]]; then
		echo "baseline missing — writing ${BASELINE#"$ROOT"/} from the current tree"
		{ echo "# per-method CCN baseline for fork-NEW code (functions over the target only)"; \
		  echo "# maintained by tests/complexity-gate.sh; do not edit by hand"; \
		  sort -k2,2nr "${tmp}.over"; } > "$BASELINE"
		echo "PASS (baseline initialised)"
	else
		regressed=0
		declare -A base
		while IFS=$'\t' read -r loc ccn rest; do
			[[ "$loc" =~ ^# ]] && continue
			base["$loc"]="$ccn"
		done < "$BASELINE"
		while IFS=$'\t' read -r loc ccn nloc length; do
			if [[ -z "${base[$loc]:-}" ]]; then
				echo "REGRESSION: new function over target: $loc (CCN $ccn)"; regressed=1
			elif [[ "$ccn" -gt $(( base[$loc] + TOLERANCE )) ]]; then
				echo "REGRESSION: $loc CCN rose ${base[$loc]} -> $ccn"; regressed=1
			fi
		done < "${tmp}.over"
		if [[ "$regressed" -eq 1 ]]; then
			echo "FAIL: complexity ratchet regressed (baseline unchanged)"; rc=1
		else
			{ echo "# per-method CCN baseline for fork-NEW code (functions over the target only)"; \
			  echo "# maintained by tests/complexity-gate.sh; do not edit by hand"; \
			  sort -k2,2nr "${tmp}.over"; } > "$BASELINE"
			echo "PASS (ratchet clean; baseline refreshed)"
		fi
	fi
	;;
esac
rm -f "$tmp" "${tmp}.over"
exit $rc
