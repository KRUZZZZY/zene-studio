ALREADY-FIXED

Cluster new-p16-177, "A refused restore can destroy the revision it was trying to restore"
(`src/core/ProjectRevisions.cpp`, historical line 294-304).

The reported defect is real in the code the finding was written against, and it is already
fixed on this branch: the refusal path of `restoreProjectRevision()` no longer deletes the
staged copy, and a leftover stage is found and reused by the next attempt. What the branch did
not have is a regression for the finding's exact shape - the rotation's *first* shift already
overwrote the selected revision's slot before a *later* copy fails. This lane adds that test in
`tests/src/core/ProjectRevisionRestoreStagingTest.cpp`, the file that already owns the staging
contract.

## 1. The decision, with the code that shows it

What the finding says happens: a restore stages revision 2, the rotation copies revision 1 over
revision 2's slot, a later copy fails, the restore returns refused **and deletes the stage** -
so the bytes the user asked for are gone.

What the code on this branch does now:

| Step | Evidence |
| --- | --- |
| The rotation shifts the NEWEST slot first, exactly as the finding describes (`Keep` = 3, so `rev1 -> rev2` runs before `rev0 -> rev1`) | `src/core/ProjectRevisions.cpp:275` (`shiftRevisionsUp`, loop `for (int revision = ProjectRevisionPolicy::Keep - 1; revision > 0; --revision)`), `include/ProjectRevisions.h:56` (`Keep = 3`) |
| The stage is created under a path of its own before the rotation | `src/core/ProjectRevisions.cpp:439` (`stageRestoreTarget`), `:206` (`newRestoreStagingPath`, a `QUuid` suffix) |
| **On a refused rotation the stage is kept** - the lines the finding names, with no `QFile::remove(staging)` | `src/core/ProjectRevisions.cpp:448-454`: `if (refused) { /* The stage survives: it holds the bytes this restore took out of the set, and a retry has to be able to find them again. */ *error = reason; return false; }` |
| A leftover stage is found and REUSED by the retry instead of re-staging the (now different) slot | `src/core/ProjectRevisions.cpp:138` (`pendingRestoreStagingPath`), `:217-244` (`stageRestoreTarget` takes the pending stage and copies nothing) |
| The stage is consumed only when a restore actually succeeded | `src/core/ProjectRevisions.cpp:479` (`QFile::remove(staging)` after the replacing rename), `:474-478` (a failed replacement keeps it) |
| The contract is stated on the declaration | `include/ProjectRevisions.h:85-89` ("A failed restore keeps its stage, and a successful one consumes it.") |

The fix is on this branch in commit `6bf8d21c33` "fix(revisions): preserve staged restore
targets (cluster new-p12-185-port)" (`git log -1 6bf8d21c33` names it), the port of the accepted
fix; that lane's record is `docs/qwen-fixes-2026-10/cluster-new-p12-185-port.md`. The one pre-fix
line the finding points at - `QFile::remove(staging)` immediately before `*error = reason; return
false;` - is not in the tree. Section 3 puts it back and watches the new test go red.

