# Cluster new-p8-93

Verdict: **FIXED**

`ControlServer::sendBytes()` now rejects a reply larger than `MaxQueuedReplyBytes` before attempting its first socket write. This prevents a first partial write from placing an over-cap tail in `Client::pending`.

## Evidence

- `src/core/ControlServer.cpp:429`: the reply-size guard runs before either the existing-queue path or the first write.
- `tests/src/core/ControlShutdownHookTest.cpp:203`: the regression serves a test-only command with a 9 MiB response over a constrained AF_UNIX client socket. Without the fix, it receives the complete reply and the connection remains open; with the fix, the server retires the client before the full reply is sent.
- `tests/fork-sources.txt:1936`: records the product-new test source.

The RED assertion was `drain.closed && drain.bytes < expectedBytes` at `ControlShutdownHookTest.cpp:261`; it failed before the fix and again after temporarily removing the production patch. After reapplying the patch, the same test passed.

## Build and test evidence

Build commands ran from the worktree root. CTest ran from `build/tests`. Build and test output is in the numbered log files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-93.r1/logs`.

| Stage | Exact command | Exit | Log |
|---|---|---:|---|
| Initial test build (pre-fix) | `cmake --build build --target ControlShutdownHookTest` | 0 | `build-1.log` |
| Initial RED | `ctest -R '^ControlShutdownHookTest$' --output-on-failure` | 8 | `test-1.log` |
| RED assertion detail | `ctest -R '^ControlShutdownHookTest$' --output-on-failure -V` | 8 | `test-2.log` |
| Fixed build | `cmake --build build --target zene ControlShutdownHookTest` | 0 | `build-2.log` |
| Initial GREEN | `ctest -R '^ControlShutdownHookTest$' --output-on-failure` | 0 | `test-3.log` |
| Reversal build (fix removed) | `cmake --build build --target zene ControlShutdownHookTest` | 0 | `build-3.log` |
| Reversal RED | `ctest -R '^ControlShutdownHookTest$' --output-on-failure -V` | 8 | `test-4.log` |
| Restored build | `cmake --build build --target zene ControlShutdownHookTest` | 0 | `build-4.log` |
| Restored GREEN | `ctest -R '^ControlShutdownHookTest$' --output-on-failure -V` | 0 | `test-5.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-3.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-4.log` |

The affected CTest family executed 1 registered test (`ControlShutdownHookTest`). Its final QtTest summary was 7 passed, 0 failed, 0 skipped. The reversal run reported the same regression assertion failure as the initial RED run.

## Files in this fix

- `src/core/ControlServer.cpp`
- `tests/src/core/ControlShutdownHookTest.cpp`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-93.md`
