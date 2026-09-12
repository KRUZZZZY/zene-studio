#!/usr/bin/env bash
# Print the ledger/one-line numbers of one merge's evidence dir, for the report table.
# usage: numbers.sh <baseline|merge1..merge6|final>
set -u
cd "$(dirname "$0")/../../.." || exit 2
L="tests/integration-logs-3e/$1"
echo "== $1 =="
for f in local-ci merge.exit gate9 gate6 run-all-gates regen-index regen-head precommit-index; do
	[[ -f "${L}/${f}.exit" ]] && echo "  $(cat "${L}/${f}.exit")"
done
grep -E 'ctest totals' "${L}/local-ci.log" 2>/dev/null | tail -1 | sed 's/^/  /'
grep -E 'PASS: every tracked source in scope' "${L}/gate9.log" 2>/dev/null | tail -1 | sed 's/^/  gate9: /'
grep -E 'changed path\(s\) declared' "${L}/gate6.log" 2>/dev/null | tail -1 | sed 's/^/  gate6: /'
grep -E '^RESULT:' "${L}/run-all-gates.log" 2>/dev/null | tail -1 | sed 's/^/  gate-runner: /'
grep -E 'REPRODUCES|VERIFY RESULT' "${L}/regen-head.log" 2>/dev/null | sed 's/^/  /'
grep -E 'GATE6-REPLICA|cross-manifest' "${L}/precommit-index.log" 2>/dev/null | sed 's/^/  /'
