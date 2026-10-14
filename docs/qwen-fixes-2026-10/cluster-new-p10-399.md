# Cluster new-p10-399: step recorder paint hang

**Verdict: FIXED**

`StepRecorderWidget::setStepsLength()` now ignores zero and negative tick lengths. `paintEvent()` draws the ruler only when it has a positive step length, which also protects the widget's default zero length. This prevents the GUI thread from getting stuck while painting.

## File evidence

- [StepRecorderWidget.cpp](../../src/gui/editors/StepRecorderWidget.cpp#L101): `setStepsLength()` rejects nonpositive tick lengths.
- [StepRecorderWidget.cpp](../../src/gui/editors/StepRecorderWidget.cpp#L122): the ruler loop is guarded by a positive-length check.
- [StepRecorderWidgetTest.cpp](../../tests/src/gui/StepRecorderWidgetTest.cpp#L51): the test renders a zero-length widget in a child process and fails if painting does not finish within five seconds.
- [CMakeLists.txt](../../tests/CMakeLists.txt#L859): registers the test and its offscreen Qt platform.

The initial test harness used `repaint()` on a hidden offscreen widget and did not dispatch paint; its first run passed on the buggy code. Before changing production code, I changed the harness to call `QWidget::render()`. That corrected regression then failed on the original implementation, and the same regression passed with the fix.

## RED/GREEN evidence

The affected CTest family contains **1 test** (`StepRecorderWidgetTest`). The corrected regression failed before the fix and in the required reverse-patch check; it passed with the fix both before and after that check.

| Check | Exact command | Exit |
|---|---|---:|
| RED, original code | `ctest -R StepRecorderWidgetTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| GREEN, fixed code | `ctest -R StepRecorderWidgetTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| RED, reverse-patch proof | `ctest -R StepRecorderWidgetTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| GREEN, restored patch | `ctest -R StepRecorderWidgetTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

CTest ran from `build/tests`. Each result above is 1/1 CTest tests. The original RED assertion was `painting with a zero step length did not finish`.

## Build, patch, and gate commands

Commands using `cmake --build . --target StepRecorderWidgetTest` ran from `build/tests`; all others ran from the worktree root. The initial test harness run is recorded as a pass, but is not RED/GREEN evidence because it did not paint.

| Purpose | Exact command | Exit |
|---|---|---:|
| Baseline build attempt (rebuilt `zene`; test target is in `build/tests`) | `cmake --build build --target zene StepRecorderWidgetTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 2 |
| Baseline regression target build | `cmake --build . --target StepRecorderWidgetTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Initial, ineffective harness run | `ctest -R StepRecorderWidgetTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Corrected regression target build | `cmake --build . --target StepRecorderWidgetTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Fixed `zene` build | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Fixed regression target build | `cmake --build . --target StepRecorderWidgetTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Save fix and reverse it for proof | `git diff -- src/gui/editors/StepRecorderWidget.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Reverse-patch `zene` build | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Reverse-patch regression target build | `cmake --build . --target StepRecorderWidgetTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/build-7.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Restored `zene` build | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/build-8.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Restored regression target build | `cmake --build . --target StepRecorderWidgetTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/build-9.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Whole-tree source manifest reproduction | `bash tests/all-sources-reproduce.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-399.r1/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

The complexity gate reported no regressions; the file-length gate passed. The all-sources manifest reproduced its recipe. The `fff grep` tool was not available in this session's tool set, so numbered logs were searched with `rg` for `error`, `FAILED`, and `Assertion`, plus target and result lines.

## Files in the commit

- `src/gui/editors/StepRecorderWidget.cpp`
- `tests/src/gui/StepRecorderWidgetTest.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-399.md`
