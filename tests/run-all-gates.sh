#!/usr/bin/env bash
# run-all-gates.sh — run every executable QA gate for the LMMS standards fork.
#
# Usage:
#   bash tests/run-all-gates.sh                 # fast gates (1, 3, 4, 6)
#   bash tests/run-all-gates.sh --with-coverage # + Gate 2 (full coverage build; slow)
#   bash tests/run-all-gates.sh --strict        # pass --strict to gates that support it
#
# Exit 0 only if every gate that ran passed. Each gate's own output is printed.
# Gate 5 (mutation testing) is advisory/deferred by design — see tests/QA-GATES.md.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT" || exit 2

WITH_COVERAGE=0
STRICT=""
for arg in "$@"; do
	case "$arg" in
		--with-coverage) WITH_COVERAGE=1 ;;
		--strict) STRICT="--strict" ;;
	esac
done

declare -a RESULTS
fail=0

banner() { printf '\n================ Gate %s: %s ================\n' "$1" "$2"; }
record() { RESULTS+=("$1|$2|$3"); [[ "$3" != "PASS" ]] && fail=1; }

# ---- Gate 1: unit tests -----------------------------------------------------
banner 1 "unit tests (ctest)"
if [[ ! -d build ]]; then
	echo "no build/ — configure first: cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DWANT_QT6=ON && cmake --build build -j4"
	record 1 "ctest" "SKIP"
else
	cmake --build build -j4 > /tmp/gate1-build.log 2>&1
	build_rc=$?
	if [[ $build_rc -ne 0 ]]; then
		echo "build FAILED (exit $build_rc) — tail:"; tail -15 /tmp/gate1-build.log
		record 1 "ctest" "FAIL"
	else
		( cd build/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure )
		ctest_rc=$?
		[[ $ctest_rc -eq 0 ]] && record 1 "ctest" "PASS" || record 1 "ctest" "FAIL"
	fi
fi

# ---- Gate 2: coverage ratchet (optional) ------------------------------------
banner 2 "coverage ratchet"
if [[ $WITH_COVERAGE -eq 1 ]]; then
	bash tests/run-coverage.sh build-coverage && \
	bash tests/coverage-gate.sh build-coverage/coverage/coverage-fork.info --check
	[[ $? -eq 0 ]] && record 2 "coverage" "PASS" || record 2 "coverage" "FAIL"
else
	echo "skipped (pass --with-coverage)"
	record 2 "coverage" "SKIP"
fi

# ---- Gate 3: no tautological tests ------------------------------------------
banner 3 "no tautological tests"
bash tests/no-tautology-gate.sh $STRICT
[[ $? -eq 0 ]] && record 3 "no-tautology" "PASS" || record 3 "no-tautology" "FAIL"

# ---- Gate 4: per-method complexity ------------------------------------------
banner 4 "per-method complexity"
bash tests/complexity-gate.sh --check
[[ $? -eq 0 ]] && record 4 "complexity" "PASS" || record 4 "complexity" "FAIL"

# ---- Gate 5: mutation testing (advisory, not run) ---------------------------
banner 5 "mutation testing (advisory/deferred)"
echo "not run — tooling immature for Qt/C++ (QA-GATES.md Gate 5); coverage ratchet + real assertions are the substitutes"
record 5 "mutation" "SKIP"

# ---- Gate 6: no upstream behavioural regressions ----------------------------
banner 6 "no upstream behavioural regressions"
bash tests/no-upstream-regression-gate.sh
[[ $? -eq 0 ]] && record 6 "upstream-regression" "PASS" || record 6 "upstream-regression" "FAIL"

# ---- Gate 7: per-file length -------------------------------------------------
banner 7 "per-file length (<=500 ratchet)"
bash tests/file-length-gate.sh --check
[[ $? -eq 0 ]] && record 7 "file-length" "PASS" || record 7 "file-length" "FAIL"

# ---- Gate 8: token duplication ----------------------------------------------
banner 8 "token duplication (<5%)"
bash tests/duplication-gate.sh
[[ $? -eq 0 ]] && record 8 "duplication" "PASS" || record 8 "duplication" "FAIL"

# ---- summary ----------------------------------------------------------------
printf '\n================ SUMMARY ================\n'
printf '%-6s %-24s %s\n' "gate" "name" "result"
for row in "${RESULTS[@]}"; do
	IFS='|' read -r g n r <<< "$row"
	printf '%-6s %-24s %s\n' "$g" "$n" "$r"
done
echo
if [[ $fail -eq 1 ]]; then
	echo "RESULT: FAIL — see the failing gate above"
	exit 1
fi
echo "RESULT: PASS — every executed gate passed"
