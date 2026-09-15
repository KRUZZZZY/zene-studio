#!/usr/bin/env bash
# run-proofs-w4.sh - run every ctest the EIGHT merged branches registered, plus
# the full suite, from <build>/tests as the rules require. Exit codes UNPIPED.
#
# The eight names were enumerated by diffing tests/CMakeLists.txt against
# 3fae5a5e1 (the pre-merge tip): 3 add_test(NAME ...) entries and 5 LMMS_TESTS
# sources, each of which auto-registers a ctest of the same name.
set -u
L=../../.merge-logs-030w4
NEW="LuaApiSurface ControlMcpGroupCoverage ControlDetectCommands
     PatcherCommandsTest RevisionTimelineTest ControlDetectCommands
     SafeStartTest SafeStartLoadPathTest SampleAccurateAutomationTest"

run_one() {
	name="$1"
	echo "======== $name"
	ctest -R "^${name}\$" --output-on-failure > "$L/ctest-$name.log" 2>&1
	rc=$?
	echo "EXIT=$rc"
	tail -25 "$L/ctest-$name.log" | sed 's/^/    /'
	echo
}

echo "== ctest inventory (this directory MUST report a non-zero test count)"
ctest -N > "$L/ctest-list.log" 2>&1
echo "ctest -N EXIT=$?"
grep -c "Test *#" "$L/ctest-list.log"
echo

echo "== the eight tests the merged branches registered"
for t in $NEW; do run_one "$t"; done

echo "== the full suite (unpiped)"
ctest --output-on-failure > "$L/ctest-suite.log" 2>&1
echo "FULL SUITE EXIT=$?"
grep -E "tests passed|tests failed|Total Test time" "$L/ctest-suite.log" | tail -3
echo "--- failures, if any ---"
grep -E "^\s*[0-9]+/[0-9]+ Test +#[0-9]+" "$L/ctest-suite.log" | grep -v "Passed" | head -30
grep -E "\*\*\*Failed|Failed +" "$L/ctest-suite.log" | head -30
