#!/usr/bin/env bash
#
# tools/local-ci.sh — reproduce the Linux job of .github/workflows/build.yml as
# closely as this machine allows, in one command, and state honestly which of
# the other six jobs it cannot reproduce.
#
# Usage:
#   tools/local-ci.sh                  configure + build + ctest (the CI's linux-x86_64 steps)
#   tools/local-ci.sh --configure-only configure only (cheapest flag/dependency check)
#   tools/local-ci.sh --no-build       skip the compile; run ctest against an existing build
#   tools/local-ci.sh --jobs N         compile parallelism (default 4; the CI's cap is -j2)
#   tools/local-ci.sh --build-dir DIR  build directory (default: build-ci)
#   tools/local-ci.sh --help
#
# Exit status: 0 only if every step that ran passed. ctest reporting 0 tests is
# an error, never a pass (workspace rule 7). Logs land in <build-dir>/*.log and
# the exit code of each step is printed unpiped (workspace rule 6).
#
# Step 0 (2026-09-12) provisions the pinned plugin-hosting dependencies, the same
# step the release jobs run: the shipped v0.1.0-alpha configured on all seven
# jobs with "VST3 hosting skipped" / "CLAP hosting skipped" and shipped neither
# host, so the release configuration now asks for both explicitly
# (-DWANT_VST3=ON -DWANT_CLAP=ON) and the fetch happens up front.
#
# Exactness notes:
#   * CI_CMAKE_OPTS below is the linux-x86_64 job's CMAKE_OPTS, byte for byte.
#   * The CI runner installs qtbase5-dev, so the CI job configures against Qt5.
#     This box has no Qt5 development files and no sudo: when Qt5 is missing the
#     script adds -DWANT_QT6=ON (part of task #616's documented configuration)
#     and prints it as a DEVIATION. It never silently changes the flag set.
#   * ccache missing is a warning, not a failure: CompileCache.cmake degrades to
#     a plain build. That is reported as a deviation as well.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="build-ci"
JOBS="${JOBS:-4}"
CTEST_JOBS="${CTEST_JOBS:-2}"
MODE="full"

# --- the linux-x86_64 job's exact CMAKE_OPTS (build.yml, env.CMAKE_OPTS) -----
CI_CMAKE_OPTS=(
	-DUSE_WERROR=ON
	-DCMAKE_BUILD_TYPE=RelWithDebInfo
	-DTARGET_UARCH=official
	-DUSE_COMPILE_CACHE=ON
	-DWANT_DEBUG_CPACK=ON
	-DWANT_VST3=ON
	-DWANT_CLAP=ON
)

usage() {
	cat <<'EOF'
tools/local-ci.sh — reproduce the CI's linux-x86_64 job locally, in one command.

Usage:
  tools/local-ci.sh                  configure + build + ctest (the CI's steps)
  tools/local-ci.sh --configure-only configure only (cheapest flag/dependency check)
  tools/local-ci.sh --no-build       skip the compile; run ctest against an existing build
  tools/local-ci.sh --jobs N         compile parallelism (default 4; the CI's cap is -j2)
  tools/local-ci.sh --build-dir DIR  build directory (default: build-ci)
  tools/local-ci.sh --help

Runs cmake with the linux-x86_64 job's exact CMAKE_OPTS:
  -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official
  -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON -DWANT_VST3=ON -DWANT_CLAP=ON
Before that it runs the same provisioning step the workflow runs
(.github/workflows/provision-plugin-hosting-deps.sh) so the pinned VST3 SDK and
CLAP headers are in <build>/vst3sdk and <build>/clap; -DWANT_VST3=ON on a tree
without them is a configure FAILURE, not a silent skip (that silent skip is what
the published v0.1.0-alpha shipped on all seven jobs). It then builds with a
capped job count and runs ctest from <build>/tests; 0 tests is an error. Every
job of the workflow matrix gets a summary line: REPRODUCED or
NOT-REPRODUCIBLE-HERE (<reason>).
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		--configure-only) MODE="configure-only"; shift ;;
		--no-build)       MODE="no-build";       shift ;;
		--jobs)           JOBS="${2:?--jobs needs a value}";       shift 2 ;;
		--build-dir)      BUILD_DIR="${2:?--build-dir needs a value}"; shift 2 ;;
		-h|--help)        usage; exit 0 ;;
		*) echo "local-ci: unknown argument: $1" >&2; usage >&2; exit 2 ;;
	esac
