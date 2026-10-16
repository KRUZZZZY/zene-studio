# Cluster new-p10-85

**Verdict: BLOCKED**

## Finding evidence

The finding is real in the source. [ControlServer.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-85/include/ControlServer.h:91) declares a 64-peer `MaxClients` limit. In [ControlServerWin32.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-85/src/core/ControlServerWin32.cpp:351), the connected-pipe path unconditionally appends a new client thread; the Windows accept loop has no cap check. The POSIX accept loop has its own check, so its existing live-socket test cannot prove this Windows behavior.

## Why blocked

This worktree is configured and built for Linux (`uname` reports Linux and CMake uses `/usr/bin/c++`). `ControlNamedPipeSmoke` is registered only under `IF(WIN32 AND PYTHON3_EXECUTABLE)` in `tests/CMakeLists.txt:3941`, and no MinGW compiler is available. The only related CTest entry in this build is `ControlShutdownHookTest`, which tests shutdown-hook/socket lifecycle, not concurrent named-pipe clients.

I did not add a source-text assertion or treat the POSIX socket cap as a regression: neither exercises the user-visible Windows pipe path. Without a Windows build and runner, I cannot produce the required behavioral RED/GREEN evidence or verify a Windows-only production change. No production or test code was changed.

## Verification evidence

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| RED regression | Not run | N/A | No Windows pipe regression can execute in this Linux build. |
| GREEN regression | Not run | N/A | No production fix was applied. |
| Affected-family tests | Not run | N/A | No Windows test was run; `ControlNamedPipeSmoke` is not registered in this build. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r1/logs/gate-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 1 | 6 unrelated complexity regressions; baseline not written. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r1/logs/gate-6.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 1 | 14 unrelated new over-limit source entries; baseline not written. |

Inspection commands and observed exits:

- `uname -a; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0.
- `rg -n 'CMAKE_SYSTEM_NAME|CMAKE_CXX_COMPILER:FILEPATH|CMAKE_GENERATOR:' build/CMakeCache.txt; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0; the cache shows `/usr/bin/c++` and Unix Makefiles.
- `rg -n -C 3 'MaxClients =|m_win32ClientThreads.emplace_back|void ControlServer::win32ClientLoop' include/ControlServer.h src/core/ControlServerWin32.cpp; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0; evidence above.
- `rg -n -C 2 'IF\(WIN32 AND PYTHON3_EXECUTABLE\)|add_test\(NAME ControlNamedPipeSmoke' tests/CMakeLists.txt; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0; Windows-only registration at line 3941.
- `rg -n 'ControlNamedPipeSmoke|ControlShutdownHookTest' build/tests/CTestTestfile.cmake; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0; only `ControlShutdownHookTest` is present.
- `command -v x86_64-w64-mingw32-g++; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=1; no MinGW compiler found.

The required gates were run before any product or test-code change. The complexity log names regressions in `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, and `ControlCommandsShortcuts.cpp`. The file-length log names 14 committed source files missing from its current over-limit baseline. I did not alter unrelated code or re-anchor either gate.

Build and CTest commands were not run because this host cannot build or execute the Windows named-pipe transport. No zero-test CTest invocation was made.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-85.md` (this blocked report only)
