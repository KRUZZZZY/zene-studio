IMPLEMENTED

# Cluster Q050-022 - Show automation lanes as track children

All acceptance criteria met and proven by tests run in this session. Every log
referenced below is a NEW numbered file under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-022.r3/logs`.

## What the unit asked for

`automation.lane_add` already stored `followsTrack`; the arranger drew the lane
as a peer row, never indented, never hidden with its owner, and the agent surface
did not report the relation (R-workflow-197: lanes "do not fold with or nest
inside the owning track"). This change makes the lane a collapsible child of its
owner in the arranger, keeps the ownership consistent through hide/fold/move/
delete/undo/redo, and reports the same state through `automation.get_state`.

## Acceptance evidence

All tests: `ctest -R <name> --output-on-failure` from
`<worktree>/build/tests`, with `QT_QPA_PLATFORM=offscreen` and the lane's
`LD_LIBRARY_PATH`; the environment is set by ctest for the GUI test and inherited
here.

| # | Criterion | Test (exact command) | How it proves it | Exit |
|---|-----------|----------------------|------------------|------|
| 1 | lane with followsTrack rendered as a collapsible child of its owner | `ctest -R AutomationLaneChildGuiTest` -> `theLaneIsDrawnDirectlyUnderItsOwner` | row index(lane) == row index(owner)+1; lane settings column indented further | 0 |
| 1 | the child is collapsible | same ctest -> `foldingTheOwnerHidesItsChildLane` | `automation.lanes_set_collapsed` true hides the lane row, keeps the owner; false brings it back | 0 |
| 2 | collapsing/hiding the owner hides its child lanes | same ctest -> `hidingTheOwnerHidesItsChildLane` | `setVisible(false)` + `realignTracks` hides both rows and drops both from the hit test | 0 |
| 2 | deleting the owner keeps lane ownership consistent | `ctest -R AutomationLaneChildrenTest` -> `deletingTheOwnerRemovesLanesAndUndoRestoresThem` | `track.remove` removes owner+1 lane as one step; ONE undo restores both with the lane still following the owner and its point intact; redo removes both | 0 |
| 2 | moving the owner keeps lane ownership consistent | same ctest -> `reorderingTheOwnerKeepsLaneOwnership` | after `track.move`, the lane is still present and `automation.get_state` still reports the owner | 0 |
| 2 | undoing/redoing keeps lane ownership consistent | `foldingIsOneUndoStepAndRoundTrips` (fold) + `deletingTheOwnerRemovesLanesAndUndoRestoresThem` (delete) | undo and redo both deposit the same owner/lane state | 0 |
| 3 | GUI test verifies child-lane selection/editing | `ctest -R AutomationLaneChildGuiTest` -> `theChildLaneIsSelectableAndEditable` | the lane's `AutomationClipView` is drawn on the lane row, a click selects it, and an edit is visible in `automation.get_state` | 0 |
| 3 | agent surface reports the same owner and lane state | `ctest -R AutomationLaneChildGuiTest` -> `theAgentReportsTheDrawnOwnerAndLaneState`; `ctest -R AutomationLaneChildrenTest` -> `theSurfaceReportsTheOwnerAndTheChildCount` | `automation.get_state` reports `follows`/`automation_lane_count`/`lanes_collapsed`; folding through the command changes the drawn row to the same state | 0 |
| 4 | fold flag + follow survive save/load | `AutomationLaneChildrenTest` -> `followAndFoldSurviveSaveAndLoad` | `autolanes=1` on the owner element and `follows` on the lane element reload to the same owner | 0 |

Exact commands and observed exits:

```
unset LMMS_PLUGIN_DIR; export QT_QPA_PLATFORM=offscreen ... (lane environment)
cmake --build build --target s AutomationLaneChildrenTest AutomationLaneChildGuiTest -- -j4 > logs/build-4.log 2>&1   EXIT=0
cd build/tests && ctest -R AutomationLaneChildrenTest --output-on-failure > logs/test-core-1.log 2>&1   EXIT=0
cd build/tests && ctest -R AutomationLaneChildGuiTest --output-on-failure > logs/test-gui-4.log 2>&1   EXIT=0
cmake --build build --target zene -- -j4 > logs/build-8.log 2>&1   EXIT=0
cmake --build build --target ReversibilityContractTest -- -j4 > logs/build-9.log 2>&1   EXIT=0
cd build/tests && ctest -R "ReversibilityContractTest|ControlCommandsSnapshot" --output-on-failure > logs/test-rev-snap.log 2>&1   EXIT=8 (ReversibilityContractTest)
# after updating the A16 histogram block:
cd build/tests && ctest -R ReversibilityContractTest --output-on-failure > logs/test-rev-2.log 2>&1   EXIT=0
# ControlCommandsSnapshot passed in test-rev-snap.log (EXIT=0 for that test)
python3 tests/doc-figures-gate.py > logs/doc-figures-1.log 2>&1   EXIT=1 -> logs/doc-figures-2.log EXIT=0 after README canary 828->829
python3 research/reviews/m1-session-2026-10-12/scratch/m0/snapshot_live.py $PWD > logs/snapshot.log 2>&1   EXIT=0
```

## Acceptance table (per-criterion test names and exits)

| Criterion (verbatim) | Test name | Exit |
|----------------------|-----------|------|
| "An automation lane with followsTrack is rendered as a collapsible child of its owning track in the arranger." | `theLaneIsDrawnDirectlyUnderItsOwner`, `foldingTheOwnerHidesItsChildLane` | 0 |
| "Collapsing or hiding the owner hides its child lanes; moving, deleting, undoing, and redoing the owner keep lane ownership consistent." | `foldingTheOwnerHidesItsChildLane`, `hidingTheOwnerHidesItsChildLane`, `reorderingTheOwnerKeepsLaneOwnership`, `deletingTheOwnerRemovesLanesAndUndoRestoresThem`, `foldingIsOneUndoStepAndRoundTrips` | 0 |
| "A GUI test verifies child-lane selection/editing and the agent surface reports the same owner and lane state." | `theChildLaneIsSelectableAndEditable`, `theAgentReportsTheDrawnOwnerAndLaneState`, `theSurfaceReportsTheOwnerAndTheChildCount` | 0 |

## Gates (unpiped exits)

`bash tests/complexity-gate.sh --check` and `bash tests/file-length-gate.sh --check`
are red on train 5 by design; per the train-5 rule the requirement is that no file
of this lane is newly listed. Verified by grepping the gate output for every file
this lane touched: none appears (`logs/gate-complexity-2.log`,
`logs/gate-filelength-2.log`). The one regression this lane DID introduce
(`journalTrackRemovalWithLanes` CCN 12) was refactored into `captureGroup`/
`restoreGroup`/`removeGroup` before the final run.

```
bash tests/complexity-gate.sh --check   > logs/gate-complexity-2.log 2>&1   EXIT=1 (pre-existing train-5 red; no lane file listed)
bash tests/file-length-gate.sh --check  > logs/gate-filelength-2.log 2>&1   EXIT=1 (pre-existing train-5 red; no lane file listed)
bash tests/fork-sources-gate.sh         > logs/gate-fork.log 2>&1           EXIT=0
tests/scripted/check-namespace          > logs/gate-namespace.log 2>&1      EXIT=0
bash tests/no-tautology-gate.sh         > logs/gate-tautology.log 2>&1      EXIT=0
python3 tests/doc-figures-gate.py       > logs/doc-figures-2.log 2>&1       EXIT=0
```

`tests/all-sources-reproduce.sh` is exercised by `fork-sources-gate` and printed
`REPRODUCES` (EXIT=0). `bash tests/no-upstream-regression-gate.sh` is run in the
final check block below; every inherited file this lane changed is declared in
`tests/upstream-modifications.txt`.

## New commands and reversibility

- `automation.lanes_set_collapsed` - **TrueInverse** (live Track checkpoint; the
  owner's own `autolanes` attribute). `src/core/ControlCommandsAutomationLanes.cpp:318`,
  A16 row `src/core/ControlReversibilityTableArranger.cpp:131`.
- `automation.get_state` gained read-only fields `follows`, `automation_lane_count`,
  `lanes_collapsed` (still NotMutating).
- Snapshot regenerated live: 829 ids / 76 groups (was 828/76), README canary and
  the A16 histogram block updated in the same change.

## Format changes

New owner attribute `autolanes` on `<track>`, written only when folded and reset
on absence. Old projects load and render exactly as before; the new data
round-trips (proved by `followAndFoldSurviveSaveAndLoad`).

## Known limits

- The arranger order is derived: `realignTracks` draws each owner's lane rows
  directly beneath it, while the flat container order is unchanged.
- Folding is a view flag with no audio meaning; a folded lane still plays.
- TSan does not run on this box; the new code is control/GUI-thread only.

## Final check

```
bash tests/complexity-gate.sh --check      EXIT=1  (pre-existing; no lane file listed)
bash tests/file-length-gate.sh --check     EXIT=1  (pre-existing; no lane file listed)
bash tests/fork-sources-gate.sh            EXIT=0
bash tests/all-sources-reproduce.sh        EXIT=0
bash tests/no-upstream-regression-gate.sh  EXIT=0
tests/scripted/check-namespace             EXIT=0
bash tests/no-tautology-gate.sh            EXIT=0
```

Logs: `gate-complexity-2.log`, `gate-filelength-2.log`, `gate-fork.log`,
`gate-namespace.log`, `gate-tautology.log`, `gate-upstream.log`,
`doc-figures-2.log`, `snapshot.log`, `test-core-1.log`, `test-gui-4.log`,
`test-rev-2.log`.
