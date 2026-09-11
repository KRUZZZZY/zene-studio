#!/usr/bin/env bash
# test-verification-debt.sh — red/green proof for the three verification-debt fixes
# (docs/VERIFICATION-DEBT-FIXES.md; defects recorded in docs/STATUS.md).
#
# For each defect this script builds a synthetic fixture in a temp directory and runs the
# changed gate TWICE against the same fixture: once from the PRE-FIX revision of the
# script (fetched with `git show <base>:tests/<name>`) and once from this working tree.
# Every exit code is read unpiped into a variable, and the assertions are printed with
# expected vs actual. The script exits non-zero if any assertion fails.
#
# What "red" means per defect — the direction differs, and saying so is the point:
#   1. coverage gate   red = the pre-fix gate ACCEPTS a fixture it must refuse
#                      (exit 0, and a zero-instrumented file is banked at 100.00%);
#                      green = the fixed gate exits 1 on that fixture, 0 on a good one.
#   2. run-all-gates   red = the pre-fix runner reports PASS (exit 0) for a run in which
#                      gates were SKIPPED; green = the fixed runner exits 3, names the
#                      skipped gates, and still exits 0 when every gate runs.
#   3. fork-sources    red = no check for the omission exists at the pre-fix revision,
#                      and the only gate that reacts (Gate 6) calls it the WRONG thing
#                      ("undeclared change to upstream-inherited code"); green = the new
#                      gate names the file as "not in tests/fork-sources.txt" (exit 1)
#                      and exits 0 once the file is registered.
#
# Usage:
#   bash tests/test-verification-debt.sh            # run the proof
#   VERIFICATION_DEBT_BASE=<rev> ...                # override the pre-fix revision
#   KEEP_FIXTURES=1 ...                             # keep the temp trees for inspection
#
# Exit codes: 0 = every assertion held, 1 = an assertion failed, 2 = setup error.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT" || exit 2

BASE="${VERIFICATION_DEBT_BASE:-0c23587d2}"
if ! git rev-parse --verify --quiet "$BASE^{commit}" >/dev/null; then
	echo "error: pre-fix revision '$BASE' is not a commit in this repo" >&2
	echo "set VERIFICATION_DEBT_BASE to the commit the fixes were built on" >&2
	exit 2
fi

SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/zene-verification-debt.XXXXXX")" || exit 2
if [[ "${KEEP_FIXTURES:-0}" != "1" ]]; then
	trap 'rm -rf "$SCRATCH"' EXIT
fi

FAILED=0
check() { # check <label> <expected> <actual>
	if [[ "$2" == "$3" ]]; then
		printf '  OK   %-64s exit %s\n' "$1" "$3"
	else
		printf '  FAIL %-64s expected exit %s, got %s\n' "$1" "$2" "$3"
		FAILED=1
	fi
}
assert_has() { # assert_has <label> <file> <literal>
	if grep -qF -- "$3" "$2"; then
		printf '  OK   %-64s %s\n' "$1" "'$3' present"
	else
		printf '  FAIL %-64s %s not found in %s\n' "$1" "'$3'" "$2"
		FAILED=1
	fi
}
assert_lacks() { # assert_lacks <label> <file> <literal>
	if grep -qF -- "$3" "$2"; then
		printf '  FAIL %-64s %s unexpectedly present in %s\n' "$1" "'$3'" "$2"
		FAILED=1
	else
		printf '  OK   %-64s %s\n' "$1" "'$3' absent"
	fi
}
show() { printf '\n$ %s\n' "$*"; }

echo "pre-fix revision: $BASE ($(git log -1 --format=%h\ %s "$BASE"))"
echo "fixtures: $SCRATCH"

# ============================================================================
# Defect 1 — coverage-gate.sh: no entry floor, zero-instrumented file banked 100%
# ============================================================================
echo
echo "=== Defect 1: coverage entry floor + unmeasurable files ==="
C1="$SCRATCH/coverage"
mkdir -p "$C1/tests" "$C1/src"
git show "$BASE:tests/coverage-gate.sh" > "$C1/tests/coverage-gate-old.sh"
cp "$ROOT/tests/coverage-gate.sh" "$C1/tests/coverage-gate.sh"

