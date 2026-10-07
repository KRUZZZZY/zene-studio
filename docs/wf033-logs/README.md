# WF-033 evidence - red/green runs, regression families, cross-build check

Lane `050/feat-wf033-2026-10-06`, base `6d1a1143d`. Every run below is from `build/tests` with the
unpiped exit code (`ctest ... > log 2>&1; echo EXIT=$?`); the build is the targeted
`cmake --build build --target zene <tests>` of this tree. Design and decisions:
`docs/UNDO-HISTORY-PERSISTENCE.md`.

## 1. Red - the new test against the BASE sources

The feature's sources were set aside (a patch in the session scratchpad), the base binary and the new
test built, and the test run - `ctest -R UndoHistoryPersistenceTest --output-on-failure` -> **EXIT=8**:

```
1/1 Test #123: UndoHistoryPersistenceTest .......***Failed    0.43 sec
PASS   : UndoHistoryPersistenceTest::initTestCase()
FAIL!  : UndoHistoryPersistenceTest::offByDefaultAndAnOldProjectRoundTripsUnchanged() 'state.value(QStringLiteral("default_enabled")) == QJsonValue(false)' returned FALSE. ({"error":"no command 'project.undo_history'"})
   Loc: [../../tests/src/core/UndoHistoryPersistenceTest.cpp(307)]
FAIL!  : UndoHistoryPersistenceTest::aSavedHistoryUndoesToEveryPreEditState() 'opted.ok' returned FALSE. (no command 'project.set_undo_history')
   Loc: [../../tests/src/core/UndoHistoryPersistenceTest.cpp(338)]
FAIL!  : UndoHistoryPersistenceTest::aHistoryTheProjectNoLongerMatchesIsDiscarded() 'run(QStringLiteral("project.set_undo_history"), {{QStringLiteral("setting"), QStringLiteral("on")}}).ok' returned FALSE. ()
   Loc: [../../tests/src/core/UndoHistoryPersistenceTest.cpp(402)]
FAIL!  : UndoHistoryPersistenceTest::theCapsDropTheOldestAndAnOversizeStepIsReported() 'run(QStringLiteral("project.set_undo_history"), {{QStringLiteral("setting"), QStringLiteral("on")}}).ok' returned FALSE. ()
   Loc: [../../tests/src/core/UndoHistoryPersistenceTest.cpp(449)]
PASS   : UndoHistoryPersistenceTest::cleanupTestCase()
0% tests passed, 1 tests failed out of 1
```

## 2. Green - the same test on the feature

`ctest -R UndoHistoryPersistenceTest --output-on-failure` -> **EXIT=0**, all six slots PASS (the four
behaviours plus init/cleanup).

## 3. Red/green for the fixes found while proving it

**a. The interface box** (`UndoHistoryPanelTest::theSaveWithProjectBoxSetsTheProjectsChoice`), red with
the panel's sources at base -> **EXIT=8**:

```
PASS   : UndoHistoryPanelTest::initTestCase()
PASS   : UndoHistoryPanelTest::thePanelListsUndoesAndRedoes()
PASS   : UndoHistoryPanelTest::theLimitsRowSetsTheCapsAndTheMergeWindow()
FAIL!  : UndoHistoryPanelTest::theSaveWithProjectBoxSetsTheProjectsChoice() 'persist != nullptr' returned FALSE. ()
   Loc: [../../tests/src/gui/UndoHistoryPanelTest.cpp(134)]
PASS   : UndoHistoryPanelTest::cleanupTestCase()
0% tests passed, 1 tests failed out of 1
```
Green with the panel change -> **EXIT=0**, 5/5 slots PASS.

**b. `<head writer>` in the fingerprint.** The cross-build check (section 5) first DISCARDED a history the
base build had re-saved without any edit: `<head writer>` names the process that saved, and every build
stamps its own. The test slot was extended (another process's `writer` on an unchanged document must keep
the history) and run before the fix -> **EXIT=8**:

