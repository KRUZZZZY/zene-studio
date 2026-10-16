# Cluster new-p10-85

**Verdict: BLOCKED**

## Finding and change

The finding is real. `include/ControlServer.h:92` declares `MaxClients = 64`. The Windows accept loop in `src/core/ControlServerWin32.cpp:366-382` now reaps completed workers before checking that limit, closes a newly accepted pipe when all 64 client slots remain active, and starts a worker only when a slot is available. `include/ControlServer.h:241-248` stores a completion flag with each worker; `src/core/ControlServerWin32.cpp:275-289` joins and removes completed workers. The worker list therefore stays bounded instead of retaining every historical connection. `closeWin32()` still joins all remaining workers at `src/core/ControlServerWin32.cpp:515-519`.

`tests/src/core/ControlShutdownHookTest.cpp:230-282` adds a Windows-only regression that holds 64 pipe clients open, expects an excess client's pipe to close, closes one allowed client, then retries until a replacement receives a reply. The case skips on POSIX because that build does not include the Windows named-pipe transport.

## Verification evidence

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| Pre-fix regression preflight | `ctest -R '^ControlShutdownHookTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r2/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r2/logs/test-1.log; exit $rc` | 0 | Not RED: this Linux run skipped `windowsCapsActivePipeClientsAndReusesFreedSlot()`. |
| Final build | `cmake --build build --target zene ControlShutdownHookTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r2/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r2/logs/build-3.log; exit $rc` | 0 | `zene` and the affected test target built in the configured Linux tree. |
| Final targeted CTest | `ctest -R '^ControlShutdownHookTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r2/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r2/logs/test-3.log; exit $rc` | 0 | 1/1 CTest test passed. The Windows regression slot was skipped. |
| QtTest case count | `./ControlShutdownHookTest -v1 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r2/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r2/logs/test-4.log; exit $rc` (from `build/tests`) | 0 | 12 passed, 0 failed, 1 skipped; the skipped case is the Windows pipe regression. |
| Reversal proof | Not run | N/A | Reversing the product patch here would leave the Windows-only case skipped in both states, so it could not provide the required failing assertion. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r2/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r2/logs/gate-3.log; exit $rc` | 1 | Six unrelated functions are reported over CCN 10. No function from this change is reported. Baseline unchanged. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r2/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-85.r2/logs/gate-4.log; exit $rc` | 1 | Fourteen unrelated files are reported over the existing 500-line limit. This test file is not listed. Baseline unchanged. |

## Why blocked

This worktree is configured for Linux with `/usr/bin/c++`; its `Q_OS_WIN` implementation compiles out, and the Windows-only regression skips. There is no MinGW compiler or configured Windows Qt build in this environment. I therefore cannot honestly claim behavioral RED, GREEN, or the required retraction proof. The code and regression are present, but Windows validation remains required.

## Files committed

- `include/ControlServer.h`
- `src/core/ControlServerWin32.cpp`
- `tests/src/core/ControlShutdownHookTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-85.md`
