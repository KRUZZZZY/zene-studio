#!/usr/bin/env bash
# run-merge-checks.sh — the per-merge verification of merge train 3D.
#
# usage: bash tests/integration-logs-3d/run-merge-checks.sh <merge-dir-name>
#
# Runs, in this order and never concurrently (the mutation gate (gate 5) mutates
# src/core/RoutingGraph.cpp and rebuilds, so a second gate run at the same time
# corrupts both):
#   1. the build + ctest  (tools/local-ci.sh, -j4, ctest from build/tests)
#   2. Gate 9  tests/fork-sources-gate.sh
#   3. Gate 6  tests/no-upstream-regression-gate.sh
#   4. tests/run-all-gates.sh          (ten gates; 3 = PASS-WITH-SKIPS, not a pass)
#   5. the three manifest re-derivations against HEAD
#   6. git status, to catch a mutant the mutation gate failed to restore
# Every exit code is measured unpiped and appended to the log it belongs to.
set -uo pipefail
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-integration || exit 2
D="tests/integration-logs-3d/${1:?merge dir name required}"
mkdir -p "$D"

echo "--- [1/6] build + ctest (tools/local-ci.sh --build-dir build --jobs 4) ---"
JOBS=2 bash tools/local-ci.sh --build-dir build --jobs 4 > "$D/local-ci.log" 2>&1
echo "LOCAL_CI_EXIT=$?" >> "$D/local-ci.log"

echo "--- [2/6] Gate 9 (fork-sources) ---"
bash tests/fork-sources-gate.sh > "$D/gate9.log" 2>&1
echo "GATE9_EXIT=$?" >> "$D/gate9.log"

echo "--- [3/6] Gate 6 (no upstream regression) ---"
bash tests/no-upstream-regression-gate.sh > "$D/gate6.log" 2>&1
echo "GATE6_EXIT=$?" >> "$D/gate6.log"

echo "--- [4/6] run-all-gates.sh (ten gates) ---"
bash tests/run-all-gates.sh > "$D/run-all-gates.log" 2>&1
echo "RUNALL_EXIT=$?" >> "$D/run-all-gates.log"

echo "--- [5/6] manifest re-derivation at HEAD ---"
python3 tests/integration-logs-3d/tools/regen.py --head > "$D/regen-head.log" 2>&1
echo "REGEN_HEAD_EXIT=$?" >> "$D/regen-head.log"

echo "--- [6/6] tree state after the gates (a mutant left behind shows here) ---"
git status --porcelain | grep -v '^??' > "$D/tree-after-gates.txt"
if [ -s "$D/tree-after-gates.txt" ]; then
	echo "TREE-NOT-CLEAN:"; cat "$D/tree-after-gates.txt"
else
	echo "TREE-CLEAN (no tracked modification left by any gate)"
fi
echo "DONE"