# fixture: a stable 100% file, a file with ZERO instrumented lines, and a brand-new
# file whose measured coverage is 5% — the "new code with no tests" case.
trace_bad="$C1/trace-bad.info"
trace_good="$C1/trace-good.info"
{
	echo "SF:$C1/src/stable.cpp";     echo "LF:10";  echo "LH:10";  echo "end_of_record"
	echo "SF:$C1/src/braindead.cpp";  echo "LF:0";   echo "LH:0";   echo "end_of_record"
	echo "SF:$C1/src/newlow.cpp";     echo "LF:100"; echo "LH:5";   echo "end_of_record"
} > "$trace_bad"
{
	echo "SF:$C1/src/stable.cpp";   echo "LF:10";  echo "LH:10";  echo "end_of_record"
	echo "SF:$C1/src/newgood.cpp";  echo "LF:100"; echo "LH:95";  echo "end_of_record"
} > "$trace_good"

printf '# baseline\nsrc/stable.cpp\t100.00\n' > "$C1/base-old-bad.tsv"
printf '# baseline\nsrc/stable.cpp\t100.00\n' > "$C1/base-old-good.tsv"
printf '# baseline\nsrc/stable.cpp\t100.00\n' > "$C1/base-new-bad.tsv"
printf '# baseline\nsrc/stable.cpp\t100.00\n' > "$C1/base-new-good.tsv"

show "bash tests/coverage-gate.sh trace-bad.info base-*.tsv   # pre-fix first"
bash "$C1/tests/coverage-gate-old.sh" "$trace_bad" "$C1/base-old-bad.tsv" > "$C1/old-bad.log" 2>&1
rc=$?; check "defect 1 RED    pre-fix gate on the bad fixture (must be exit 0: it does not bite)" 0 "$rc"
assert_has "defect 1 RED    pre-fix run banks the zero-instrumented file at 100%" \
	"$C1/base-old-bad.tsv" "$(printf 'src/braindead.cpp\t100.00')"
assert_lacks "defect 1 RED    pre-fix run never mentions an entry floor" "$C1/old-bad.log" "entry floor"

bash "$C1/tests/coverage-gate.sh" "$trace_bad" "$C1/base-new-bad.tsv" > "$C1/new-bad.log" 2>&1
rc=$?; check "defect 1 GREEN  fixed gate on the bad fixture (must be exit 1)" 1 "$rc"
assert_has "defect 1 GREEN  fixed run reports the 5% new file below the floor" \
	"$C1/new-bad.log" "src/newlow.cpp: 5.00%"
assert_has "defect 1 GREEN  fixed run reports the zero-instrumented file as unmeasurable" \
	"$C1/new-bad.log" "unmeasurable  src/braindead.cpp: 0 instrumented lines"
assert_lacks "defect 1 GREEN  nothing was banked at 100% on the failed run" \
	"$C1/base-new-bad.tsv" "$(printf 'src/braindead.cpp\t100.00')"

show "bash tests/coverage-gate.sh trace-good.info base-new-good.tsv   # control: good new file"
bash "$C1/tests/coverage-gate.sh" "$trace_good" "$C1/base-new-good.tsv" > "$C1/new-good.log" 2>&1
rc=$?; check "defect 1 GREEN  fixed gate on the good fixture (must be exit 0)" 0 "$rc"
assert_has "defect 1 GREEN  the 95% new file entered the baseline at its measured value" \
	"$C1/base-new-good.tsv" "$(printf 'src/newgood.cpp\t95.00')"

show "bash tests/coverage-gate.sh trace-bad.info base-*.tsv   # old gate, good fixture"
bash "$C1/tests/coverage-gate-old.sh" "$trace_good" "$C1/base-old-good.tsv" > "$C1/old-good.log" 2>&1
rc=$?; check "defect 1 CONTROL pre-fix gate on the good fixture (unchanged behaviour)" 0 "$rc"

