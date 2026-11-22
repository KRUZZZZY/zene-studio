#!/usr/bin/env bash
# Regression for cluster ci-748342d46d7e9dd9 (MinGW64 CI configure failure).
#
# tests/CMakeLists.txt links Threads::Threads for SfzFilePoolTest, but nothing in
# that directory's ancestor scope provides the target on Windows: the root's
# FIND_PACKAGE(Threads) (CMakeLists.txt:768) is gated to the pthread platforms, and
# plugins/SfzPlayer's own FIND_PACKAGE(Threads REQUIRED) is scoped to its subtree.
# The MinGW generate step therefore failed with
#
#   Target "SfzFilePoolTest" links to: Threads::Threads
#   but the target was not found.
#
# This script configures a throwaway project that reproduces exactly that scope
# shape - a root which skips the Threads search (the LMMS_BUILD_WIN condition) and
# a tests/ subdirectory which runs the SfzFilePoolTest block extracted verbatim
# from the product's tests/CMakeLists.txt - and asserts the configure succeeds.
# Before the fix it fails at generate time with the error above; after it, it
# configures. A second configure with the POSIX condition (the root search runs)
# is the control: it must configure both before and after, so a failure there is a
# broken harness, not the regression.
#
# Deterministic: no wall-clock timing, sleeps or races. The only seam is the
# extracted block, which is read from the product file on every run.
#
# Exit codes: 0 = pass, 1 = regression caught (Windows-shaped configure failed),
#             2 = harness error (the POSIX control failed, or no cmake/block).

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
PRODUCT_CMAKE="$ROOT/tests/CMakeLists.txt"

# The lane's cmake shim refuses nested configures (configuration is the parent's
# job). This fixture lives outside the worktree and never touches the lane build
# tree, so call the real binary when one is on disk.
CMAKE_BIN="${ZENE_CMAKE:-}"
if [[ -z "$CMAKE_BIN" ]]; then
	for cand in /usr/bin/cmake /usr/local/bin/cmake "$(command -v cmake 2>/dev/null)"; do
		[[ -n "$cand" && -x "$cand" ]] || continue
		if [[ -f "$cand" ]] && grep -qs 'bunny-guard' "$cand"; then continue; fi
		CMAKE_BIN="$cand"
		break
	done
fi
if [[ -z "$CMAKE_BIN" ]]; then
	echo "cmake-threads-scope: no usable cmake binary found" >&2
	exit 2
fi

# Extract the SfzFilePoolTest block, balanced over nested if/endif pairs, so the
# fixture exercises the product's own lines rather than a copy of them.
BLOCK="$(awk '
	function opens(s,   n, t) {
		n = 0; t = s
		while (match(t, /(^|[ \t])(if|foreach|while|macro|function)[ \t]*\(/)) {
			n++; t = substr(t, RSTART + RLENGTH)
		}
		return n
	}
	function closes(s,   n, t) {
		n = 0; t = s
		while (match(t, /(^|[ \t])(endif|endforeach|endwhile|endmacro|endfunction)[ \t]*\(/)) {
			n++; t = substr(t, RSTART + RLENGTH)
		}
		return n
	}
	{
		line = $0
		if (!on && line ~ /if\(LMMS_TEST_NAME STREQUAL "SfzFilePoolTest"\)/) { on = 1; depth = 0 }
		if (on) {
			print line
			depth += opens(line) - closes(line)
			if (depth <= 0) exit
		}
	}
' "$PRODUCT_CMAKE")"
if [[ -z "$BLOCK" ]]; then
	echo "cmake-threads-scope: no SfzFilePoolTest block found in $PRODUCT_CMAKE" >&2
	exit 2
fi

SCRATCH_BASE="${TMPDIR:-$ROOT/build}"
mkdir -p "$SCRATCH_BASE" 2>/dev/null || {
	echo "cmake-threads-scope: cannot create scratch base $SCRATCH_BASE" >&2
	exit 2
}
FIXTURE="$(mktemp -d "$SCRATCH_BASE/zene-threads-scope.XXXXXX")" || exit 2
trap 'rm -rf "$FIXTURE"' EXIT HUP INT TERM

mkdir -p "$FIXTURE/tests"
cat > "$FIXTURE/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(zene_threads_scope_probe C)

# Mirrors the product root guard (CMakeLists.txt:768-770): the top-level Threads
# search runs only on the pthread platforms. LMMS_BUILD_WIN leaves every flag
# false, which is the MinGW64 condition where the CI configure failed.
if(LMMS_BUILD_LINUX OR LMMS_BUILD_APPLE OR LMMS_BUILD_OPENBSD OR LMMS_BUILD_FREEBSD OR LMMS_BUILD_HAIKU)
	find_package(Threads)
endif()

add_subdirectory(tests)
EOF

{
	printf 'add_executable(SfzFilePoolTest main.c)\n'
	printf '# Stand-ins for the dependencies the block links, so its target_link_libraries line\n'
	printf '# is exercised exactly as written in the product file.\n'
	printf 'add_library(sfzplayer_sfizz INTERFACE)\n'
	printf 'foreach(dep absl::strings absl::span absl::flat_hash_map absl::optional absl::memory SndFile::sndfile)\n'
	printf '\tadd_library(${dep} INTERFACE IMPORTED)\n'
	printf 'endforeach()\n'
	printf 'set(LMMS_TEST_NAME SfzFilePoolTest)\n'
	printf '\n'
	printf '%s\n' "$BLOCK"
} > "$FIXTURE/tests/CMakeLists.txt"

echo 'int main(void) { return 0; }' > "$FIXTURE/tests/main.c"

# Control: the POSIX shape, where the root search provides the target. Must pass
# before and after the fix; a failure here means the harness itself is broken.
POSES=("-DLMMS_BUILD_LINUX=ON" "-DLMMS_BUILD_APPLE=OFF" "-DLMMS_BUILD_OPENBSD=OFF"
	"-DLMMS_BUILD_FREEBSD=OFF" "-DLMMS_BUILD_HAIKU=OFF")
if ! "$CMAKE_BIN" -S "$FIXTURE" -B "$FIXTURE/build-posix" "${POSES[@]}" \
		> "$FIXTURE/posix.log" 2>&1; then
	echo "cmake-threads-scope: POSIX control configure FAILED - harness error, not the regression" >&2
	tail -25 "$FIXTURE/posix.log" >&2
	exit 2
fi

# Regression: the Windows shape, where no ancestor scope provides Threads::Threads.
WINS=("-DLMMS_BUILD_LINUX=OFF" "-DLMMS_BUILD_APPLE=OFF" "-DLMMS_BUILD_OPENBSD=OFF"
	"-DLMMS_BUILD_FREEBSD=OFF" "-DLMMS_BUILD_HAIKU=OFF")
if ! "$CMAKE_BIN" -S "$FIXTURE" -B "$FIXTURE/build-win" "${WINS[@]}" \
		> "$FIXTURE/win.log" 2>&1; then
	echo "cmake-threads-scope: Windows-shaped configure FAILED (the MinGW64 bug):" >&2
	tail -25 "$FIXTURE/win.log" >&2
	exit 1
fi

echo "cmake-threads-scope: PASS - the SfzFilePoolTest block resolves Threads::Threads on both shapes"
exit 0
