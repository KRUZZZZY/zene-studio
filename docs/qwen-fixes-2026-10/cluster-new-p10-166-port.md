# Cluster new-p10-166-port

Verdict: **ALREADY-FIXED**

The newest line already fixes this bug via commit `32d599e20` (`fix(gui): define Graph first Shift-click (cluster new-p9-159)`), which is an ancestor of this branch's HEAD. That commit initializes `Graph::m_lastCursorX` to `-1` in the constructor and makes a first Shift-click (and first mouse move) edit only the clicked sample instead of calling `drawLineAt` with an indeterminate cursor position. The current `src/gui/widgets/Graph.cpp` already contains both changes, so no production edit is required.

## Evidence

- `src/gui/widgets/Graph.cpp:52` initializes `m_lastCursorX( -1 )`.
- `src/gui/widgets/Graph.cpp:190-197` handles a first Shift-click as a single-sample edit when `m_lastCursorX < 0`.
- `tests/src/gui/SampleEditorDialogTest.cpp::firstShiftClickEditsOnlyTheClickedSample` is the regression covering this behaviour and passes on the current tree.

Reverse-proof on the current tree (reverting commit `32d599e20`'s Graph.cpp hunk, rebuilding, and running the same test):

| Evidence | Exact command | Exit |
|---|---|---:|
| Baseline build; worktree root | `cmake --build build --target zene SampleEditorDialogTest > .../logs/build-green-0.log 2>&1; echo EXIT=$?` | 0 |
| Baseline GREEN; run from `build/tests` | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > .../logs/test-green-0.log 2>&1; echo EXIT=$?` | 0 |
| Reverse patch; worktree root | `git apply -R .../logs/fix.patch; echo EXIT=$?` | 0 |
| Reverse-patch build; worktree root | `cmake --build build --target zene SampleEditorDialogTest > .../logs/build-red-1.log 2>&1; echo EXIT=$?` | 0 |
| RED with fix reversed; run from `build/tests` | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > .../logs/test-red-1.log 2>&1; echo EXIT=$?` | 8 |
| Restore patch; worktree root | `git apply .../logs/fix.patch; echo EXIT=$?` | 0 |
| Restore build; worktree root | `cmake --build build --target zene SampleEditorDialogTest > .../logs/build-green-1.log 2>&1; echo EXIT=$?` | 0 |
| GREEN restored; run from `build/tests` | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > .../logs/test-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate; worktree root | `bash tests/complexity-gate.sh --check > .../logs/gate-complexity.log 2>&1; echo EXIT=$?` | 1 |
| File-length gate; worktree root | `bash tests/file-length-gate.sh --check > .../logs/gate-filelength.log 2>&1; echo EXIT=$?` | 1 |

The two gate exits of 1 come from pre-existing regressions in the newest line (6 functions over the CCN target, 13 files over the 500-line limit) that are unrelated to this bug and are already present on the clean tree; this change introduces no new regression in either gate.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-166-port.md`