# ============================================================================
# Defect 2 — run-all-gates.sh: SKIP laundered into PASS
# ============================================================================
echo
echo "=== Defect 2: run-all-gates.sh treats SKIP as a pass ==="
C2="$SCRATCH/runner"
mkdir -p "$C2/tests"
git show "$BASE:tests/run-all-gates.sh" > "$C2/tests/run-all-gates-old.sh"
cp "$ROOT/tests/run-all-gates.sh" "$C2/tests/run-all-gates.sh"
# stub gates: every one passes, so only the SKIP treatment differs between revisions
for g in no-tautology-gate.sh complexity-gate.sh no-upstream-regression-gate.sh \
	file-length-gate.sh duplication-gate.sh mutation-gate.sh run-coverage.sh coverage-gate.sh \
	fork-sources-gate.sh; do
	printf '#!/usr/bin/env bash\nexit 0\n' > "$C2/tests/$g"
done

show "bash <fixture>/tests/run-all-gates-old.sh   # no build/, no --with-coverage: gates 1 and 2 skip"
bash "$C2/tests/run-all-gates-old.sh" > "$C2/old.log" 2>&1
rc=$?; check "defect 2 RED    pre-fix runner with 2 skipped gates (must be exit 0: looks green)" 0 "$rc"
assert_has "defect 2 RED    pre-fix summary claims every executed gate passed" \
	"$C2/old.log" "RESULT: PASS — every executed gate passed"

show "bash <fixture>/tests/run-all-gates.sh       # same fixture, fixed runner"
bash "$C2/tests/run-all-gates.sh" > "$C2/new.log" 2>&1
rc=$?; check "defect 2 GREEN  fixed runner with 2 skipped gates (must be exit 3)" 3 "$rc"
assert_has "defect 2 GREEN  summary counts the skipped gates" \
	"$C2/new.log" "skipped: 2 of 9 gates did not run"
assert_has "defect 2 GREEN  summary names gate 1" "$C2/new.log" "gate 1 (ctest): no configured build/ directory"
assert_has "defect 2 GREEN  summary names gate 2" "$C2/new.log" "gate 2 (coverage): --with-coverage was not passed"
assert_has "defect 2 GREEN  verdict is not a pass" "$C2/new.log" "RESULT: PASS-WITH-SKIPS (exit 3)"

# control: a run in which every gate executes and passes must still exit 0
mkdir -p "$C2/build/tests"
printf 'cmake_minimum_required(VERSION 3.16)\nproject(gate_fixture CXX)\nenable_testing()\nadd_subdirectory(tests)\n' \
	> "$C2/build/CMakeLists.txt"
printf 'add_test(NAME fixture_smoke COMMAND ${CMAKE_COMMAND} -E true)\n' > "$C2/build/tests/CMakeLists.txt"
cmake -S "$C2/build" -B "$C2/build" > "$C2/build-configure.log" 2>&1
show "bash <fixture>/tests/run-all-gates.sh --with-coverage   # every gate runs"
bash "$C2/tests/run-all-gates.sh" --with-coverage > "$C2/new-full.log" 2>&1
rc=$?; check "defect 2 CONTROL fixed runner with every gate run (must be exit 0)" 0 "$rc"
assert_has "defect 2 CONTROL verdict is a real pass" "$C2/new-full.log" "RESULT: PASS — every executed gate passed (9/9 ran)"

# ============================================================================
# Defect 3 — nothing says "this new source is not in tests/fork-sources.txt"
# ============================================================================
echo
echo "=== Defect 3: an unregistered new source file is never named ==="
C3="$SCRATCH/forksrc"
mkdir -p "$C3/src/core" "$C3/include" "$C3/tests"
git -C "$C3" init -q
git -C "$C3" config user.email proof@example.invalid
git -C "$C3" config user.name "verification-debt proof"
printf '# fork-NEW sources\nsrc/fork_new.cpp\n' > "$C3/tests/fork-sources.txt"
printf '# upstream-inherited sources\nsrc/upstream_known.cpp\n' > "$C3/tests/all-sources.txt"
printf 'int fork_new = 1;\n' > "$C3/src/fork_new.cpp"
printf 'int upstream_known = 1;\n' > "$C3/src/upstream_known.cpp"
git -C "$C3" add -A >/dev/null && git -C "$C3" commit -qm base
BASE_COMMIT="$(git -C "$C3" rev-parse HEAD)"

