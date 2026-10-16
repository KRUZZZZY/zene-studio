IMPLEMENTED

# Cluster Q050-022 - Show automation lanes as track children (round 6)

This is the round-6 rework of the review at
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/qa/reviews/Q050-022.r5.code.md`.
It addresses the three rework points and changes nothing else. The feature itself
(rounds 1-5) is unchanged; round 5's code and this round's diff are the only
moving parts. Build tree was already configured; only the named targets were
built (`-j4`), logs under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-022.r6/logs`.

## What this round changed

1. **The GUI test is registered as fork-NEW product code.**
   `tests/src/gui/AutomationLaneChildGuiTest.cpp` was only in
   `tests/all-sources.txt` (the whole-tree scope), so the fork-scoped ratchets
   (coverage, complexity, file-length, duplication) never measured it. It is now
   in `tests/fork-sources.txt` (appended with a comment: it lives under
   `tests/src/gui`, which the recipe's pathspec does not cover, so it is named by
   hand like the other GUI tests there). Its core sibling
   `tests/src/core/AutomationLaneChildrenTest.cpp` was already registered.
   `tests/all-sources.txt` was re-derived from its header recipe and reproduces
   (it already listed the file through `git ls-files`, exit 0). The file is under
   the 500-line limit and no function in it exceeds CCN 10, so registering it
   lists nothing new in gates 4 or 7.
2. **The locking rationale now matches the code.** `Track.cpp:419` and the
   feature note claimed a concurrent move was excluded by the read lock.
   `TrackContainer::moveTrack` (`src/core/TrackContainer.cpp:341`) and
   `clearAllTracks` mutate `m_tracks` WITHOUT `m_tracksMutex`, so the read lock in
   `Track::automationLaneChildren` excludes only `addTrack`/`removeTrack`. Both
   the `Track.cpp` comment and `docs/050-features/Q050-022.md` (What was built
   and Known limits) now say exactly that, and state that the real serialization
   is that every caller and mutator is GUI/control-thread code: control commands
   are marshalled to the UI thread by `ControlRegistry::runOnUiThread`
   (`src/core/ControlRegistry.cpp:223`). No code was added to the locking path -
   the claim was corrected rather than the matrix of writers widened, because
   `clearAllTracks` (project teardown, lock commented out at
   `src/core/TrackContainer.cpp:358`) would still be a writer outside it.
3. **The negative control now fails at the assertion the report names.** In
   `AutomationLaneChildGuiTest::movingTheOwnerDownKeepsItsLaneBeneathIt` the
   lane-adjacency `QVERIFY2` is now checked BEFORE the peer-row `QCOMPARE`s, so a
   reverted deferral fails on the lane assertion, not on a peer position. Rerun
   with the deferral guarded out confirmed it: `test-neg` FAILED exactly on
   `movingTheOwnerDownKeepsItsLaneBeneathIt() 'rows.indexOf(m_laneView) ==
   rows.indexOf(m_ownerView) + 1' returned FALSE. (the redraw after a downward
   owner move detached its lane)`, EXIT 8.

Negative control: `src/gui/editors/TrackContainerView.cpp` had
`ownersHere.contains(lane->followsTrack())` replaced by `false` (only that
expression changed) and `AutomationLaneChildGuiTest` was rebuilt (`build-neg.log`,
EXIT 0); CTest then FAILED on the lane-adjacency assertion above, EXIT 8
(`test-neg.log`). The product file was restored and the full focused set passed
again (`build-2.log`, `test-lanes-2.log`, EXIT 0). `git diff` for the control
shows no leftover change.

## Acceptance evidence

Tests from `<worktree>/build/tests` with `QT_QPA_PLATFORM=offscreen` and the
lane's env; every command's exit code is the one printed by the shell.

| Criterion (verbatim) | Test name(s) | How it proves it | Exit |
|----------------------|--------------|------------------|------|
| An automation lane with followsTrack is rendered as a collapsible child of its owning track in the arranger. | `AutomationLaneChildGuiTest::theLaneIsDrawnDirectlyUnderItsOwner`, `foldingTheOwnerHidesItsChildLane` | lane row index == owner+1 and its settings column indents further; `automation.lanes_set_collapsed` folds/unfolds with no `realignTracks()` call | 0 |
| Collapsing or hiding the owner hides its child lanes; moving, deleting, undoing, and redoing the owner keep lane ownership consistent. | `foldingTheOwnerHidesItsChildLane`, `hidingTheOwnerHidesItsChildLane`, `theAgentReportsTheDrawnOwnerAndLaneState` (fold + undo + redo), `AutomationLaneChildrenTest::reorderingTheOwnerKeepsLaneOwnership`, `deletingTheOwnerRemovesLanesAndUndoRestoresThem`, `foldingIsOneUndoStepAndRoundTrips`, `movingTheOwnerDownKeepsItsLaneBeneathIt` (GUI row order after a downward move) | fold/undo/redo move the drawn row through the track signal alone; `track.move` keeps `follows`; one `control.undo` restores the owner+lanes group; the downward move keeps the lane at owner+1 | 0 |
| A GUI test verifies child-lane selection/editing and the agent surface reports the same owner and lane state. | `theChildLaneIsSelectableAndEditable`, `theAgentReportsTheDrawnOwnerAndLaneState`, `AutomationLaneChildrenTest::theSurfaceReportsTheOwnerAndTheChildCount` | a GUI point move is read back from `automation.get_state`; `follows`/`automation_lane_count`/`lanes_collapsed` agree with the drawn rows | 0 |