Verdict: ALREADY-FIXED, not FALSE-POSITIVE - the loss is real for the historical code (section
3's red run shows it) and the current code prevents it.

## 2. The regression this lane adds

`tests/src/core/ProjectRevisionRestoreStagingTest.cpp`,
`ProjectRevisionRestoreStagingTest::aRefusedRotationKeepsTheRevisionTheRestoreSelected()`. It
drives the finding's scenario with real files, no test seam and no clock:

1. Writes the live project and revisions 0, 1 and 2 with documents of their own; revision 2 is
   the one that will be restored.
2. Makes `rev0` unreadable (`QFile::setPermissions(rev0, QFile::Permissions())`, put back by an
   RAII local). Rotation therefore succeeds on `rev1 -> rev2` - the shift over the selected slot
   - and fails on the later `rev0 -> rev1` copy, so the restore is refused **after** the selected
   slot is gone.
3. Asserts, in order: the restore refused (`false`, non-empty error); `rev2` now holds `rev1`'s
   bytes (the shift really did overwrite the selection); exactly one
   `refused-rotation.mmp.rev2.restore*` stage exists and holds the selected bytes; and a retry of
   the same revision installs those bytes into the live project instead of the ones the shift
   left in the slot.

Why no existing test caught it: the three retry cases already in this file
(`failedRestoreRetryUsesPendingStagedTarget`, `failedRestoreRetryFindsHiddenUuidStage`,
`failedRestoreRetryFindsStageForANameWithGlobCharacters`) SEED the failed state by hand with
`seedFailedRestore()`, so they never exercise the refusal path that used to delete the stage. The
new case is the only one that reaches it.

### Test matrix (step 3)

| Path | Where it is proved |
| --- | --- |
| Normal restore | `failedRestoreRetryUsesPendingStagedTarget`, `aRevisionExactlyAtTheCapStillRestores`, `restoreOfARotationIdGoesThroughTheKeepThreePolicy` - pass (`logs/test-13-family.log`) |
| Failure: shift fails after the selected slot was overwritten, stage kept, retry installs the selected bytes | **new** `aRefusedRotationKeepsTheRevisionTheRestoreSelected` - red/green in `logs/test-11-red.log`, `logs/test-12-green.log` |
| Failure: the replacement of the live file fails | `aFailedReplaceKeepsTheLiveProject`, `aFailedReplacementAfterStagingKeepsTheLiveProject`, `aFailedReplacementAfterStagingKeepsTheSlot` - pass |
| Failure: a shift copy fails, destination backup kept, refusal reported | `aFailedShiftKeepsTheBackupAndReportsARefusal` - pass |
| Exhaustion: unreadable source, stale slot that cannot be removed, restore of a vanished revision, zero-byte revision, over-cap revision, over-cap pending stage | `aFailedShiftKeepsTheBackupAndReportsARefusal`, `rotationReportsFailureToRemoveStaleRevision`, `refusalsAreTypedAndWriteNothing`, `anEmptyRevisionIsRefusedAndTheLiveProjectKept`, `anOversizedRevisionIsRefusedBeforeTheRetainedSetMoves`, `anOversizedPendingStageIsNotInstalled` - pass |
| Reconfiguration: pending stage of an earlier attempt (legacy `.restore` name, hidden `.restore.<uuid>`, a file name with glob characters), unrelated `.rotate*`/`.replace*` files left alone | the three retry cases above, `aReplaceLeavesAnUnrelatedReplaceFileAlone`, `aRotationLeavesAnUnrelatedRotateFileAlone` - pass |
| Audio thread | none of these files runs on the audio path: `ProjectRevisions.cpp` is a control-command helper that does file I/O, and this lane changes tests only - no allocation, lock, wait or log added anywhere |
| Supported platforms | the test uses `QFile::setPermissions`, `QDir::entryList` and `QString` paths only - no `std::filesystem`, no absolute-path assumption. An unreadable source is already used on the train by `aFailedShiftKeepsTheBackupAndReportsARefusal`, so the technique is not new to this suite (where a platform cannot drop read permission for the owner, e.g. running as root, that sibling would fail first - the suite is run unprivileged, uid 1000 here) |

## 3. Commands and exit codes (copied from this session's logs)

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p16-177.r1/logs`.

GREEN with the branch's code (the new case runs and passes):

```
cmake --build build --target ProjectRevisionRestoreStagingTest -j4 > .../logs/build-7-green.log 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0

cd build/tests && ctest -R "ProjectRevisionRestoreStagingTest" --output-on-failure > .../logs/test-9-green.log 2>&1; echo TEST_EXIT=$?
TEST_EXIT=0            (test-9-green.log: 1/1 Test #245 ... Passed)

./ProjectRevisionRestoreStagingTest aRefusedRotationKeepsTheRevisionTheRestoreSelected -o -,txt > .../logs/test-10-single-case.log 2>&1; echo TEST_EXIT=$?
TEST_EXIT=0            (test-10-single-case.log: "PASS : ProjectRevisionRestoreStagingTest::aRefusedRotationKeepsTheRevisionTheRestoreSelected()", Totals: 3 passed, 0 failed)
```

RED - the finding's deletion put back, to prove the test catches it:

```
git diff -- src/core/ProjectRevisions.cpp > .../logs/fix.patch; echo DIFF_EXIT=$?
DIFF_EXIT=0            (the patch re-introduces `QFile::remove(staging);` on the refusal path)

cmake --build build --target ProjectRevisionRestoreStagingTest -j4 > .../logs/build-8-red.log 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0

cd build/tests && ctest -R "ProjectRevisionRestoreStagingTest" --output-on-failure > .../logs/test-11-red.log 2>&1; echo TEST_EXIT=$?
TEST_EXIT=8
  FAIL!  : ProjectRevisionRestoreStagingTest::aRefusedRotationKeepsTheRevisionTheRestoreSelected() Compared values are not the same
     Actual   (stages.size()): 0
     Expected (1)            : 1
     Loc: [../../tests/src/core/ProjectRevisionRestoreStagingTest.cpp(223)]
```

GREEN again - the fix restored:

```
git apply -R .../logs/fix.patch; echo APPLY_R_EXIT=$?
APPLY_R_EXIT=0         (git diff --stat -- src/core/ProjectRevisions.cpp printed nothing: back to HEAD)

cmake --build build --target ProjectRevisionRestoreStagingTest -j4 > .../logs/build-9-green.log 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0

cd build/tests && ctest -R "ProjectRevisionRestoreStagingTest" --output-on-failure > .../logs/test-12-green.log 2>&1; echo TEST_EXIT=$?
TEST_EXIT=0            (test-12-green.log: 1/1 ... 100% tests passed)
```

The `ctest -R` family (`Revision|ProjectRevIds|DataFileSaveIntegrity|ProjectContainer`), after
building the targets that were `Not Run` in this build tree
(`logs/build-4-family.log`, `logs/build-5-family.log`, both EXIT=0):

```
cd build/tests && ctest -R "Revision|ProjectRevIds|DataFileSaveIntegrity|ProjectContainer" --output-on-failure > .../logs/test-13-family.log 2>&1; echo TEST_EXIT=$?
TEST_EXIT=0            10/10 tests passed (RevisionTimelineTest, ProjectRevisionRestoreStagingTest,
                       RevisionRestoreCreatedFileUndoTest, ProjectRevIdsTest, DataFileSaveIntegrityTest,
                       ProjectContainer{Test,EntriesTest,SaveTest,SaveAsGuiTest}, RevisionsDocsTest)
```

Earlier family runs `logs/test-5-family.log` and `logs/test-6-family.log` reported `Not Run` for
targets that were not built yet; `test-13` is the run that counts. An intermediate run
`logs/test-8-green.log` failed on my own test (it wrote revisions 1 and 2 but not revision 0, so
the `setPermissions` that makes the source unreadable returned false); the test now writes
`rev0` explicitly and `test-10`/`test-12` are the runs of the committed version.

Gates:

```
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-2.log 2>&1; echo GATES_EXIT=$?
GATES_EXIT=0
  PASS  fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace /
        no-tautology / doc-figures / complexity / file-length   -> RESULT: PASS
```

## 4. Collateral damage

* The product code is unchanged by this lane (`git diff --stat` after the restore step listed
  only `tests/src/core/ProjectRevisionRestoreStagingTest.cpp`). No project format, revision
  layout, command surface, reversibility class or undo step changes; old projects and saved data
  load exactly as before.
* No limit or check was added, so no other caller can be newly refused.
* The changed test file is already registered (`tests/fork-sources.txt` lists the revision test
  sources; `gates-2.log` shows `fork-sources` PASS), so no list file needed an edit.
* Undo: the test calls `restoreProjectRevision()` directly - the function `revisions.restore`,
  `project.restore_revision` and the timeline's rotation ids all route into - and pushes no undo
  step. No command was added, so the ControlCommandsSnapshot canary is untouched.

## 5. Self-check

* **The regression test FAILS with your product change reverted and PASSES with it (exit
  codes).** The product fix predates this lane, so it was reverted rather than re-added:
  `git apply` of `logs/fix.patch` re-inserted the pre-fix `QFile::remove(staging)` on the refusal
  path, rebuilt (`logs/build-8-red.log`, EXIT=0) and the test FAILED (`logs/test-11-red.log`,
  TEST_EXIT=8, `stages.size(): 0, expected 1`); `git apply -R` restored the fix
  (`APPLY_R_EXIT=0`), rebuilt (`logs/build-9-green.log`, EXIT=0) and the test PASSED
  (`logs/test-12-green.log`, TEST_EXIT=0).
* **The test is deterministic.** No wall clock, no sleeps, no threads, no seam: the "later copy
  fails" condition is a real unreadable file (`QFile::setPermissions(rev0, QFile::Permissions())`,
  restored by an RAII guard) and every comparison is against a document the test wrote.
* **The fix is at the cause and complete: every caller and sibling checked.**
  `restoreProjectRevision` is the single implementation. Its callers are
  `src/core/ControlCommandsProjectFiles.cpp:374` (`project.restore_revision`) with its recorded
  undo/redo at `:394`/`:417`, `src/core/ControlCommandsRevisions.cpp:189`/`:224` (the recorded
  undo/redo of `revisions.restore`), and the timeline's rotation ids at
  `src/core/RevisionTimeline.cpp:433`; all of them go through the one function, so all inherit the
  kept stage.
  The timeline's sibling restore of the STABLE artefacts (`.bak`, autosave, git) stages the bytes
  it read into `<project>.timeline-stage` and deletes that stage on a refused rotation
  (`src/core/RevisionTimeline.cpp:443-471`) - correctly, because its source artefact is never
  rotated away, so a retry re-reads the same bytes; there is nothing it could lose, and it is
  deliberately unchanged. `rotateProjectRevision`'s other callers
  (`src/core/ControlCommandsProjectFiles.cpp:276` in `project.save`,
  `src/core/RevisionTimeline.cpp:464`) only read its refusal flag and touch no stage.
* **No collateral damage.** Section 4: no product file changed, no new limit, registration gates
  green, `git status` clean, and the 10-test revision/project family passes.
* **ALREADY-FIXED: the code that shows it.** `src/core/ProjectRevisions.cpp:448-454` (the refusal
  keeps the stage), `:138` + `:217-244` (a pending stage is found and reused), `:479` (consumed
  only on success), contract at `include/ProjectRevisions.h:85-89`, landed in commit
  `6bf8d21c33`. Section 3's red run shows the same test fails when that deletion is put back.
* **Sibling commands in the command files the check lists** (`app.*`, `arrangement.*`): this lane
  changes no command file and no command. The check lists them because it diffs the branch
  against its train base (`fa4a5f2e7`, 654 files) rather than against this task's commit; the
  only paths this task touched are `tests/src/core/ProjectRevisionRestoreStagingTest.cpp` and
  this report, so no command, argument schema, reversibility class or canary entry moves.

## Other findings (not fixed here, not part of this finding)

* A stage kept by a refused restore is reused by a *much later* attempt at the same revision
  (`src/core/ProjectRevisions.cpp:138`), so bytes staged by an old failed attempt can be
  installed instead of the slot's current contents. The accepted p12-185 fix took that trade-off
  deliberately - after a shift the stage is the only copy of the target - and the stage is a side
  file a user can delete. If it becomes a problem, the honest fix is provenance on the stage
  (the slot hash/mtime it was taken from), not a deletion here.
* `pendingRestoreStagingPath` returns the first candidate in name order when several stages exist
  for one revision (a legacy `<rev>.restore` and a `<rev>.restore.<uuid>`); the legacy one wins.
  Deterministic, but not the newest.