# a commit that adds two new product sources, registered in NO scope list — the
# LatencyCompensation incident, reduced to its essentials
printf 'int latency_compensation = 1;\n' > "$C3/src/core/LatencyCompensation.cpp"
printf 'int latency_compensation_h = 1;\n' > "$C3/include/LatencyCompensation.h"
git -C "$C3" add -A >/dev/null && git -C "$C3" commit -qm "add LatencyCompensation without registering it"

echo "pre-fix check for the omission:"
if git cat-file -e "$BASE:tests/fork-sources-gate.sh" 2>/dev/null; then
	printf '  FAIL %-64s exists at the pre-fix revision\n' "no registration check at $BASE"
	FAILED=1
else
	printf '  OK   %-64s does not exist at %s — nothing ran this check\n' "no registration check at $BASE" "$BASE"
fi

git show "$BASE:tests/no-upstream-regression-gate.sh" > "$C3/tests/no-upstream-regression-gate.sh"
show "bash <fixture>/tests/no-upstream-regression-gate.sh $BASE_COMMIT   # the pre-fix reaction (Gate 6)"
bash "$C3/tests/no-upstream-regression-gate.sh" "$BASE_COMMIT" > "$C3/gate6.log" 2>&1
rc=$?; check "defect 3 RED    pre-fix Gate 6 exits non-zero on the unregistered file" 1 "$rc"
assert_has "defect 3 RED    ...but calls it an undeclared change to UPSTREAM code (the wrong diagnosis)" \
	"$C3/gate6.log" "undeclared change to upstream-inherited code"
assert_lacks "defect 3 RED    ...and never says the file is missing from tests/fork-sources.txt" \
	"$C3/gate6.log" "not in tests/fork-sources.txt"

cp "$ROOT/tests/fork-sources-gate.sh" "$C3/tests/fork-sources-gate.sh"
show "bash <fixture>/tests/fork-sources-gate.sh   # the new gate, same commit"
bash "$C3/tests/fork-sources-gate.sh" > "$C3/gate9-bad.log" 2>&1
rc=$?; check "defect 3 GREEN  new gate exits 1 on the unregistered files" 1 "$rc"
assert_has "defect 3 GREEN  it names src/core/LatencyCompensation.cpp" \
	"$C3/gate9-bad.log" "src/core/LatencyCompensation.cpp"
assert_has "defect 3 GREEN  it names include/LatencyCompensation.h" \
	"$C3/gate9-bad.log" "include/LatencyCompensation.h"
assert_has "defect 3 GREEN  it says plainly what the omission is" \
	"$C3/gate9-bad.log" "are not in tests/fork-sources.txt"

printf 'src/core/LatencyCompensation.cpp\ninclude/LatencyCompensation.h\n' >> "$C3/tests/fork-sources.txt"
show "bash <fixture>/tests/fork-sources-gate.sh   # after registering the two files"
bash "$C3/tests/fork-sources-gate.sh" > "$C3/gate9-good.log" 2>&1
rc=$?; check "defect 3 GREEN  new gate exits 0 once the files are registered" 0 "$rc"
assert_has "defect 3 GREEN  and reports the registered counts" "$C3/gate9-good.log" "PASS: every tracked source in scope is registered"

# ============================================================================
echo
echo "================ SUMMARY ================"
if [[ $FAILED -eq 1 ]]; then
	echo "RESULT: FAIL — at least one assertion above did not hold."
	exit 1
fi
echo "RESULT: PASS — every red/green assertion held."
exit 0
