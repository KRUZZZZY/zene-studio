# Cluster new-p9-60

**Verdict: FIXED**

`clip.speed` previously checkpointed and wrote the clip edits for every successful call, including `{clip: ...}` when all four resulting speed fields equalled their current values. The handler now checkpoints, writes and marks the song modified only if at least one speed field changes. The success response is unchanged.

## Evidence

- `src/core/ControlCommandsSampleTools.cpp:141-153` captures the before state, computes the requested result and gates the checkpoint/write on the four speed fields changing.
- `tests/src/core/AudioAutomationToolsTest.cpp:116-123` invokes `clip.speed` with only a clip and asserts the journal depth stays unchanged.
- The initial RED run failed at the regression assertion: actual undo depth `2`, expected `1` (`test-1.log`, lines 16-19). Eight other Qt test cases passed.
- The required reverse-patch RED run reproduced the same failure (`test-3.log`, lines 16-19). Reapplying the production patch made the same test target pass (`test-4.log`).
- The affected CTest family is `AudioAutomationToolsTest`; `ctest -R AudioAutomationToolsTest` selected **1 CTest test**. The passing run reports 1/1 passed.
- The existing test source is already registered in `tests/fork-sources.txt:1907`; no source-list change was needed.

## Verification commands

Build commands ran from the repository root. CTest commands ran from `build/tests`. The listed exit codes are the shell exit codes printed after each command.

| Purpose | Exact command | Exit |
|---|---|---:|
| Build regression before product fix | `cmake --build build --target AudioAutomationToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-60.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| RED on original product code | `ctest -R AudioAutomationToolsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-60.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 |
| Build after fix | `cmake --build build --target zene AudioAutomationToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-60.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| GREEN after fix | `ctest -R AudioAutomationToolsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-60.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Save product-only reverse-proof patch | `git diff -- src/core/ControlCommandsSampleTools.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-60.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Remove product fix for proof | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-60.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Build with product fix removed | `cmake --build build --target zene AudioAutomationToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-60.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| RED with product fix removed | `ctest -R AudioAutomationToolsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-60.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 |
| Reapply product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-60.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Final build | `cmake --build build --target zene AudioAutomationToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-60.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Final GREEN | `ctest -R AudioAutomationToolsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-60.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-60.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-60.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |

The fff grep tool was not available in this session. I searched the log folder with `rg` for `error`, `FAILED` and `Assertion`; the two failure summaries are the expected RED runs above.

## Files in the fix commit

- `src/core/ControlCommandsSampleTools.cpp`
- `tests/src/core/AudioAutomationToolsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-60.md`