done

cd "$REPO_ROOT" || exit 2
mkdir -p "$BUILD_DIR"

DEVIATIONS=()

# --- environment probes (facts, not guesses) --------------------------------
CCACHE_TOOL="$(command -v ccache 2>/dev/null || true)"
if [ -z "$CCACHE_TOOL" ]; then
	DEVIATIONS+=("ccache not on PATH: -DUSE_COMPILE_CACHE=ON degrades to a CMake warning (CI runs ccache)")
fi

# Qt5 development files: the CI runner has qtbase5-dev. Check the standard
# CMake config locations, then pkg-config as a second signal; no guessing.
QT5_CONFIG=""
for d in "${Qt5_DIR:-}" \
	/usr/lib/x86_64-linux-gnu/cmake/Qt5 \
	/usr/lib/aarch64-linux-gnu/cmake/Qt5 \
	/usr/lib/cmake/Qt5 \
	/usr/local/lib/cmake/Qt5 \
	/opt/Qt/5/gcc_64/lib/cmake/Qt5; do
	if [ -n "$d" ] && [ -f "$d/Qt5Config.cmake" ]; then
		QT5_CONFIG="$d/Qt5Config.cmake"
		break
	fi
done
if [ -z "$QT5_CONFIG" ] && command -v pkg-config >/dev/null 2>&1; then
	if pkg-config --exists Qt5Core 2>/dev/null; then
		QT5_CONFIG="pkg-config:Qt5Core"
	fi
fi

QT_FLAGS=()
if [ -n "$QT5_CONFIG" ]; then
	QT_FLAGS=()   # exact CI configuration: Qt5, no extra flag
else
	QT_FLAGS=(-DWANT_QT6=ON)
	DEVIATIONS+=("Qt5 development files not found: added -DWANT_QT6=ON (CI's runner installs qtbase5-dev; this box has Qt6 only)")
fi

# --- machine context --------------------------------------------------------
HOST_DESC="$(uname -s) $(uname -m), $( (. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME") || echo unknown)"
CC_DESC="$( (g++ --version 2>/dev/null | head -1) || echo 'g++ not found')"
CMAKE_DESC="$(cmake --version 2>/dev/null | head -1)"