```
FAIL!  : UndoHistoryPersistenceTest::aHistoryTheProjectNoLongerMatchesIsDiscarded() Compared values are not the same
   Actual   (historyState().value(QStringLiteral("last_load")).toObject().value(QStringLiteral("loaded_steps")).toInt()): 0
   Expected (2)                                                                                                         : 2
0% tests passed, 1 tests failed out of 1
```
After the fix (the head's `writer` is left out of the fingerprint) -> **EXIT=0**.

**c. Id remapping.** The first green attempt of the id-remapping sequence (an edit on an UNAUTOMATED model
above a loaded history, two whole-project restores, then its redo) FAILED: a project save writes a model's
journal id only when the model is automated. Snapshots now carry every model's id (section 2 of the design
record). The mutation check - snapshots built with the ids off (`setSnapshotIds( false )`) - fails exactly
there -> **EXIT=8**:

```
FAIL!  : UndoHistoryPersistenceTest::aSavedHistoryUndoesToEveryPreEditState() 'liveContent() == afterNewEdit' returned FALSE. (the edit made above the loaded history did not redo after a restore)
0% tests passed, 1 tests failed out of 1
```
With the ids -> **EXIT=0**.

## 4. Regression families on the final build

Unit families (`ctest -R '^(...)$'`, 32 tests) -> **EXIT=0**:

```
12/32 Test #102: PartialLoadTest .......................   Passed    1.31 sec
13/32 Test #103: ProjectOpenIntegrityTest ..............   Passed    1.38 sec
14/32 Test #104: ProjectRecoveryTest ...................   Passed    0.08 sec
15/32 Test #117: ReversibilityContractTest .............   Passed    1.35 sec
16/32 Test #118: ReversibilityUndoTest .................   Passed    1.69 sec
17/32 Test #119: RevisionTimelineTest ..................   Passed    1.36 sec
18/32 Test #120: UnclaimedElementsTest .................   Passed    1.35 sec
19/32 Test #121: UnclaimedTrackTypeTest ................   Passed    1.37 sec
20/32 Test #122: UndoBoundsTest ........................   Passed    2.33 sec
21/32 Test #123: UndoHistoryPersistenceTest ............   Passed    1.33 sec
22/32 Test #126: LanesUpconvertTest ....................   Passed    1.36 sec
23/32 Test #127: ScenesUpconvertTest ...................   Passed    1.33 sec
24/32 Test #173: AutomationLanesTest ...................   Passed    1.40 sec
25/32 Test #197: UndoHonestyTest .......................   Passed    1.49 sec
26/32 Test #198: DeviceMovesUndoTest ...................   Passed    1.44 sec
27/32 Test #214: StableTrackIdsTest ....................   Passed    1.45 sec
28/32 Test #215: ProjectRevIdsTest .....................   Passed    1.52 sec
29/32 Test #216: ProvenanceSectionTest .................   Passed    1.40 sec
30/32 Test #220: TempoMapPersistenceTest ...............   Passed    1.43 sec
31/32 Test #251: UndoHistoryPanelTest ..................   Passed    1.38 sec
32/32 Test #306: SessionModelTest ......................   Passed    1.31 sec
100% tests passed, 0 tests failed out of 32
```

Socket-driven transcripts, the snapshot, the agent-surface gate and golden audio (18 tests) -> **EXIT=0**:

