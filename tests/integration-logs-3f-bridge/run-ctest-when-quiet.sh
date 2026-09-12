#!/usr/bin/env bash
# Wait for a quiet window in this worktree's build tree, then run ctest once.
#
# Why: this worktree's build/ is shared with whatever else is running on the box
# (another lane relinking the same test binaries is enough), and ctest reports a
# binary that is mid-relink as "***Not Run" + "[permission denied]" — that is a
# measurement of the relink, not of the code. The loop only decides WHEN to
# measure; it never builds anything itself.
#
#   bash tests/integration-logs-3f-bridge/run-ctest-when-quiet.sh > tests/integration-logs-3f-bridge/ctest-from-build-tests.log 2>&1
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BUILD_TESTS="$ROOT/build/tests"
QUIET_FOR=3          # consecutive quiet checks
MAX_WAIT=900         # seconds
INTERVAL=5

busy() {
	pgrep -f 'cmake --build|/usr/bin/make|cc1plus|/usr/bin/ld ' > /dev/null 2>&1
}

quiet=0
waited=0
while [[ $waited -lt $MAX_WAIT ]]; do
	if busy; then
		quiet=0
	else
		quiet=$((quiet + 1))
		if [[ $quiet -ge $QUIET_FOR ]]; then break; fi
	fi
	sleep "$INTERVAL"
	waited=$((waited + INTERVAL))
done
echo "=== ctest window: waited ${waited}s for a quiet build tree (quiet checks: ${quiet}) ==="
if [[ $quiet -lt $QUIET_FOR ]]; then
	echo "=== gave up after ${MAX_WAIT}s: the build tree never went quiet ==="
	exit 2
fi

cd "$BUILD_TESTS" || exit 2
QT_QPA_PLATFORM=offscreen ctest
rc=$?
echo "=== ctest exit: $rc ==="
exit $rc
