# Cluster new-p10-87: Windows clients have no idle timeout

## Verdict: BLOCKED

The finding is real. `include/ControlServer.h:93` already defines the 30-second `ClientIdleTimeoutMs`, but the Windows client path does not use it. `src/core/ControlServerWin32.cpp:139-149` waits on the pipe operation and server quit event with `WaitForMultipleObjects(..., INFINITE)`. The pending read at `src/core/ControlServerWin32.cpp:201-212` uses that wait, and `win32ClientLoop()` calls `readChunk()` at line 375 without tracking an idle deadline. A connected peer that sends nothing therefore remains in the read wait until server shutdown.

I did not change production code. The required regression must exercise the Windows named-pipe path and fail before the fix, then pass after it and after reverting/reapplying the product patch. This host is Linux, the configured build uses `/usr/bin/c++`, and `x86_64-w64-mingw32-g++` is unavailable. The Linux test binary does not compile the `Q_OS_WIN` transport. Wine is present, but there is no Windows build to run. A POSIX test or source-pattern assertion would not prove the reported Windows behavior, so the regression, RED/GREEN run, and revert proof are not claimed.

The two required ratchet gates also fail on this initially clean worktree, on unrelated existing regressions. The complexity gate reports six new over-target functions in other files; the file-length gate reports fourteen new over-limit files. I did not change either baseline or those unrelated files.

| Evidence | Command/result |
| --- | --- |
| Source path | `include/ControlServer.h:93`; `src/core/ControlServerWin32.cpp:139-149, 201-212, 356-375` |
| Windows toolchain | `command -v x86_64-w64-mingw32-g++` — exit 1 (not installed); `uname -s` — exit 0 (`Linux`) |
| RED regression | Not run; no Windows named-pipe test binary/toolchain. |
| GREEN regression and revert proof | Not run; no Windows named-pipe test binary/toolchain. |
| Affected-family test count | 0 executed. No `ctest` command was run. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r1/logs/gate-1.log 2>&1; echo EXIT=$?` — gate exit 1. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r1/logs/gate-2.log 2>&1; echo EXIT=$?` — gate exit 1. |
| Files in this report-only commit | `docs/qwen-fixes-2026-10/cluster-new-p10-87.md` |

The gate logs are `gate-1.log` and `gate-2.log` in the run log folder above. The available `fff grep` tool was not exposed in this session; targeted `rg` searches found the gate regression lines recorded here.
