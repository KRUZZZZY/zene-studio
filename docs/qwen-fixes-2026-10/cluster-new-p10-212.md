# Cluster new-p10-212: exact half-quantum phase errors

Verdict: **FIXED**

## Finding and change

In `src/core/LinkSync.cpp:91`, the old `floor(x + 0.5)` tie handling returned `-period/2` for an exact positive half-period, contrary to the documented interval `(-period/2, period/2]`. It now uses `ceil(x - 0.5)`, so both exact half-period inputs map to the positive boundary. The link state report consumes this helper in `src/core/LinkSyncWire.cpp:200`.

`tests/src/core/ControlLinkCommandsTest.cpp:331` checks both `+2` and `-2` with a period of `4`, expecting `+2` from each. The test uses a friend accessor declared at `include/LinkSync.h:267` to exercise the arithmetic at the exact tie without report-clock drift. The historical `LinkPhaseLock.cpp` named in the finding is absent from this worktree; the current phase-difference implementation is in `LinkSync.cpp`.

## Evidence table

The affected ctest family is `ControlLinkCommandsTest`: **1 ctest**, with 13 QtTest entries in the RED output (12 passed, 1 failed). The GREEN ctest summary reports 1/1 passed.

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Build original code and new regression | `cmake --build build --target zene ControlLinkCommandsTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-212.r1/logs/build-1.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | 0 | Build succeeded before the fix. |
| RED on original code | `ctest -R '^ControlLinkCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-212.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | 8 | Exact positive half returned `-2`; 1 ctest failed. |
| Build fixed code | `cmake --build build --target zene ControlLinkCommandsTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-212.r1/logs/build-2.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | 0 | Build succeeded. |
| GREEN | `ctest -R '^ControlLinkCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-212.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | 0 | 1/1 ctest passed. |
| Revert production arithmetic for negative control | `git diff -- src/core/LinkSync.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-212.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-212.r1/logs/fix.patch; status=$?; echo EXIT=$status; exit "$status"` | 0 | Reverted only the production arithmetic; the test accessor remained so the regression could compile. |
| Build negative control | `cmake --build build --target zene ControlLinkCommandsTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-212.r1/logs/build-3.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | 0 | Rebuilt reverted production code. |
| Negative-control RED | `ctest -R '^ControlLinkCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-212.r1/logs/test-3.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | 8 | Same assertion failed: `+2` returned `-2`. |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-212.r1/logs/fix.patch; status=$?; echo EXIT=$status; exit "$status"` | 0 | Restored the fix. |
| Rebuild restored code | `cmake --build build --target zene ControlLinkCommandsTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-212.r1/logs/build-4.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | 0 | Build succeeded. |
| GREEN after negative control | `ctest -R '^ControlLinkCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-212.r1/logs/test-4.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | 0 | 1/1 ctest passed after restoring the patch. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-212.r1/logs/complexity-gate-1.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-212.r1/logs/file-length-gate-1.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | 0 | Passed. |

The requested `fff grep` tool was unavailable (`command -v fff` exited 1 and no fff tool was exposed), so build/test/gate logs were searched with `rg`, one term at a time.

## Files committed

- `include/LinkSync.h`
- `src/core/LinkSync.cpp`
- `tests/src/core/ControlLinkCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-212.md`
