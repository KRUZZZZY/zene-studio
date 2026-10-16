IMPLEMENTED

# Cluster Q050-022 - Show automation lanes as track children (round 5)

This is the round-5 rework of the review at
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/qa/reviews/Q050-022.r4.code.md`.
Everything it asked for is addressed below; nothing else moved.

## What this round changed

1. **`groupedWithLaneChildren` defers a child lane until its owner is emitted**
   (`src/gui/editors/TrackContainerView.cpp:345`). Previously a lane met before
   its owner in the flat list was placed as a standalone row and was already
   marked placed when the owner's turn came, so a DOWNWARD move of the owner
   (`moveTrackView` leaves the lane earlier in the flat list and then calls
   `realignTracks`, `:246-265`) drew the lane ABOVE its owner and `realignTracks`
   could not restore the grouping. Now the first pass records which owners the
   view list itself holds; a lane whose owner is still ahead is skipped in its
   own turn and the owner's sweep places it directly after itself. A lane whose
   owner is not in the container at all (an orphan) is still emitted where it
   stands, so its row stays reachable; unrelated tracks keep their relative
   order; a lane that follows a lane nests in the same sweep; and a defensive
   tail keeps every view in the output even for a hand-made follows cycle, so
   the regrouping can never delete rows.
2. **A GUI regression test pins that downward move.**
   `AutomationLaneChildGuiTest::movingTheOwnerDownKeepsItsLaneBeneathIt`
   (`tests/src/gui/AutomationLaneChildGuiTest.cpp:253`): a peer track is added,
   the owner is moved below it with `moveTrackView` (which runs `realignTracks`
   itself), and the test asserts the peer at row 0, the owner at row 1 and the
   lane at the owner's row + 1, still visible. It calls neither `realignTracks()`
   nor any fold command. The command, undo and redo fold assertions
   (`foldingTheOwnerHidesItsChildLane`, `theAgentReportsTheDrawnOwnerAndLaneState`)
   are unchanged and still pass with no manual realignment.
3. **`Track::automationLaneChildren` is genuinely read-locked**
   (`src/core/Track.cpp:425`): the walk of the container's track list runs under
   `TrackContainer::m_tracksMutex` (`lockForRead`, the same lock every other
   container walker takes), so the round-4 report's claim is now true instead of
   merely corrected. The callers are GUI/command-thread code only
   (`TrackOperationsWidget.cpp:330`, `ControlCommandsArrangement.cpp:320`,
   `ControlCommandsAutomation.cpp:113`, `ControlCommandsAutomationLanes.cpp:256`,
   `ControlStructuralSupport.cpp:316`); none holds the lock already (the lock is
   not recursive), and nothing on the audio thread calls it - by the realtime
   rule nothing may, because the lock can wait. TSan does not run on this box
   (thread-creation crash), so the race reasoning is by inspection: every caller
   is on the GUI/control thread and the writers that touch `m_tracks` take the
   write lock (e.g. `TrackContainer::addTrack`/`removeTrack`).

Negative control: with the deferral disabled (`ownersHere.contains` guarded out,
`build-neg.log`), `ctest -R AutomationLaneChildGuiTest` FAILED exactly on the new
assertion - "the redraw after a downward owner move detached its lane", EXIT 8
(`test-neg.log`). The fix was restored and the suite passed again (`build-2.log`,
`test-lanes-2.log`, EXIT 0).

## Acceptance evidence

Commands run from `<worktree>`; tests from `<worktree>/build/tests` with
`QT_QPA_PLATFORM=offscreen` and the lane's env. Logs are NEW numbered files
under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-022.r5/logs`.

| Criterion (verbatim) | Test name(s) | How it proves it | Exit |
|----------------------|--------------|------------------|------|
| An automation lane with followsTrack is rendered as a collapsible child of its owning track in the arranger. | `AutomationLaneChildGuiTest::theLaneIsDrawnDirectlyUnderItsOwner`, `foldingTheOwnerHidesItsChildLane` | lane row index == owner+1 and its settings column indents further; `automation.lanes_set_collapsed` folds/unfolds with no `realignTracks()` call | 0 |
| Collapsing or hiding the owner hides its child lanes; moving, deleting, undoing, and redoing the owner keep lane ownership consistent. | `foldingTheOwnerHidesItsChildLane`, `hidingTheOwnerHidesItsChildLane`, `theAgentReportsTheDrawnOwnerAndLaneState` (fold + undo + redo), `AutomationLaneChildrenTest::reorderingTheOwnerKeepsLaneOwnership`, `deletingTheOwnerRemovesLanesAndUndoRestoresThem`, `foldingIsOneUndoStepAndRoundTrips`, NEW `movingTheOwnerDownKeepsItsLaneBeneathIt` (GUI row order after a downward move) | fold/undo/redo move the drawn row through the track signal alone; `track.move` keeps `follows`; one `control.undo` restores the owner+lanes group; the downward move keeps the lane at owner+1 | 0 |
| A GUI test verifies child-lane selection/editing and the agent surface reports the same owner and lane state. | `theChildLaneIsSelectableAndEditable`, `theAgentReportsTheDrawnOwnerAndLaneState`, `AutomationLaneChildrenTest::theSurfaceReportsTheOwnerAndTheChildCount` | a GUI point move is read back from `automation.get_state`; `follows`/`automation_lane_count`/`lanes_collapsed` agree with the drawn rows | 0 |