echo "=== local-ci: reproducing .github/workflows/build.yml :: linux-x86_64 ==="
echo "machine     : $HOST_DESC"
echo "compiler    : $CC_DESC"
echo "cmake       : $CMAKE_DESC"
echo "ccache      : ${CCACHE_TOOL:-NOT FOUND}"
echo "build dir   : $BUILD_DIR (jobs=$JOBS, ctest -j$CTEST_JOBS)"
echo "CI CMAKE_OPTS: ${CI_CMAKE_OPTS[*]}"
if [ ${#QT_FLAGS[@]} -gt 0 ]; then
	echo "qt flags    : ${QT_FLAGS[*]}"
fi
echo

# --- helpers ----------------------------------------------------------------
tail_log() { [ -f "$1" ] && tail -n "${2:-25}" "$1"; }

RC_CONFIGURE="not run"
RC_BUILD="not run"
RC_CTEST="not run"
CTEST_TOTALS=""
OVERALL=0

# --- step 0: the pinned plugin-hosting dependencies -------------------------
# The workflow runs this as its own step (plus two actions/cache steps) before
# Configure. It is not optional here: the release configuration asks for
# -DWANT_VST3=ON -DWANT_CLAP=ON, and with the checkout absent that is a
# configure FATAL_ERROR rather than the silent STATUS-line skip the published
# alpha shipped. Reproducing that skipped configuration on purpose means
# configuring a scratch tree without these flags (see
# docs/PLUGIN-HOSTING-IN-RELEASE.md).
echo "--- [0/3] provision the pinned VST3 SDK and CLAP headers (the CI step) ---"
if [ -f "$BUILD_DIR/vst3sdk/LICENSE.txt" ] && [ -f "$BUILD_DIR/clap/include/clap/clap.h" ]; then
	echo "  both checkouts already present in $BUILD_DIR; verifying they are the pins"
fi
bash .github/workflows/provision-plugin-hosting-deps.sh "$BUILD_DIR" > "$BUILD_DIR/provision.log" 2>&1
RC_PROVISION=$?
echo "provision EXIT=$RC_PROVISION   (log: $BUILD_DIR/provision.log)"
grep -E "^(VST3 SDK|CLAP|plugin-hosting deps|  VST3 SDK|  CLAP)" "$BUILD_DIR/provision.log" | sed 's/^/  /'
if [ "$RC_PROVISION" -ne 0 ]; then
	echo "--- provisioning failed; last 30 lines: ---"
	tail_log "$BUILD_DIR/provision.log" 30
	echo "local-ci: the release configuration (-DWANT_VST3=ON -DWANT_CLAP=ON) cannot be"
	echo "          configured without these checkouts; this run stops here."
	OVERALL=1
fi

# --- step 1: configure ------------------------------------------------------
if [ "$OVERALL" -eq 0 ]; then
echo "--- [1/3] configure (cmake -S . -B $BUILD_DIR ...) ---"
cmake -S . -B "$BUILD_DIR" "${CI_CMAKE_OPTS[@]}" ${QT_FLAGS[@]+"${QT_FLAGS[@]}"} \
	> "$BUILD_DIR/configure.log" 2>&1
RC_CONFIGURE=$?
echo "configure EXIT=$RC_CONFIGURE   (log: $BUILD_DIR/configure.log)"
if [ "$RC_CONFIGURE" -ne 0 ]; then
	echo "--- configure failed; last 30 lines: ---"
	tail_log "$BUILD_DIR/configure.log" 30
	OVERALL=1
fi
fi   # end: step 1 (skipped when provisioning failed)

# --- step 2: build ----------------------------------------------------------
if [ "$OVERALL" -eq 0 ] && [ "$MODE" != "configure-only" ] && [ "$MODE" != "no-build" ]; then
	echo "--- [2/3] build (cmake --build $BUILD_DIR -j$JOBS) ---"
	cmake --build "$BUILD_DIR" -j "$JOBS" > "$BUILD_DIR/build.log" 2>&1
	RC_BUILD=$?
	echo "build EXIT=$RC_BUILD   (log: $BUILD_DIR/build.log)"
	if [ "$RC_BUILD" -ne 0 ]; then
		echo "--- build failed; last 30 lines: ---"
		tail_log "$BUILD_DIR/build.log" 30
		OVERALL=1
	fi
elif [ "$MODE" = "no-build" ]; then
	echo "--- [2/3] build skipped (--no-build) ---"
elif [ "$MODE" = "configure-only" ]; then
	echo "--- [2/3] build skipped (--configure-only) ---"
fi

# --- step 3: ctest from <build>/tests (never from the top level) ------------
if [ "$OVERALL" -eq 0 ] && [ "$MODE" != "configure-only" ]; then
	if [ ! -f "$BUILD_DIR/tests/CTestTestfile.cmake" ]; then
		echo "--- [3/3] ctest: no CTestTestfile.cmake in $BUILD_DIR/tests — nothing to run (ERROR) ---"
		RC_CTEST="error: no test file"
		OVERALL=1
	else
		echo "--- [3/3] ctest (from $BUILD_DIR/tests, -j$CTEST_JOBS) ---"
		(
			cd "$BUILD_DIR/tests" || exit 2
			ctest --output-on-failure -j "$CTEST_JOBS"
		) > "$BUILD_DIR/ctest.log" 2>&1
		RC_CTEST=$?
		CTEST_TOTALS="$(grep -E 'tests passed.*tests failed out of [0-9]+' "$BUILD_DIR/ctest.log" | tail -1)"
		N_TESTS="$(echo "$CTEST_TOTALS" | sed -n 's/.*out of \([0-9]*\).*/\1/p')"
		echo "ctest EXIT=$RC_CTEST   (log: $BUILD_DIR/ctest.log)"
		[ -n "$CTEST_TOTALS" ] && echo "ctest totals: $CTEST_TOTALS"
		if [ "$RC_CTEST" -ne 0 ]; then
			echo "--- ctest failed; last 30 lines: ---"
			tail_log "$BUILD_DIR/ctest.log" 30
			OVERALL=1
		fi
		if [ -z "$N_TESTS" ] || [ "$N_TESTS" -eq 0 ]; then
			echo "local-ci: ctest reported 0 tests — treated as an ERROR, never a pass"
			OVERALL=1
		fi
	fi
fi

# --- per-job coverage for this machine --------------------------------------
echo
echo "=== job coverage on this machine ==="

case "$(uname -s)" in
	Linux)  IS_LINUX=1 ;;
	Darwin) IS_LINUX=0 ;;
	*)      IS_LINUX=0 ;;
