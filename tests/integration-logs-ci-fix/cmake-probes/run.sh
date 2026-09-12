#!/usr/bin/env bash
# run.sh -- reproduce, and re-check the fix for, the two Windows configure
# failures of the v0.2.0-alpha tag WITHOUT a Windows machine.
#
# Both failures are platform *inputs* to platform-independent CMake code, so
# each can be reproduced here by supplying those inputs to the real module:
#
#   clap-before  the real pre-fix cmake/modules/ClapHeaders.cmake, run with the
#                MinGW toolchain's find policy (CMAKE_FIND_ROOT_PATH_MODE_INCLUDE
#                = ONLY, set by cmake/toolchains/common/mingw-vcpkg.cmake:48) and
#                a genuine CLAP checkout sitting at the path the module looks in
#                -> must FAIL at ClapHeaders.cmake:45, as the mingw64 job did
#   clap-after   the fixed module, same policy, same checkout
#                -> must find CLAP 1.2.10 and configure
#   tgt-lmms     cmake/modules/InstallTargetDependencies.cmake called with
#                TARGETS lmms (what cmake/install/CMakeLists.txt:28 passed)
#                -> must FAIL with "Not a target: lmms" at line 37, as msvc-x64
#                and windows-arm64 did
#   tgt-zene     the same module called with TARGETS zene (the renamed
#                executable target) -> must configure
#
# The probe projects are generated into ./out/gen/ rather than committed. The
# pre-fix ClapHeaders.cmake is likewise extracted from git on each run.
#
# SCOPE: this proves the mechanism of each error and that the fix addresses it.
# That the Windows jobs really reach these statements with these arguments is
# shown by their own logs (see ../msvc-x64.clean.log and
# ../windows-arm64.clean.log); the fix itself is confirmed only by the next
# Windows CI run, because no Windows compiler exists on this box.
#
# Exit 0 only when all four expectations hold.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKTREE="$(cd "$HERE/../../.." && pwd)"
GEN="$HERE/out/gen"
mkdir -p "$GEN/clap" "$GEN/target-check" "$GEN/before" "$GEN/fake-sysroot-1" "$GEN/fake-sysroot-2"

CLAP_CHECKOUT="$WORKTREE/build/clap"
ROOT_PATHS="$GEN/fake-sysroot-1;$GEN/fake-sysroot-2"
failures=0

step() { # step <label> <expected> <actual> <ok>
	printf '%-12s %-46s %-20s %s\n' "$1" "$2" "$3" "$4"
	[[ "$4" == "ok" ]] || failures=$((failures + 1))
}

[[ -f "$CLAP_CHECKOUT/include/clap/clap.h" ]] || {
	echo "error: no CLAP checkout at $CLAP_CHECKOUT" >&2
	echo "       run: bash .github/workflows/provision-plugin-hosting-deps.sh build" >&2
	exit 2
}

cat > "$GEN/clap/CMakeLists.txt" <<'CLAP_PROBE_EOF'
# Runs the REAL cmake/modules/ClapHeaders.cmake with the MinGW toolchain's
# find-path policy, to reproduce the mingw64 configure error without a cross
# compiler: find_path re-roots its HINTS under CMAKE_FIND_ROOT_PATH when
# CMAKE_FIND_ROOT_PATH_MODE_INCLUDE is ONLY (cmake/toolchains/common/
# mingw-vcpkg.cmake:48), so a CLAP checkout inside the build tree is invisible.
cmake_minimum_required(VERSION 3.16)
project(clap_headers_probe NONE)

if(NOT DEFINED CLAP_HEADERS_CMAKE)
	message(FATAL_ERROR "pass -DCLAP_HEADERS_CMAKE=<file>")
endif()

include("${CLAP_HEADERS_CMAKE}")
message(STATUS "PROBE: LMMS_CLAP_INCLUDE_DIR=${LMMS_CLAP_INCLUDE_DIR}")
CLAP_PROBE_EOF

cat > "$GEN/target-check/CMakeLists.txt" <<'TGT_PROBE_EOF'
# Runs the REAL cmake/modules/InstallTargetDependencies.cmake against a target
# list, to reproduce the Windows configure error
#   CMake Error at cmake/modules/InstallTargetDependencies.cmake:37 (message):
#     Not a target: lmms
# without a Windows compiler: the failing statement is the IF(NOT TARGET ...)
# guard at the top of its FOREACH, which is platform independent. cmake/install/
# CMakeLists.txt:28 passed "lmms"; the renamed executable target is "zene".
cmake_minimum_required(VERSION 3.16)
project(install_target_deps_probe NONE)

if(NOT DEFINED REPO_CMAKE_MODULE_PATH)
	message(FATAL_ERROR "pass -DREPO_CMAKE_MODULE_PATH=<repo>/cmake/modules")