Exact commands and observed exits:

```
cmake --build build --target zene AutomationLaneChildrenTest AutomationLaneChildGuiTest -- -j4  > logs/build-1.log 2>&1   EXIT=0
cd build/tests && ctest -R "AutomationLaneChildrenTest|AutomationLaneChildGuiTest" --output-on-failure > logs/test-lanes-1.log 2>&1   EXIT=0
# negative control: deferral disabled, only the product file changed
cmake --build build --target AutomationLaneChildGuiTest -- -j4   > logs/build-neg.log 2>&1   EXIT=0
cd build/tests && ctest -R AutomationLaneChildGuiTest --output-on-failure > logs/test-neg.log 2>&1   EXIT=8 (expected failure: "the redraw after a downward owner move detached its lane")
# deferral restored; full final run
cmake --build build --target zene AutomationLaneChildrenTest AutomationLaneChildGuiTest -- -j4  > logs/build-2.log 2>&1   EXIT=0
cd build/tests && ctest -R "AutomationLaneChildrenTest|AutomationLaneChildGuiTest" --output-on-failure > logs/test-lanes-2.log 2>&1   EXIT=0
cmake --build build --target ReversibilityContractTest -- -j4  > logs/build-rev.log 2>&1   EXIT=0
cd build/tests && ctest -R ReversibilityContractTest --output-on-failure > logs/test-rev.log 2>&1    EXIT=0
cd build/tests && ctest -R ControlCommandsSnapshot --output-on-failure > logs/test-snapshot.log 2>&1 EXIT=0
```

## Gates

```
bash tests/complexity-gate.sh --check      > logs/gate-complexity.log 2>&1   EXIT=1 (pre-existing on train 5; no Q050-022 file listed)
bash tests/file-length-gate.sh --check     > logs/gate-filelength.log 2>&1   EXIT=1 (pre-existing on train 5; no Q050-022 file listed)
bash tests/fork-sources-gate.sh            > logs/gate-fork.log 2>&1         EXIT=0
bash tests/all-sources-reproduce.sh        > logs/gate-allsources.log 2>&1   EXIT=0 (REPRODUCES)
bash tests/no-upstream-regression-gate.sh  > logs/gate-upstream.log 2>&1     EXIT=0
tests/scripted/check-namespace             > logs/gate-namespace.log 2>&1    EXIT=0
bash tests/no-tautology-gate.sh            > logs/gate-tautology.log 2>&1    EXIT=0
python3 tests/doc-figures-gate.py          > logs/doc-figures.log 2>&1       EXIT=0 (829 ids / 76 groups, w/ wasm.* left out)
```

## New commands and reversibility

None added or removed this round. `automation.lanes_set_collapsed` is still
**TrueInverse** (`src/core/ControlCommandsAutomationLanes.cpp:242`, A16 row
`src/core/ControlReversibilityTableArranger.cpp:131`); `automation.get_state`
reports read-only fields and is still NotMutating. No command surface changed,
so the snapshot is unchanged (829 ids / 76 groups, re-checked).

## Format changes

None. No serialised data changed this round.

## Known limits

- The fold signal is queued: a caller that sets the flag and inspects the rows
  in the same call still needs an event-loop turn. Every product path returns to
  the loop before the user sees it.
- A lane's points are edited on the lane's own editing surface (the automation
  editor opened by double-clicking the child lane's clip); the arranger clip view
  has never edited points directly.
- `anUncapturableLaneRefusesTheInverseHonestly` reaches the 64 KB capture cap with
  a synthetic oversized lane; the same production path runs.
- TSan does not run on this box (thread-creation crash); the lock added to
  `automationLaneChildren` is reasoned about by inspection above: GUI/command
  callers only, no caller nests inside the same lock, audio thread never calls it.
- A downward owner move past its OWN lane row is a visual no-op (the lane comes
  back beneath it): the derived order puts a child always directly under its
  owner, so the owner cannot leave its own lane above it.
