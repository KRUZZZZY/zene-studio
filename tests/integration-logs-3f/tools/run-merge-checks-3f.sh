#!/usr/bin/env bash
# Merge train 3F — the per-state check bundle (3E's bundle, re-pointed at 3F).
# usage: run-merge-checks-3f.sh <TAG>        TAG = baseline | merge1 | merge2 | final | tip
#
# Runs, in this order and each with an UNPIPED exit code:
#   1. tools/local-ci.sh            (provision/configure/build/ctest; 0 tests is an ERROR)
#   2. tests/fork-sources-gate.sh   (Gate 9, expect 0)
#   3. tests/no-upstream-regression-gate.sh (Gate 6, expect 0)
#   4. tests/run-all-gates.sh       (expect 3 = PASS-WITH-SKIPS, ten gates)
#   5. the three manifest re-derivations against the index, then against HEAD
#   6. precommit_check.py INDEX     (Gate 6's rule replayed against the index)
#   7. git status, to catch a mutant the mutation gate failed to restore
set -u
TAG="$1"
cd "$(dirname "$0")/../../.." || exit 2
L="tests/integration-logs-3f/${TAG}"
mkdir -p "$L"

JOBS=2 bash tools/local-ci.sh --build-dir build --jobs 4 > "${L}/local-ci.log" 2>&1
echo "LOCAL_CI_EXIT=$?" | tee "${L}/local-ci.exit"

bash tests/fork-sources-gate.sh > "${L}/gate9.log" 2>&1
echo "GATE9_EXIT=$?" | tee "${L}/gate9.exit"

bash tests/no-upstream-regression-gate.sh > "${L}/gate6.log" 2>&1
echo "GATE6_EXIT=$?" | tee "${L}/gate6.exit"

bash tests/run-all-gates.sh > "${L}/run-all-gates.log" 2>&1
echo "RUN_ALL_GATES_EXIT=$?" | tee "${L}/run-all-gates.exit"

python3 tests/integration-logs-3f/tools/regen.py INDEX > "${L}/regen-index.log" 2>&1
echo "REGEN_INDEX_EXIT=$?" | tee "${L}/regen-index.exit"
python3 tests/integration-logs-3f/tools/regen.py --head > "${L}/regen-head.log" 2>&1
echo "REGEN_HEAD_EXIT=$?" | tee "${L}/regen-head.exit"

python3 tests/integration-logs-3f/tools/precommit_check.py INDEX > "${L}/precommit-index.log" 2>&1
echo "PRECOMMIT_EXIT=$?" | tee "${L}/precommit-index.exit"

# The capability contract and the release honesty guard (3F addition: the brief's
# third proof). The gate needs a build to judge, so it runs after local-ci.
grep -vE '^#|^$' tests/advertised-features.tsv > "${L}/contract-rows.txt"
echo "CONTRACT_ROWS=$(grep -c . "${L}/contract-rows.txt")" | tee "${L}/contract-rows.exit"
QT_QPA_PLATFORM=offscreen ./build/zene --version > "${L}/version.txt" 2>&1
bash tests/release-honesty-gate.sh --dump "${L}/version.txt" > "${L}/honesty-gate.log" 2>&1
echo "HONESTY_EXIT=$?" | tee "${L}/honesty-gate.exit"

git status --porcelain > "${L}/tree-after-gates.txt" 2>&1
echo "--- tree-after-gates (expect only untracked tests/integration-logs-3f/*):"
grep -vE '^\?\? tests/integration-logs-3f/' "${L}/tree-after-gates.txt" || echo "  (clean apart from this train's own evidence)"
cp "${L}/local-ci.log" "${L}/bundle.log" 2>/dev/null || true