```
 1/18 Test #328: LuaApiSurface .......................   Passed    0.06 sec
 2/18 Test #334: ControlSessionLifecycleTranscript ...   Passed    5.36 sec
 3/18 Test #337: ControlFreezeCommandsTranscript .....   Passed   10.05 sec
 4/18 Test #338: ControlPunchTranscript ..............   Passed    2.26 sec
 5/18 Test #339: ControlTrackFolderTranscript ........   Passed   14.28 sec
 6/18 Test #347: ControlUndoStructuralTranscript .....   Passed    2.51 sec
 7/18 Test #352: ControlFeedbackCommands .............   Passed    2.02 sec
 8/18 Test #357: ControlRecordingRecovery ............   Passed    4.04 sec
 9/18 Test #372: ControlDryRunJournal ................   Passed    1.84 sec
10/18 Test #375: ControlSaveState ....................   Passed    2.01 sec
11/18 Test #379: ControlGoldenAudio ..................   Passed   21.62 sec
12/18 Test #386: ControlReversibilityTranscript ......   Passed    2.51 sec
13/18 Test #387: agent_surface .......................   Passed    5.62 sec
14/18 Test #388: ControlHeadlessProjectOpen ..........   Passed    1.90 sec
15/18 Test #394: ControlCommandsSnapshot .............   Passed    3.20 sec
16/18 Test #398: ControlMcpGroupCoverage .............   Passed    6.24 sec
17/18 Test #400: ControlSurfaceHardening .............   Passed   38.06 sec
18/18 Test #401: DocFiguresGate ......................   Passed    0.07 sec
100% tests passed, 0 tests failed out of 18
```

`ReversibilityContractTest` failed once on the way, as it must: two new rows moved the A16 table
(809 -> 811 rows for this build); the published block in `docs/RELEASE-NOTES-v0.3.0-alpha.md` was
re-taken (818/404/40/21/353 -> 820/404/41/21/354). `DeviceMovesUndoTest` failed once because this tree had
not built its `bassbooster` fixture; built, it passes. `ControlCommandsSnapshot` was regenerated from a live
instance (`tools/mcp-zene-control/snapshot_commands.py --socket`): **811 ids / 76 groups** (was 809 / 76;
added `project.set_undo_history`, `project.undo_history`; changed `project.save`'s result schema; nothing
else).

## 5. Cross-build check (a build that does not know the section)

The base binary (`6d1a1143d`, sha256 `5396ef4a...`) was kept before the first rebuild and driven over the
socket against this build's binary (driver in the session scratchpad, not committed):

```
NEW save: {"cap_bytes": 67108864, "dropped_oldest": 0, "enabled": true, "max_steps": 20, "oversize": false, "oversize_bytes": 0, "saved_bytes": 19170, "saved_steps": 3, "unavailable": 1, "warning": "", "written": true}
file steps: 3
BASE open keys: ['error_count', 'errors', 'file', 'format_upgraded', 'ids_assigned', 'loaded_with_errors', 'not_loaded', 'not_loaded_count', 'partial', 'tempo', 'track_count', 'unclaimed', 'unclaimed_count']
BASE open unclaimed: {"unclaimed": ["/song/z:undohistory"], "unclaimed_count": 1}
BASE re-save steps: 3 edited steps: 3
NEW open base-resaved: {"discarded": "", "found": true, "loaded_steps": 3} names ['TripleOscillator', 'Sample track', 'Pattern 0', 'Automation track', 'two']
undo1 True ['TripleOscillator', 'Sample track', 'Pattern 0', 'Automation track', 'one'] undo2 True ['TripleOscillator', 'Sample track', 'Pattern 0', 'Automation track', 'base'] names before edits ['TripleOscillator', 'Sample track', 'Pattern 0', 'Automation track', 'base']
NEW open base-edited: {"discarded": "the project was changed after its undo history was saved (re-saved by a build that does not read the history), so the history no longer describes it", "found": true, "loaded_steps": 0} depth 0
WORKDIR /tmp/wf033-cross-si_phwop
```

Read: the base build opens a project WITH a history, reports the section as unclaimed and re-emits it
verbatim; this build then loads that history (3 steps, both undos land on the pre-edit names). After the
base build saved an EDIT, this build discards the stale history and says why. ("unavailable 1" in the first
save is the step a fresh session's start-up project leaves below the edits, whose pre-state was never
captured.)
