#!/usr/bin/env bash
# release-mcp-bridge-leg.sh — the release fitness leg that mirrors quality-gates.yml's
# `MCP bridge Python tests (no build)` job, verbatim in what it asserts.
#
# WHY A SEPARATE SCRIPT. The job runs two unittest modules and, critically, reads each
# module's own "Ran N tests" back: 0 tests is an ERROR, never a pass. A crashed import
# still exits non-zero, but the count check is what catches a suite that silently collected
# nothing - which is exactly the shape a release must not read as green. The fitness
# oracle's leg table is a list of single commands, so the same assertions live here rather
# than being re-typed (and quietly shortened) inside it.
#
# It is the only leg that runs a REAL test suite without a build, which makes it the leg a
# deliberately red ref (a failing test) is refused by, with a real assertion failure behind
# the refusal instead of a source-scanning gate's verdict.
#
# The module list is the job's: test_units and test_wire_client are the 53 of the bridge's
# 64 tests that need no DAW binary. test_mcp_e2e and test_mcp_errors start a built `zene`
# and are dispatch-only upstream, so they are out of scope here by the job's own statement,
# not by a choice of this script.
#
# Requires the `mcp` package the bridge imports (the job installs `mcp==2.0.0`; any version
# that imports is accepted here, and a missing one is this script's exit 3).
#
# Usage: bash tests/release-mcp-bridge-leg.sh
# Exit codes: 0 = every module ran and passed, 1 = a module failed or ran 0 tests,
#             2 = setup error (no such directory), 3 = the environment cannot run it.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BRIDGE="$ROOT/tools/mcp-zene-control"

[ -d "$BRIDGE/tests" ] || { echo "release-mcp-bridge-leg: no $BRIDGE/tests" >&2; exit 2; }
if ! python3 -c 'import mcp' >/dev/null 2>&1; then
	echo "release-mcp-bridge-leg: the 'mcp' package is not importable - this leg cannot be" >&2
	echo "measured (the CI job installs mcp==2.0.0). An unmeasured leg is a refusal." >&2
	exit 3
fi

cd "$BRIDGE" || exit 2
status=0
total=0
for module in test_units test_wire_client; do
	log="$(mktemp)"
	code=0
	python3 -m unittest "tests.${module}" > "$log" 2>&1 || code=$?
	ran="$(sed -n 's/^Ran \([0-9][0-9]*\) tests\?.*/\1/p' "$log" | tail -1)"
	echo "--- python3 -m unittest tests.${module} (exit ${code}, ran ${ran:-0}) ---"
	cat "$log"
	echo "--- end tests.${module} ---"
	if [ "${ran:-0}" -eq 0 ]; then
		echo "tests.${module} ran 0 tests - 0 tests is an error, not a pass" >&2
		status=1
	fi
	[ "$code" -eq 0 ] || status=1
	total=$((total + ${ran:-0}))
	rm -f "$log"
done
echo "mcp-bridge-python-tests: ${total} test(s) run"
if [ "$total" -eq 0 ]; then
	echo "no tests ran at all - that is an error, not a pass" >&2
	status=1
fi
exit "$status"
