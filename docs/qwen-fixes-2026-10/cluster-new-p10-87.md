# Cluster new-p10-87: Windows clients have no idle timeout

## Verdict: BLOCKED

The Windows read path now leaves the client's absolute deadline unchanged while request bytes remain buffered. It refreshes the deadline after `win32ServeBuffer()` consumes complete lines and leaves an empty buffer, matching the POSIX policy. The existing idle-peer named-pipe test remains, and a second Windows-only test trickles an incomplete line until the peer must be disconnected.

Native verification is blocked by this Linux host: no MinGW or MSVC compiler is installed, and `clang-cl` cannot include `windows.h`. The Linux test executable passes with the product hunk restored and reversed because both named-pipe slots are compiled out under `Q_OS_WIN`. Therefore this run does not establish native RED/GREEN or the requested Windows result on the original code.

## File evidence

- `src/core/ControlServerWin32.cpp:225-243` appends read bytes without refreshing the deadline; `:400-415` refreshes only after request processing leaves `buffer` empty. Reply write progress continues to refresh the deadline.
- `src/core/ControlServer.cpp:335-345` is the POSIX policy: after dispatch, it refreshes only when the buffer is empty.
- `tests/src/core/ControlShutdownHookTest.cpp:215-303` covers an idle named-pipe peer and a client that sends repeated `{` bytes without a newline.

## Evidence

All Linux ctest runs below were issued from `build/tests`. Each reports 1/1 ctest executable passed; its Windows-only named-pipe slots were excluded.

| Check | Exact command | Result |
| --- | --- | --- |
| Final affected-target build, product fix restored | `cmake --build build --target zene ControlShutdownHookTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r3/logs/build-7.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 0 |
| Final focused test, product fix restored | `ctest -R '^ControlShutdownHookTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r3/logs/test-7.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 0; 1/1 ctest executable |
| Reverse only the current product-file hunks | `git diff -- src/core/ControlServerWin32.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r3/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r3/logs/fix.patch; status=$?; echo EXIT=$status; exit $status` | EXIT 0 |
| Reversed-hunk Linux build | `cmake --build build --target zene ControlShutdownHookTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r3/logs/build-6.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 0 |
| Reversed-hunk Linux test (not a RED result) | `ctest -R '^ControlShutdownHookTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r3/logs/test-6.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 0; 1/1 ctest executable; Windows slots excluded |
| Restore product hunk | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r3/logs/fix.patch; status=$?; echo EXIT=$status; exit $status` | EXIT 0 |
| Native Windows RED on original code | Not run | N/A; no Windows-capable compiler/SDK on this host |
| Native Windows test with only product hunks reversed | Not run | N/A; no Windows-capable compiler/SDK on this host |
| Native Windows GREEN with fix | Not run | N/A; no Windows-capable compiler/SDK on this host |
| Host platform | `uname -s > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r3/logs/platform-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 0; Linux |
| MinGW compiler probe | `command -v x86_64-w64-mingw32-g++ > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r3/logs/toolchain-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 1; unavailable |
| MSVC compiler probe | `command -v cl > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r3/logs/toolchain-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 1; unavailable |
| Windows SDK header probe | `clang-cl /nologo /c /TP /Fo:build/windows-sdk-probe.obj build/windows-sdk-probe.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r3/logs/windows-sdk-probe-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 1; `windows.h` unavailable |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r3/logs/gate-7.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 1; six unrelated new CCN regressions |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r3/logs/gate-8.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 1; fourteen unrelated files over 500 lines |

The Windows-specific RED command and exit code cannot be reported because it was not run. Linux reversal and restored-state tests both pass, which confirms only that the Linux test binary does not execute these Windows-only cases. `fff` grep was not exposed in this session; logs were searched with `rg` as the available fallback.

## Gate findings

The final runs return EXIT 1 for both checks. Complexity reports six new CCN regressions outside the changed files: `dotCentreAlong`, `parseNotes`, `adjust`, `setStrip`, `rotateProjectRevision`, and `importedOverrides`. File-length reports fourteen unrelated files over 500 lines: `src/core/SmfInterchangeReader.cpp`, `src/core/TakeLane.cpp`, `tests/src/core/CollabTransportTest.cpp`, `ControlAutomationScriptTest.cpp`, `ControlDeviceCatalogueTest.cpp`, `ControlEditCommandsTest.cpp`, `CustomAccessTest.cpp`, `DeviceDspTest.cpp`, `HardwareIoTest.cpp`, `NotePlaybackTest.cpp`, `RackMacrosTest.cpp`, `RevisionTimelineTest.cpp`, `VideoTrackTest.cpp`, and `tests/src/gui/ArrangerTimelineTest.cpp`. `ControlShutdownHookTest.cpp` is no longer among the over-limit files. No gate was changed or weakened.

## Files committed

- `src/core/ControlServerWin32.cpp`
- `tests/src/core/ControlShutdownHookTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-87.md`

The source and test files are already registered in `tests/fork-sources.txt`; no registration change is needed.

## Round 4 QA rework

Verdict remains **BLOCKED**. QA requires a Windows-capable build for the candidate, the reversed-product-hunk RED run, and the restored GREEN run. This worktree has only the existing native Linux build tree (`CMAKE_CXX_COMPILER=/usr/bin/c++`, `CMAKE_GENERATOR=Unix Makefiles`), and this host reports Linux. MinGW and MSVC are unavailable; `clang-cl` is installed but cannot find `windows.h`. Wine is installed, but it does not supply a Windows compiler or SDK. I did not run or count a Windows build or test, and did not reverse the product hunk because this host cannot execute the requested Windows verification.

| Check | Exact command | Result |
| --- | --- | --- |
| Host | `uname -s > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r4/logs/platform-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 0; `Linux` |
| MinGW compiler | `command -v x86_64-w64-mingw32-g++ > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r4/logs/toolchain-3.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 1; unavailable |
| MSVC compiler | `command -v cl > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r4/logs/toolchain-4.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 1; unavailable |
| `clang-cl` presence | `command -v clang-cl > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r4/logs/toolchain-5.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 0; `/usr/bin/clang-cl` |
| Windows SDK header probe | `clang-cl /nologo /c /TP /Fo:build/windows-sdk-probe.obj build/windows-sdk-probe.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r4/logs/windows-sdk-probe-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 1; `windows.h` not found |
| Wine runtime | `wine --version > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r4/logs/wine-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 0; Wine is present, but no Windows toolchain/SDK is available |
| Windows candidate build (`zene ControlShutdownHookTest`) | Not run; the only configured `build` tree is native Linux | N/A; no Windows build result |
| Windows focused ctest (`^ControlShutdownHookTest$`) | Not run; no Windows test executable can be built here | N/A; zero Windows tests executed; no Windows test count |
| Reversed-hunk Windows build and RED test | Not run; no Windows-capable build/runtime toolchain | N/A; no RED result |
| Restored-hunk Windows build and GREEN test | Not run; no Windows-capable build/runtime toolchain | N/A; no GREEN result |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r4/logs/gate-11.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 1; six new CCN regressions outside the changed files |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r4/logs/gate-12.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 1; fourteen over-limit files outside the changed files |

The `fff` grep tool was not available in this session, so the new logs were searched with `rg` as a fallback. The Windows header failure is in `windows-sdk-probe-2.log`; the gate findings are in `gate-11.log` and `gate-12.log`. No product or test file was changed in this round. The report is the only file committed for this round: `docs/qwen-fixes-2026-10/cluster-new-p10-87.md`.