esac
IS_X86_64=0
[ "$(uname -m)" = "x86_64" ] && IS_X86_64=1
MINGW_CC="$(command -v x86_64-w64-mingw32-g++ 2>/dev/null || true)"
AARCH64_CC="$(command -v aarch64-linux-gnu-g++ 2>/dev/null || true)"
CL_CC="$(command -v cl.exe 2>/dev/null || true)"

# linux-x86_64 — the job this script runs.
if [ "$IS_LINUX" = 1 ] && [ "$IS_X86_64" = 1 ]; then
	echo "linux-x86_64: REPRODUCED"
	case "$MODE" in
		full) [ "$RC_BUILD" = "0" ] && [ "$RC_CTEST" = "0" ] \
			&& echo "  run: configure OK, build OK, ctest OK${CTEST_TOTALS:+ ($CTEST_TOTALS)}" \
			|| echo "  run: configure=$RC_CONFIGURE build=$RC_BUILD ctest=$RC_CTEST — a failing step here is a REAL failure of the code, not a machine limit"
			;;
		configure-only) echo "  run: configure=$RC_CONFIGURE (--configure-only; build/ctest not run)" ;;
		no-build)       echo "  run: configure=$RC_CONFIGURE, build skipped, ctest=$RC_CTEST" ;;
	esac
else
	echo "linux-x86_64: NOT-REPRODUCIBLE-HERE (not an x86_64 Linux host)"
fi
for d in ${DEVIATIONS[@]+"${DEVIATIONS[@]}"}; do
	echo "  deviation: $d"
done

# linux-arm64
if [ "$(uname -m)" = "aarch64" ] && [ "$IS_LINUX" = 1 ]; then
	echo "linux-arm64: REPRODUCED"
else
	[ -n "$AARCH64_CC" ] \
		&& echo "linux-arm64: NOT-REPRODUCIBLE-HERE (x86_64 host with no aarch64 cross toolchain; arm64-only diagnostics cannot fire)" \
		|| echo "linux-arm64: NOT-REPRODUCIBLE-HERE (x86_64 host; needs ubuntu-24.04-arm runner or an aarch64 toolchain)"
fi

# mingw64 (cross-build)
if [ -n "$MINGW_CC" ]; then
	echo "mingw64: REPRODUCIBLE-HERE (x86_64-w64-mingw32-g++ found; needs vcpkg manifest + cmake/toolchains/x64-mingw-vcpkg.cmake)"
else
	echo "mingw64: NOT-REPRODUCIBLE-HERE (no x86_64-w64-mingw32 cross toolchain; job uses vcpkg + cmake/toolchains/x64-mingw-vcpkg.cmake)"
fi

# macOS (two jobs)
if [ "$IS_LINUX" = 0 ] && [ "$(uname -s)" = "Darwin" ]; then
	echo "macos-x86_64: REPRODUCIBLE-HERE (Darwin host)"
	echo "macos-arm64: REPRODUCIBLE-HERE (Darwin host)"
else
	echo "macos-x86_64: NOT-REPRODUCIBLE-HERE (Darwin toolchain + Xcode SDK required; Darwin-only libc++/std::filesystem floors cannot be emulated)"
	echo "macos-arm64: NOT-REPRODUCIBLE-HERE (Darwin toolchain + Xcode SDK required; also brew ld search-path defaults)"
fi

# msvc-x64
if [ -n "$CL_CC" ]; then
	echo "msvc-x64: REPRODUCIBLE-HERE (cl.exe found)"
else
	echo "msvc-x64: NOT-REPRODUCIBLE-HERE (MSVC toolchain required: /WX, no libm, import-library semantics)"
fi

# windows-arm64 (msys2 CLANGARM64)
echo "windows-arm64: NOT-REPRODUCIBLE-HERE (Windows 11 ARM64 + msys2 CLANGARM64 required; CPack/NSIS path handling is Windows-specific)"

echo
if [ "$MODE" = "configure-only" ]; then
	echo "local-ci: configure-only mode; overall configure exit=$RC_CONFIGURE"
	[ "$RC_CONFIGURE" -eq 0 ] && exit 0 || exit 1
fi
echo "local-ci: overall exit=$OVERALL (0 = every executed step passed)"
exit "$OVERALL"