endif()
set(CMAKE_MODULE_PATH "${REPO_CMAKE_MODULE_PATH}")
set(BIN_DIR "bin")
set(LIB_DIR "lib")
set(LMMS_INSTALL_DEPENDENCIES ON)

# the renamed executable target, as src/CMakeLists.txt:150 creates it
add_executable(zene IMPORTED GLOBAL)

include(InstallTargetDependencies)

if(NOT DEFINED WHICH)
	message(FATAL_ERROR "pass -DWHICH=<target>")
endif()

INSTALL_TARGET_DEPENDENCIES(
	NAME "main_binary"
	TARGETS ${WHICH}
	DESTINATION "bin"
	LIB_DIRS "lib"
)
message(STATUS "PROBE: accepted TARGETS ${WHICH}")
TGT_PROBE_EOF

# the pre-fix module (its own copy, so the red case stays reproducible)
git -C "$WORKTREE" show post-alpha/integration:cmake/modules/ClapHeaders.cmake \
	> "$GEN/before/ClapHeaders.cmake" || { echo "error: cannot extract the pre-fix ClapHeaders.cmake" >&2; exit 2; }

# --- CLAP, pre-fix ---------------------------------------------------------
cmake -S "$GEN/clap" -B "$GEN/clap-before" \
	-DCLAP_HEADERS_CMAKE="$GEN/before/ClapHeaders.cmake" \
	-DLMMS_CLAP_PATH="$CLAP_CHECKOUT" \
	-DCMAKE_FIND_ROOT_PATH="$ROOT_PATHS" \
	-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY > "$GEN/clap-before.log" 2>&1
rc=$?
if [[ $rc -eq 0 ]]; then
	step clap-before "fails at ClapHeaders.cmake:45" "configure exit 0" "FAIL: pre-fix module configured"
elif grep -q "ClapHeaders.cmake:45" "$GEN/clap-before.log"; then
	step clap-before "fails at ClapHeaders.cmake:45" "exit $rc, line 45" "ok"
else
	step clap-before "fails at ClapHeaders.cmake:45" "exit $rc, other error" "FAIL: wrong error"
fi

# --- CLAP, fixed -----------------------------------------------------------
cmake -S "$GEN/clap" -B "$GEN/clap-after" \
	-DCLAP_HEADERS_CMAKE="$WORKTREE/cmake/modules/ClapHeaders.cmake" \
	-DLMMS_CLAP_PATH="$CLAP_CHECKOUT" \
	-DCMAKE_FIND_ROOT_PATH="$ROOT_PATHS" \
	-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY > "$GEN/clap-after.log" 2>&1
rc=$?
if [[ $rc -eq 0 ]] && grep -q "Found CLAP 1.2.10" "$GEN/clap-after.log"; then
	step clap-after "finds CLAP 1.2.10" "exit 0" "ok"
else
	step clap-after "finds CLAP 1.2.10" "exit $rc" "FAIL"
fi

# --- TARGETS lmms ----------------------------------------------------------
cmake -S "$GEN/target-check" -B "$GEN/tgt-lmms" -DWHICH=lmms \
	-DLMMS_SOURCE_DIR="$WORKTREE" \
	-DREPO_CMAKE_MODULE_PATH="$WORKTREE/cmake/modules" > "$GEN/tgt-lmms.log" 2>&1
rc=$?
if [[ $rc -eq 0 ]]; then
	step tgt-lmms "fails: Not a target: lmms" "configure exit 0" "FAIL: lmms accepted"
elif grep -q "Not a target: lmms" "$GEN/tgt-lmms.log"; then
	step tgt-lmms "fails: Not a target: lmms" "exit $rc" "ok"
else
	step tgt-lmms "fails: Not a target: lmms" "exit $rc, other error" "FAIL: wrong error"
fi

# --- TARGETS zene ----------------------------------------------------------
cmake -S "$GEN/target-check" -B "$GEN/tgt-zene" -DWHICH=zene \
	-DLMMS_SOURCE_DIR="$WORKTREE" \
	-DREPO_CMAKE_MODULE_PATH="$WORKTREE/cmake/modules" > "$GEN/tgt-zene.log" 2>&1
rc=$?
if [[ $rc -eq 0 ]] && grep -q "accepted TARGETS zene" "$GEN/tgt-zene.log"; then
	step tgt-zene "accepts the renamed target" "exit 0" "ok"
else
	step tgt-zene "accepts the renamed target" "exit $rc" "FAIL"
fi

echo
if [[ $failures -eq 0 ]]; then
	echo "PASS: both Windows configure causes reproduced, both fixes effective"
	exit 0
fi
echo "FAIL: $failures expectation(s) unmet"
exit 1