Exact commands and observed exits:

```
cmake --build build --target zene AutomationLaneChildrenTest AutomationLaneChildGuiTest -- -j4  > logs/build-1.log 2>&1   EXIT=0
cd build/tests && ctest -R "AutomationLaneChildrenTest|AutomationLaneChildGuiTest" --output-on-failure > logs/test-lanes-1.log 2>&1   EXIT=0
# negative control: deferral guarded out (ownersHere.contains -> false), only that expression changed
cmake --build build --target AutomationLaneChildGuiTest -- -j4   > logs/build-neg.log 2>&1   EXIT=0
cd build/tests && ctest -R AutomationLaneChildGuiTest --output-on-failure > logs/test-neg.log 2>&1   EXIT=8 (expected: "the redraw after a downward owner move detached its lane")
# deferral restored; full final focused run
cmake --build build --target zene AutomationLaneChildrenTest AutomationLaneChildGuiTest ReversibilityContractTest -- -j4  > logs/build-2.log 2>&1   EXIT=0
cd build/tests && ctest -R "AutomationLaneChildrenTest|AutomationLaneChildGuiTest|ReversibilityContractTest" --output-on-failure > logs/test-lanes-2.log 2>&1   EXIT=0
cd build/tests && ctest -R ControlCommandsSnapshot --output-on-failure > logs/test-snapshot.log 2>&1   EXIT=0
```

## Gates

```
bash tests/complexity-gate.sh --check      > logs/gate-complexity.log 2>&1   EXIT=1 (pre-existing on train 5; no Q050-022 file or function listed)
bash tests/file-length-gate.sh --check     > logs/gate-filelength.log 2>&1   EXIT=1 (pre-existing on train 5; no Q050-022 file listed)
bash tests/fork-sources-gate.sh            > logs/gate-fork.log 2>&1         EXIT=0
bash tests/all-sources-reproduce.sh        > logs/gate-allsources.log 2>&1   EXIT=0 (REPRODUCES)
bash tests/no-upstream-regression-gate.sh  > logs/gate-upstream.log 2>&1     EXIT=0
tests/scripted/check-namespace             > logs/gate-namespace.log 2>&1    EXIT=0
bash tests/no-tautology-gate.sh            > logs/gate-tautology.log 2>&1    EXIT=0
python3 tests/doc-figures-gate.py          > logs/gate-docfig.log 2>&1       EXIT=0 (829 ids / 76 groups, w/ wasm.* left out)
```

Gates 4 and 7 are red on train 5 as a whole; the regression lists in
`gate-complexity.log`/`gate-filelength.log` name no Q050-022 file (checked with
`grep AutomationLaneChildGuiTest` -> 0) and no function/line of this unit, so the
unit adds nothing to either ratchet.

## Registration

`tests/src/gui/AutomationLaneChildGuiTest.cpp` is now a product-new entry in
`tests/fork-sources.txt` (as are the other Q050-022 product files, listed in
`tests/upstream-modifications.txt` where they are inherited-file changes).
`tests/all-sources.txt` is re-derived by its header recipe and reproduces.

## New commands and reversibility

None added or removed this round. `automation.lanes_set_collapsed` is still
**TrueInverse** (`src/core/ControlCommandsAutomationLanes.cpp:242`, A16 row
`src/core/ControlReversibilityTableArranger.cpp:131`); `automation.get_state`
reports read-only fields and is still NotMutating. No command surface changed,
so the snapshot is unchanged (829 ids / 76 groups, `ControlCommandsSnapshot`
EXIT 0).

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
- TSan does not run on this box (thread-creation crash). The read lock in
  `automationLaneChildren` excludes only `addTrack`/`removeTrack`; `moveTrack` and
  `clearAllTracks` mutate `m_tracks` without it (see point 2), so safety rests on
  every caller and mutator being on the GUI/control thread, not on the lock alone.
- A downward owner move past its OWN lane row is a visual no-op (the lane comes
  back beneath it): the derived order puts a child always directly under its
  owner, so the owner cannot leave its own lane above it.
