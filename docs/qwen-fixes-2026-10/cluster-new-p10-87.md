# Cluster new-p10-87: Windows clients have no idle timeout

## Verdict: BLOCKED

The finding is real. Windows overlapped client reads and writes now use a per-client absolute idle deadline based on `ControlServer::ClientIdleTimeoutMs`. Successful byte transfers refresh that deadline. When a pending operation reaches it, `waitForIo()` calls `CancelIoEx()` and waits with `GetOverlappedResult(..., TRUE)` before returning, so the client loop can disconnect and release its operation state safely.

A Windows-only named-pipe regression was added to `ControlShutdownHookTest`: it connects, leaves a read pending, and requires the server to disconnect the idle peer. The host is Linux and has no Windows SDK, so this slot is compiled out here. I cannot claim the required native RED/GREEN/reversed-patch proof, and the verdict remains BLOCKED pending a Windows-capable run.

### File evidence

- `include/ControlServer.h:223` passes the client deadline through the Windows response path; `:93` defines the 30-second timeout.
- `src/core/ControlServerWin32.cpp:141-174` bounds overlapped waits and computes remaining deadline time; `:178-240` applies it to writes and reads; `:384-463` owns and propagates one deadline per client and disconnects on expiry.
- `tests/src/core/ControlShutdownHookTest.cpp:132-150,188-239` connects to a named pipe and asserts that a pending client read completes with a disconnect after the idle timeout.

### Evidence

| Check | Exact command | Result |
| --- | --- | --- |
| Pre-fix test-target build (Linux; Windows slot excluded) | `cmake --build build --target ControlShutdownHookTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r2/logs/build-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 0 |
| Pre-fix focused ctest (Linux; not a Windows RED result) | `ctest -R '^ControlShutdownHookTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r2/logs/test-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` (cwd: `build/tests`) | EXIT 0; 1/1 ctest executable |
| Post-fix affected-target build (before final Windows-only test timing assertion edit) | `cmake --build build --target zene ControlShutdownHookTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r2/logs/build-3.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 0; both targets built |
| Post-fix focused ctest (Linux; Windows slot excluded) | `ctest -R '^ControlShutdownHookTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r2/logs/test-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` (cwd: `build/tests`) | EXIT 0; 1/1 ctest executable |
| Rebuild after the final Windows-only test timing assertion edit | `cmake --build build --target zene ControlShutdownHookTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r2/logs/build-4.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 143; canceled after 8 minutes queued behind the shared build-slot guard; no compilation started |
| Native named-pipe RED on original product code | Not run | No Windows-capable build on this host |
| Native named-pipe GREEN after fix | Not run | No Windows-capable build on this host |
| Reversed product-patch RED proof | Not run | No Windows-capable build on this host |
| Complexity ratchet | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r2/logs/gate-3.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 1; six unrelated new CCN regressions, none in changed files |
| File-length ratchet | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r2/logs/gate-4.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 1; fourteen unrelated over-limit files, none in changed files |
| Windows SDK probe | `printf '#include <windows.h>\n' > build/windows-sdk-probe.cpp; clang-cl /nologo /c /TP /Fo:build/windows-sdk-probe.obj build/windows-sdk-probe.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r2/logs/windows-sdk-probe-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 1; `windows.h` not found |
| MinGW availability | `command -v x86_64-w64-mingw32-g++ > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r2/logs/toolchain-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 1; compiler absent |
| Host platform | `uname -s > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r2/logs/platform-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 0; `Linux` |
| `fff` availability | `command -v fff > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r2/logs/toolchain-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | EXIT 1; unavailable in this session; logs were searched with `rg` |

An initial queued duplicate pre-fix build ran `cmake --build build --target ControlShutdownHookTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-87.r2/logs/build-1.log 2>&1; echo EXIT=$?` and wrote `Built target ControlShutdownHookTest`, but the wrapper did not surface its shell exit code. I reran it as `build-2.log` and rely on the captured EXIT 0 above.

### Files in this commit

- `include/ControlServer.h`
- `src/core/ControlServerWin32.cpp`
- `tests/src/core/ControlShutdownHookTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-87.md`

The changed source and test files were already registered in `tests/fork-sources.txt`; no new source file was added.
