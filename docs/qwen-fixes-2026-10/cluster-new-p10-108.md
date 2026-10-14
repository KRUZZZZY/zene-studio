# Cluster new-p10-108

**Verdict: FIXED**

XyPad::mousePressEvent() now enables input only for Qt::LeftButton; mouseReleaseEvent() clears it only for that button. mouseMoveEvent() continues updating both models during an active left drag. The handlers are at plugins/Delay/DelayControlsDialog.cpp:118 and :129.

The direct regression is in tests/src/gui/XyPadPairTest.cpp:113-153. It separately checks right and middle drags, a left drag, and that a right release does not stop an active left drag. The final fix-removal run failed the right, middle, and other-button-release cases; the left drag and existing pair test passed.

## Evidence

| Check | Exact command (working directory as stated) | Result |
|---|---|---|
| Baseline RED, before production fix | cmake --build build --target XyPadPairTest (worktree root), then ctest -R '^XyPadPairTest$' --output-on-failure (build/tests) | Build EXIT=0 (build-3.log); CTest EXIT=8 (test-1.log), 1 test ran. Right drag changed X from 0.2 to 0.8. |
| Fix-removal RED, final regression | git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-108.r2/logs/fix.patch; cmake --build build --target delay XyPadPairTest zene (worktree root); ctest -R '^XyPadPairTest$' --output-on-failure (build/tests) | Patch EXIT=0 (patch-3.log); build EXIT=0 (build-7.log); CTest EXIT=8 (test-5.log). Three non-left behaviors failed; left drag passed. |
| GREEN, restored production fix | git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-108.r2/logs/fix.patch; cmake --build build --target delay XyPadPairTest zene (worktree root); ctest -R '^XyPadPairTest$' --output-on-failure (build/tests) | Patch EXIT=0 (patch-4.log); build EXIT=0 (build-8.log); CTest EXIT=0 (test-6.log), 1/1 registered test passed. |
| Affected-family count | ctest -R '^XyPadPairTest$' --output-on-failure from build/tests | 1 registered CTest test executed; no zero-test result. |
| Complexity gate | bash tests/complexity-gate.sh --check | EXIT=0 (gate-3.log). |
| File-length gate | bash tests/file-length-gate.sh --check | EXIT=0 (gate-4.log). |

The fix-removal patch was captured with git diff -- plugins/Delay/DelayControlsDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-108.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-108.r2/logs/fix.patch (EXIT=0, patch-1.log). The final regression was then rerun with the same patch removed and restored as shown above.

The initially exploratory test builds also ran cmake --build build --target XyPadPairTest (EXIT=2, build-1.log, deprecated Qt mouse-event constructor) and the same command again (EXIT=2, build-2.log, AUTOMOC shim declarations incomplete). The corrected direct test build passed in build-3.log. All logs are in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-108.r2/logs. The fff grep tool was not exposed in this session; logs were searched with separate rg terms.

## Files committed

- plugins/Delay/DelayControlsDialog.cpp — left-button gating.
- tests/src/gui/XyPadPairTest.cpp — direct XyPad regression cases.
- tests/src/gui/DelayControlsDialog.h and tests/src/gui/EqFader.h — AUTOMOC inputs for the existing configured test target.
- tests/fork-sources.txt and tests/upstream-modifications.txt — source registrations and reason.
- docs/qwen-fixes-2026-10/cluster-new-p10-108.md — this report.
