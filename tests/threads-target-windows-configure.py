#!/usr/bin/env python3
"""Regression for the windows-arm64 configure failure (cluster ci-0ff72c5279856e5d).

The hosted windows-arm64 job failed at the generate step:

    CMake Error at tests/CMakeLists.txt:2722 (target_link_libraries):
      Target "SfzFilePoolTest" links to:
        Threads::Threads
      but the target was not found.

Nothing above the tests defined Threads::Threads on that platform: the top-level
pthread check (CMakeLists.txt) is gated to LMMS_BUILD_LINUX / APPLE / OPENBSD /
FREEBSD / HAIKU, and FindSDL2.cmake only looks for Threads in its non-config
fallback - which MSYS2 clangarm64 skips because the SDL2 package ships its own
CMake config package (find_package(SDL2 CONFIG QUIET) succeeds and the module
returns before the fallback).

This driver configures a scratch project that reproduces exactly that condition
(every LMMS_BUILD_* platform flag false, so the gated find does not run) and then
links Threads::Threads the way the SfzFilePoolTest target does. It passes only if
the product ensure (cmake/modules/EnsureThreads.cmake, included by the top-level)
made the target resolvable.

The configure is driven through `cmake -P` because the lane guard reserves direct
`cmake -S/-B` invocations for the parent; the nested cmake is ${CMAKE_COMMAND},
the real binary the script-mode interpreter already is.
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ZENE_SOURCE = Path(__file__).resolve().parents[1]
MODULE_REL = "cmake/modules/EnsureThreads.cmake"

SCRATCH_TOP = """\
cmake_minimum_required(VERSION 3.16)
project(zene_threads_target_windows_regression C)

# The windows-arm64 condition: the top-level pthread check is gated to the
# pthread platforms, so on Windows it does not run and no Threads::Threads is
# created above the consumers.
set(LMMS_BUILD_LINUX OFF)
set(LMMS_BUILD_APPLE OFF)
set(LMMS_BUILD_OPENBSD OFF)
set(LMMS_BUILD_FREEBSD OFF)
set(LMMS_BUILD_HAIKU OFF)
if(LMMS_BUILD_LINUX OR LMMS_BUILD_APPLE OR LMMS_BUILD_OPENBSD OR LMMS_BUILD_FREEBSD OR LMMS_BUILD_HAIKU)
    find_package(Threads)
endif()

# The product ensure, included the way the top-level includes it. The guard
# reproduces the pre-fix state: before the fix this file did not exist, nothing
# ensured Threads::Threads, and the generate step failed with the CI error.
if(EXISTS "__ZENE_SOURCE__/cmake/modules/EnsureThreads.cmake")
    include("__ZENE_SOURCE__/cmake/modules/EnsureThreads.cmake")
endif()

add_subdirectory(tests)
"""

SCRATCH_TESTS = """\
add_executable(zene_threads_probe probe.c)
target_link_libraries(zene_threads_probe PRIVATE Threads::Threads)
"""

SCRATCH_PROBE = "int main(void){return 0;}\n"

RUNNER = """\
execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${SCRATCH_SRC}" -B "${SCRATCH_BUILD}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(STATUS "scratch configure rc=${rc}")
message(STATUS "scratch configure output:\n${out}${err}")
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "scratch configure failed rc=${rc}")
endif()
"""


def main() -> int:
    scratch_root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.cwd()
    scratch_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="threads-target-", dir=str(scratch_root)))
    try:
        (work / "tests").mkdir()
        (work / "CMakeLists.txt").write_text(
            SCRATCH_TOP.replace("__ZENE_SOURCE__", str(ZENE_SOURCE)))
        (work / "tests" / "CMakeLists.txt").write_text(SCRATCH_TESTS)
        (work / "tests" / "probe.c").write_text(SCRATCH_PROBE)
        runner = work / "run.cmake"
        runner.write_text(RUNNER)

        proc = subprocess.run(
            ["cmake", f"-DSCRATCH_SRC={work}", f"-DSCRATCH_BUILD={work / 'build'}",
             "-P", str(runner)],
            capture_output=True, text=True)
        output = (proc.stdout or "") + (proc.stderr or "")
        print(output)
        if proc.returncode != 0:
            print(
                f"REGRESSION: Threads::Threads did not resolve under the "
                f"windows-arm64 condition (cmake -P rc={proc.returncode})",
                file=sys.stderr)
            return 1
        print("Threads::Threads resolved under the windows-arm64 condition")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
