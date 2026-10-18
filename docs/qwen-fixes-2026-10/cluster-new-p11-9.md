# Cluster new-p11-9: Shift-clicking a locked clip

Verdict: **BLOCKED** — the behavior fix and regression are verified, but both mandatory ratchet gates fail on unchanged files in this branch.

## Evidence summary

| Check | Evidence |
| --- | --- |
| RED | `ctest -R '^ArrangerOverlaysVideoLaneTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-9.r1/logs/test-red-proof-2.log 2>&1; echo EXIT=$?` — exit 8; the existing clip lost selection. |
| GREEN | `ctest -R '^ArrangerOverlaysVideoLaneTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-9.r1/logs/test-green-proof-2.log 2>&1; echo EXIT=$?` — exit 0. |
| Affected family | 1/1 CTest target passed after the fix; the RED QtTest run had 9 pass and the regression fail. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-9.r1/logs/gate-complexity-3.log 2>&1; echo EXIT=$?` — exit 1. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-9.r1/logs/gate-file-length-3.log 2>&1; echo EXIT=$?` — exit 1. |
| Files in fix commit `662fb5ba7` | `src/gui/clips/ClipView.cpp`; `tests/src/gui/ArrangerOverlaysVideoLaneTest.cpp`; `tests/upstream-modifications.txt`; `docs/qwen-fixes-2026-10/cluster-new-p11-9.md`. |

## Evidence

`src/gui/clips/ClipView.cpp:1466` contains `ClipView::pressLocked()`. Before the fix, pressing any unselected locked clip without `KBD_COPY_MODIFIER` cleared all selected clips. It now skips that clear when `Qt::ShiftModifier` is held and selects the locked clip, preserving Shift-add behavior.

`tests/src/gui/ArrangerOverlaysVideoLaneTest.cpp:273` adds `shiftClickingALockedClipKeepsTheExistingSelection()`. It prepares one selected clip, Shift-clicks a second locked clip through the real widget path, and checks that both remain selected. With the production patch reversed, the assertion failed: `selectedView->isSelected()` returned false. With the patch restored, CTest passed.

The affected CTest family ran **1/1 target** (`ArrangerOverlaysVideoLaneTest`). The red QtTest run reported 9 passed and 1 failed; the green CTest run passed.

Both required gates still fail on unrelated existing files. The complexity gate reports six over-limit functions in files outside this change (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, and `ControlCommandsShortcuts.cpp`). The file-length gate reports `tests/src/gui/ArrangerTimelineTest.cpp` at 689 code lines; this file is unchanged in the final diff. No gate baseline or limit was changed.

## Commands and exit codes

| Purpose | Exact command | Exit |
| --- | --- | ---: |
| Save the product-only patch and reverse it | `git diff -- src/gui/clips/ClipView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-9.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-9.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Reverse the patch for the final overlay-test proof | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-9.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild original code and affected target | `cmake --build build --target zene ArrangerOverlaysVideoLaneTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-9.r1/logs/build-6.log 2>&1; echo EXIT=$?` | 0 |
| RED regression | `ctest -R '^ArrangerOverlaysVideoLaneTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-9.r1/logs/test-red-proof-2.log 2>&1; echo EXIT=$?` | 8 |
| Restore the product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-9.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild fixed app and affected target | `cmake --build build --target zene ArrangerOverlaysVideoLaneTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-9.r1/logs/build-7.log 2>&1; echo EXIT=$?` | 0 |
| GREEN regression | `ctest -R '^ArrangerOverlaysVideoLaneTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-9.r1/logs/test-green-proof-2.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-9.r1/logs/gate-complexity-3.log 2>&1; echo EXIT=$?` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-9.r1/logs/gate-file-length-3.log 2>&1; echo EXIT=$?` | 1 |

The gate failures block a clean FIXED verdict under this task's commit gates; fixing them would require unrelated source changes or forbidden baseline changes.
