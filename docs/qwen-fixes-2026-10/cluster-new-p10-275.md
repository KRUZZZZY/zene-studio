# Cluster new-p10-275

**Verdict: FALSE-POSITIVE**

`NineButtonSelector::setSelected()` calls `model()->setValue(newButton)` and then `updateButton(newButton)` ([plugins/Vibed/NineButtonSelector.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-275/plugins/Vibed/NineButtonSelector.cpp:68)). The patrol's duplicate-emission claim depends on `setValue()` invoking `NineButtonSelector::modelChanged()`. It does not: `ModelView::setModel()` calls `modelChanged()` when the model is replaced ([src/gui/ModelView.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-275/src/gui/ModelView.cpp:53)), while `ModelView::doConnections()` connects `dataChanged()` to the widget's `update()` slot ([src/gui/ModelView.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-275/src/gui/ModelView.cpp:89)). No value-change connection calls the selector's `modelChanged()` at [plugins/Vibed/NineButtonSelector.cpp:63](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-275/plugins/Vibed/NineButtonSelector.cpp:63). A click therefore calls `updateButton()` once, which emits `NineButtonSelection` once at line 83. No production code was changed.

The suggested `tests/reference/Vibed/NineButtonSelector.cpp` is an upstream source snapshot used by the Part C reference-module build; it is not a registered test. The configured CTest tree has no `NineButtonSelector` test. A temporary click-path probe was added to the existing `AccessibilityHelperTest` target and then removed. Once its model fixture was initialized, the probe passed on the original production implementation, so it did not provide a red regression. The early probe run that exited 8 crashed during selector construction before reaching an assertion. No fix patch reversal was applicable because no production fix was made.

## Evidence

The configured test family available for the temporary probe was `AccessibilityHelperTest`: CTest executed 1 of 1 registered tests. The candidate RED command returned 0, demonstrating that the reported duplicate was not reproduced. There is no GREEN command because this finding is false-positive and no behavior change was made.

| Check | Exact command | Result |
|---|---|---|
| Candidate RED check (not red) | `ctest -R '^AccessibilityHelperTest$' --output-on-failure` from `build/tests` | `test-red-2.log`, EXIT=0; 1/1 passed |
| Earlier harness run | `ctest -R '^AccessibilityHelperTest$' --output-on-failure` from `build/tests` | `test-red-1.log`, EXIT=8; selector-construction segfault before an assertion |
| Affected-family count | `ctest -R '^AccessibilityHelperTest$' --output-on-failure` from `build/tests` | 1 CTest test executed; no `NineButtonSelector` CTest is registered |
| Complexity gate | `bash tests/complexity-gate.sh --check` | `gate-1-complexity.log`, EXIT=0 |
| File-length gate | `bash tests/file-length-gate.sh --check` | `gate-2-file-length.log`, EXIT=0 |

The temporary test target build command was `cmake --build build --target zene AccessibilityHelperTest`. Its six logged runs exited 2, 2, 2, 0, 2, and 0 (`build-1.log` through `build-6.log`); the final build exited 0 before the passing candidate test. The early build failures were harness setup errors, not product failures.

All logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-275.r1/logs`. The `fff` grep tool was unavailable in this session, so log searches used `rg` with one search term per call.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-275.md`
