IMPLEMENTED

# Cluster Q050-022 - Show automation lanes as track children (round 4 / rework)

This is the round-4 rework of the review at
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/qa/reviews/Q050-022.r3.code.md`.
The ownership, save/load, deletion undo/redo and the two tests already passed.
Three points were asked for and are addressed below; nothing else moved.

## What this round changed

1. **Fold-state changes are now observable by the arranger on command, undo and
   redo.** `Track` gains the signal `automationLanesCollapsedChanged`
   (`include/Track.h:358,576`). `Track::setAutomationLanesCollapsed` emits it on a
   real change, and `Track::loadTrack` emits it when a checkpoint restore reads a
   different `autolanes` value (`src/core/Track.cpp:878`) - so an undo/redo of
   `automation.lanes_set_collapsed` (a live Track checkpoint, which restores
   through `loadTrack`) reaches the view. `TrackContainerView::addTrackView`
   connects every track's signal to `realignTracks` (queued,
   `src/gui/editors/TrackContainerView.cpp:213`). The fold button and the context
   menu no longer call `realignTracks()` by hand
   (`src/gui/tracks/TrackOperationsWidget.cpp:159,397`).
2. **The GUI test no longer realigns manually and no longer edits through the
   control API.** `foldingTheOwnerHidesItsChildLane`,
   `hidingTheOwnerHidesItsChildLane` and `theAgentReportsTheDrawnOwnerAndLaneState`
   invoke `automation.lanes_set_collapsed` / `track.set_visible` / `control.undo`
   / `control.redo` and then only `processEvents()`; the fold, undo and redo each
   move the drawn row on their own. `theChildLaneIsSelectableAndEditable` selects
   the lane's `AutomationClipView` with a click, opens it in the automation editor
   with a DOUBLE-CLICK on the child lane's clip, and moves a point with the
   editor's arrow keys - a real GUI gesture - then reads the moved point back from
   `automation.get_state`. There is no `automation.add_point` in the test.
3. **`track.remove`'s transaction text** (`src/core/ControlCommandsArrangement.cpp`
   `trackRemovalTransaction`, `:102`) now describes capturing the owner AND each
   child lane as one all-or-nothing structural step, and the failure case where a
   lane (or the owner) cannot be captured - in which case NO inverse is recorded
   and the reply says so. The text is asserted by
   `deletingTheOwnerRemovesLanesAndUndoRestoresThem` (success) and
   `anUncapturableLaneRefusesTheInverseHonestly` (failure).

## Acceptance evidence

Commands run from `<worktree>/build/tests` with `ctest -R <name>
--output-on-failure`, `QT_QPA_PLATFORM=offscreen` and the lane's `LD_LIBRARY_PATH`
(the GUI test's environment is set by ctest). Logs are NEW numbered files under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-022.r4/logs`.

| Criterion (verbatim) | Test name(s) | How it proves it | Exit |
|----------------------|--------------|------------------|------|
| An automation lane with followsTrack is rendered as a collapsible child of its owning track in the arranger. | `AutomationLaneChildGuiTest::theLaneIsDrawnDirectlyUnderItsOwner`, `foldingTheOwnerHidesItsChildLane` | lane row index == owner+1 and its settings column indents further; `automation.lanes_set_collapsed` folds/unfolds with no `realignTracks()` call | 0 |
| Collapsing or hiding the owner hides its child lanes; moving, deleting, undoing, and redoing the owner keep lane ownership consistent. | `foldingTheOwnerHidesItsChildLane`, `hidingTheOwnerHidesItsChildLane`, `theAgentReportsTheDrawnOwnerAndLaneState` (fold + undo + redo), `AutomationLaneChildrenTest::reorderingTheOwnerKeepsLaneOwnership`, `deletingTheOwnerRemovesLanesAndUndoRestoresThem`, `foldingIsOneUndoStepAndRoundTrips` | fold/undo/redo move the drawn row through the track signal alone; hide through `track.set_visible`; `track.move` keeps `follows`; one `control.undo` restores the owner+lanes group and its point, redo removes it again | 0 |
| A GUI test verifies child-lane selection/editing and the agent surface reports the same owner and lane state. | `theChildLaneIsSelectableAndEditable` (click + double-click + arrow-key point edit), `theAgentReportsTheDrawnOwnerAndLaneState`, `AutomationLaneChildrenTest::theSurfaceReportsTheOwnerAndTheChildCount` | a GUI point move is read back from `automation.get_state`; `follows`/`automation_lane_count`/`lanes_collapsed` agree with the drawn rows | 0 |

Additional regression this round:

| What | Test | Exit |
|------|------|------|
| An uncapturable child lane refuses the inverse honestly, still deleting the group | `AutomationLaneChildrenTest::anUncapturableLaneRefusesTheInverseHonestly` | 0 |

Exact commands and observed exits:

```
cmake --build build --target AutomationLaneChildrenTest AutomationLaneChildGuiTest -- -j4 > logs/build-1.log 2>&1   EXIT=0
cd build/tests && ctest -R AutomationLaneChildrenTest --output-on-failure          > logs/test-core-1.log 2>&1     EXIT=0
cd build/tests && ctest -R AutomationLaneChildGuiTest --output-on-failure          > logs/test-gui-1.log 2>&1      EXIT=0
cmake --build build --target zene -- -j4                                           > logs/build-2.log 2>&1        EXIT=0
bash tests/complexity-gate.sh --check                                              > logs/gate-complexity.log 2>&1 EXIT=1 (registerTrackRemove CCN 11)
# after extracting trackRemovalTransaction (this round's own regression):
cmake --build build --target AutomationLaneChildrenTest -- -j4                     > logs/build-5.log 2>&1        EXIT=0
cd build/tests && ctest -R "AutomationLaneChildrenTest|AutomationLaneChildGuiTest" --output-on-failure > logs/test-lanes-2.log 2>&1   EXIT=0
cmake --build build --target zene -- -j4                                           > logs/build-6.log 2>&1        EXIT=0
cmake --build build --target zene AutomationLaneChildrenTest AutomationLaneChildGuiTest -- -j4 > logs/build-8.log 2>&1 EXIT=0  (final, after all edits)
cd build/tests && ctest -R "AutomationLaneChildrenTest|AutomationLaneChildGuiTest" --output-on-failure > logs/test-lanes-final.log 2>&1 EXIT=0
bash tests/complexity-gate.sh --check                                              > logs/gate-complexity-2.log 2>&1 EXIT=1 (pre-existing; no lane file listed)
bash tests/file-length-gate.sh --check                                             > logs/gate-filelength-2.log 2>&1 EXIT=1 (pre-existing; no lane file listed)
bash tests/fork-sources-gate.sh                                                    > logs/gate-fork.log 2>&1      EXIT=0
bash tests/all-sources-reproduce.sh                                                > logs/gate-allsources.log 2>&1 EXIT=0 (REPRODUCES)
bash tests/no-upstream-regression-gate.sh                                          > logs/gate-upstream.log 2>&1  EXIT=0
tests/scripted/check-namespace                                                     > logs/gate-namespace.log 2>&1 EXIT=0
bash tests/no-tautology-gate.sh                                                    > logs/gate-tautology.log 2>&1 EXIT=0
python3 tests/doc-figures-gate.py                                                  > logs/doc-figures-1.log 2>&1  EXIT=0 (829 ids / 76 groups)
```

Negative control (proves the new GUI assertions detect the reviewer's gap):
disabling the `automationLanesCollapsedChanged` connect in `addTrackView` and
rebuilding made `foldingTheOwnerHidesItsChildLane` and
`theAgentReportsTheDrawnOwnerAndLaneState` FAIL, ctest EXIT=8
(`logs/build-neg.log`, `logs/test-neg.log`). The connect was restored and the
suite passed again (`logs/build-7.log`, `logs/test-gui-2.log`, EXIT=0).

## New commands and reversibility

No command was added or removed this round. `automation.lanes_set_collapsed` is
still **TrueInverse** (`src/core/ControlCommandsAutomationLanes.cpp:242`, A16 row
`src/core/ControlReversibilityTableArranger.cpp:131`); `automation.get_state`
gained only read-only fields and is still NotMutating. The commands snapshot is
unchanged (829 ids / 76 groups, canary re-checked by `doc-figures-gate.py`).

## Format changes

None this round. `autolanes` on `<track>` (written only when folded, reset on
absence) was already introduced in the previous commit; old projects load and
render exactly as before and the data round-trips
(`followAndFoldSurviveSaveAndLoad`).

## Known limits

- The fold signal is queued: a caller that sets the flag and inspects the rows in
  the same call still needs an event-loop turn. Every product path returns to the
  loop before the user sees it.
- A lane's points are edited on the lane's own editing surface (the automation
  editor opened by double-clicking the child lane's clip); the arranger clip view
  has never edited points directly.
- `anUncapturableLaneRefusesTheInverseHonestly` reaches the 64 KB capture cap with
  a synthetic oversized lane; the same production path runs.
- TSan does not run on this box; the change is control/GUI-thread only and adds
  nothing to the audio thread.
