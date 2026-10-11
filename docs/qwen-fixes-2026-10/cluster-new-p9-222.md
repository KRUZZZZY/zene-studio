# Cluster new-p9-222

Verdict: **FIXED**

## Finding evidence

| Location | Evidence |
| --- | --- |
| `src/core/LinkSync.cpp:280-284` | Adoption cast `receivedUs` and `packet.sentUs` to `qint64` and subtracted them. |
| `src/core/LinkSyncWire.cpp:97-101` | The decoder now requires `sent_us < 2^63`, the first timestamp outside the signed range used by adoption. |
| `tests/src/core/ControlLinkCommandsTest.cpp:355-359` | Regression serializes `sent_us = 2^63` and requires packet decoding to reject it. |

The wire regression failed before the fix and failed again after the saved production patch was reversed. With the fix restored, the affected family passed: 1 registered CTest test (`ControlLinkCommandsTest`), containing 12 QtTest cases.

## Build and test evidence

All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-222.r1/logs`.

| Phase | Exact command and working directory | Exit | Result |
| --- | --- | ---: | --- |
| Initial RED build (worktree) | `cmake --build build --target ControlLinkCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-222.r1/logs/build-1.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 | Regression target built. |
| Initial RED test (`build/tests`) | `ctest -R '^ControlLinkCommandsTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-222.r1/logs/test-1.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 8 | Expected failure: 11 passed, 1 failed; decoder accepted `2^63`. |
| GREEN build (worktree) | `cmake --build build --target zene ControlLinkCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-222.r1/logs/build-2.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 | Rebuilt `zene` and the affected test target. |
| GREEN test (`build/tests`) | `ctest -R '^ControlLinkCommandsTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-222.r1/logs/test-2.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 | 12 passed, 0 failed; 1/1 CTest test passed. |
| Reverse saved production patch (worktree) | `git diff -- src/core/LinkSyncWire.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-222.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-222.r1/logs/fix.patch` | 0 | Removed only the production fix for the regression proof. |
| Proof RED rebuild (worktree) | `cmake --build build --target zene ControlLinkCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-222.r1/logs/build-3.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 | Rebuilt `zene` and test target with the fix reversed. |
| Proof RED test (`build/tests`) | `ctest -R '^ControlLinkCommandsTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-222.r1/logs/test-3.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 8 | Same assertion failed: 11 passed, 1 failed. |
| Restore saved production patch (worktree) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-222.r1/logs/fix.patch` | 0 | Restored the fix. |
| Proof GREEN rebuild (worktree) | `cmake --build build --target zene ControlLinkCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-222.r1/logs/build-4.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 | Rebuilt `zene` and test target with the fix restored. |
| Proof GREEN test (`build/tests`) | `ctest -R '^ControlLinkCommandsTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-222.r1/logs/test-4.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 | 12 passed, 0 failed; 1/1 CTest test passed. |

## Ratchet gates

| Gate | Exact command (worktree) | Exit |
| --- | --- | ---: |
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-222.r1/logs/gate-1.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-222.r1/logs/gate-2.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |

The `fff grep` tool was not exposed in this tool context; the log folder was searched with `rg` using `error`, `FAILED`, and `Assertion` as separate search terms.

## Files in the commit

- `src/core/LinkSyncWire.cpp`
- `tests/src/core/ControlLinkCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-222.md`

Commit message: `fix: cluster new-p9-222 signed Link timestamp overflow`
