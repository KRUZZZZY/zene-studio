#!/usr/bin/env bash
#
# run-coverage.sh - build with gcov instrumentation, run the test suite, and
# produce an lcov coverage report for the fork's new code.
#
# Usage:
#   tests/run-coverage.sh [build-dir] [extra cmake args...]
#
# Outputs:
#   <build-dir>/coverage/   lcov tracefiles + genhtml HTML report
#
# The report is filtered to the fork's new code (sources added on top of
# upstream master); see tests/fork-sources.txt for the list. The summary table
# it prints is what tests/coverage-gate.sh ratchets on.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${1:-${SOURCE_DIR}/build-coverage}"
shift || true

FORK_SOURCES="${SCRIPT_DIR}/fork-sources.txt"
if [ ! -f "${FORK_SOURCES}" ]; then
	echo "error: ${FORK_SOURCES} not found" >&2
	exit 1
fi

# The report only means something if it is generated from a Debug build.
CMAKE_ARGS=("-DCMAKE_BUILD_TYPE=Debug" "$@")
# 20 parallel jobs would starve the machine; cap at 4.
JOBS=4
# The GUI test needs a platform plugin; offscreen works headless.
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

echo "== Configuring coverage build in ${BUILD_DIR}"
cmake -B "${BUILD_DIR}" -S "${SOURCE_DIR}" "${CMAKE_ARGS[@]}" -DWANT_COVERAGE=ON

echo "== Building (capped at -j${JOBS})"
cmake --build "${BUILD_DIR}" -j"${JOBS}"

echo "== Running ctest (from ${BUILD_DIR}/tests)"
# NOTE: ctest exits non-zero if any test fails; with set -e the script stops
# here, so coverage is never produced from a red test run.
ctest --output-on-failure --test-dir "${BUILD_DIR}/tests"

echo "== Capturing coverage"
COVERAGE_DIR="${BUILD_DIR}/coverage"
rm -rf "${COVERAGE_DIR}"
mkdir -p "${COVERAGE_DIR}"

# lcov 2.x requires an explicit ignore list for mismatched/unknown versions;
# gcov-tool is needed because the build may use a gcc version different from
# the default gcov on PATH.
GCOV_TOOL="$(command -v gcov)"
LCOV_IGNORE=("--ignore-errors" "mismatch" "--ignore-errors" "unused" "--ignore-errors" "empty" "--ignore-errors" "inconsistent")

# Capture-time only: geninfo reports "negative" counters on multithreaded runs
# (a known gcov artifact when .gcda files are updated concurrently) and warns
# about unexecuted inline-header blocks. Neither is a coverage signal, so they
# are suppressed here and only here (genhtml does not accept these names).
LCOV_CAPTURE_IGNORE=("${LCOV_IGNORE[@]}" "--ignore-errors" "negative"
	"-rc" "geninfo_unexecuted_blocks=1")

lcov --capture --directory "${BUILD_DIR}" \
	--output-file "${COVERAGE_DIR}/coverage.info" \
	--gcov-tool "${GCOV_TOOL}" \
	"${LCOV_CAPTURE_IGNORE[@]}"

# Keep only the fork's own code: everything under src/include/plugins that is
# also a fork-added source (fork-sources.txt) or one of its headers. This is
# what the ratchet tracks; upstream LMMS code is intentionally excluded.
echo "== Filtering to the fork's new code"
: > "${COVERAGE_DIR}/coverage-fork.info"
while IFS= read -r rel; do
	case "${rel}" in
		\#*) continue ;;
		"") continue ;;
	esac
	lcov --extract "${COVERAGE_DIR}/coverage.info" \
		"${SOURCE_DIR}/${rel}" \
		--output-file "${COVERAGE_DIR}/one.info" \
		"${LCOV_IGNORE[@]}" || true
	if [ -s "${COVERAGE_DIR}/one.info" ]; then
		lcov --add-tracefile "${COVERAGE_DIR}/coverage-fork.info" \
			--add-tracefile "${COVERAGE_DIR}/one.info" \
			--output-file "${COVERAGE_DIR}/coverage-fork.info" \
			"${LCOV_IGNORE[@]}"
	fi
done < "${FORK_SOURCES}"
rm -f "${COVERAGE_DIR}/one.info"

if [ ! -s "${COVERAGE_DIR}/coverage-fork.info" ]; then
	echo "error: no coverage records matched tests/fork-sources.txt" >&2
	exit 1
fi

echo "== Per-directory summary (fork code only)"
awk '
	function dirname(path,  n, i, out) { n = split(path, a, "/"); if (n == 1) return "."; out = a[1]; for (i = 2; i < n; i++) out = out "/" a[i]; return out }
	/^SF:/ { path = substr($0, 4) }
	/^LF:/ { val = substr($0, 4) + 0; lf[dirname(path)] += val }
	/^LH:/ { val = substr($0, 4) + 0; lh[dirname(path)] += val }
	/^end_of_record/ { path = "" }
	END {
		printf "%-45s %10s %10s %8s\n", "directory", "lines", "hit", "rate"
		for (d in lf) {
			rate = lf[d] > 0 ? 100.0 * lh[d] / lf[d] : 100.0
			printf "%-45s %10d %10d %7.2f%%\n", d, lf[d], lh[d], rate
		}
	}' "${COVERAGE_DIR}/coverage-fork.info" | sort -k2 -n -r

awk '
	/^LF:/ { lf += substr($0, 4) }
	/^LH:/ { lh += substr($0, 4) }
	END { printf "\nTOTAL fork-code line coverage: %.2f%% (%d/%d lines)\n", 100.0 * lh / lf, lh, lf }' \
	"${COVERAGE_DIR}/coverage-fork.info"

echo "== Generating HTML report"
genhtml "${COVERAGE_DIR}/coverage-fork.info" \
	--output-directory "${COVERAGE_DIR}/html" \
	--ignore-errors source "${LCOV_IGNORE[@]}"

echo "== Done"
echo "  HTML report: ${COVERAGE_DIR}/html/index.html"
echo "  Tracefile:   ${COVERAGE_DIR}/coverage-fork.info"
echo "  Next step:   tests/coverage-gate.sh ${COVERAGE_DIR}/coverage-fork.info"