FIXED

Cluster: new-p12-44-port-port (port of the accepted fix 4a525311f "fix(session): land checkpoints one track once so one undo takes the whole pass", cluster new-p12-44-port). The accepted fix could not go on the newest train (merge conflict in src/core/ControlCommandsSessionRecordLand.cpp), so it was re-applied by hand on this branch.

## The finding

`session.arrangement_record_land` checkpointed the song track once per landed clip instead of once per touched track, so a pass that lands N clips pushed N journal steps. A step the undo stack's own bound evicts is gone, so the checkpoint holding the track's state before the FIRST clip could be dropped while the later one survived - and one `control.undo` then restored only the later checkpoint, leaving an earlier landed clip behind. This is the finding's "when" verbatim: "Two recorded launches land on the same track; an undo can restore only the later checkpoint, leaving an earlier landed clip behind."

## Evidence (the bug on this branch, before the fix)

- `src/core/ControlCommandsSessionRecordLand.cpp:353` (pre-fix) - `track->addJournalCheckPoint();` inside `sessionrecord::landPairState()`, which the land handler calls ONCE PER PAIR (`src/core/ControlCommandsSessionRecordLand.cpp:433`). One checkpoint per clip.
- The intended contract said the opposite: `src/core/ControlReversibilityTableSessionView.cpp:119-131` (pre-fix) - "the inverse is the Track's own journal checkpoint ... taken once per touched track before its first clip is created", and `include/ControlReversibility.h:340-343` (pre-fix) - "one Track journal checkpoint per touched track".
- Measured RED (see the commands below): with the undo stack's count bound at 1, one `control.undo` after a two-clip pass left 1 clip on the track (`Actual (m_first->getClips().size()): 1`, `Expected (size_t{0}) : 0`), and a pass at the stack's default bound evicted 2 older steps instead of 1 (`Actual (journal->evictedSteps() - evictedBefore): 2`, `Expected (1) : 1`).

## The fix

- `src/core/ControlCommandsSessionRecordInternal.h:175` - `landTouchTrack()`: the track a pair will create its clip on, under the SAME two gates `landPairState()` applies before it writes (an in-range column, a span with a length) plus that column's own entry being there; nullptr when the pair touches no track.
- `src/core/ControlCommandsSessionRecordInternal.h:191` - `checkpointLandTracks()`: ONE composite `ProjectJournal` checkpoint (the existing `addJournalCheckPoint(const QVector<JournallingObject*>&)` seam) over the DISTINCT touched tracks, taken BEFORE the pass creates any clip. One step for the whole pass, whatever it lands.
- `src/core/ControlCommandsSessionRecordLand.cpp:353` - the per-pair `track->addJournalCheckPoint();` is gone; `src/core/ControlCommandsSessionRecordLand.cpp:429` takes the composite checkpoint once, before the per-pair loop creates any clip.
- `src/core/ControlReversibilityTableSessionView.cpp:119-131` and `include/ControlReversibility.h:340-343` - the contract row and its prose now name the mechanism the code uses (one composite ProjectJournal checkpoint covering all touched tracks).

No new control command, no saved-data format change, nothing on the audio thread.

## The regression test

`tests/src/core/ControlSessionRecordLandTest.cpp` (the closest area's existing test file; no test includes this file's header, so the test lives beside the other `session.arrangement_record_land` tests):

- `oneUndoTakesEveryLandedClipBackEvenAtTheUndoCap()` (line 443) - the user-visible shape of the finding: the undo stack's count bound is set to ONE step through the journal's own API (the seam `control.set_undo_depth` uses, so no cap-change transaction is recorded and the undo under test is the land's own step), two launches on one track land two clips, and ONE `control.undo` must take BOTH back.
- `aPassAtTheUndoCapsBoundIsStillOneUndo()` (line 468) - the ported accepted test: the pass pushes ONE checkpoint, so at the stack's bound it evicts ONE older step of the user's own history, not one per clip.

Both are deterministic: no wall-clock timing, no sleeps, no races. The seam is `ProjectJournal::setMaxUndoStates()` / `ProjectJournal::evictedSteps()` / `ProjectJournal::undoDepth()` - the engine's own undo accounting - plus the recorder ring filled directly as the scheduler fills it.

## Commands and exit codes (every log is in the run's LOG FOLDER)

RED - the new test against the unfixed product code:

- `cmake --build build --target ControlSessionRecordLandTest -j 16 > logs/build-red-1.log 2>&1` - EXIT=0
- `ctest -R ControlSessionRecordLandTest --output-on-failure > logs/test-red-1.log 2>&1` (from build/tests) - **EXIT=8** - 13 passed, 2 failed:
  - `oneUndoTakesEveryLandedClipBackEvenAtTheUndoCap()` - Actual 1, Expected 0 (tests/src/core/ControlSessionRecordLandTest.cpp:463)
  - `aPassAtTheUndoCapsBoundIsStillOneUndo()` - Actual 2, Expected 1 (tests/src/core/ControlSessionRecordLandTest.cpp:491)

GREEN - the same test against the fixed product code:

- `cmake --build build --target zene ControlSessionRecordLandTest -j 16 > logs/build-green-2.log 2>&1` - EXIT=0
- `ctest -R ControlSessionRecordLandTest --output-on-failure > logs/test-green-2.log 2>&1` (from build/tests) - **EXIT=0** - 1/1 test passed

The affected family (the land test, the reversibility contract/undo tests, the undo bound/honesty tests, the session record and follow tests):

- `ctest -R "ControlSessionRecordLandTest|ReversibilityContractTest|ReversibilityUndoTest|UndoBoundsTest|UndoHonestyTest|SessionArrangementRecordTest|SessionFollowTest" --output-on-failure > logs/test-family-2.log 2>&1` (from build/tests) - **EXIT=0** - 7/7 tests passed

## Step 4b - the proof that the test catches the bug

- `git diff -- src/core/ControlCommandsSessionRecordLand.cpp src/core/ControlCommandsSessionRecordInternal.h src/core/ControlReversibilityTableSessionView.cpp include/ControlReversibility.h > logs/fix.patch` - EXIT=0 (164 lines)
- `git apply -R logs/fix.patch` - EXIT=0 (product code back to the unfixed state, test kept)
- `cmake --build build --target zene ControlSessionRecordLandTest -j 16 > logs/build-revert-2.log 2>&1` - EXIT=0
- `ctest -R ControlSessionRecordLandTest --output-on-failure > logs/test-revert-2.log 2>&1` (from build/tests) - **EXIT=8** - 13 passed, 2 failed (the two new tests)
- `git apply logs/fix.patch` - EXIT=0
- `cmake --build build --target zene ControlSessionRecordLandTest -j 16 > logs/build-reapply-2.log 2>&1` - EXIT=0
- `ctest -R ControlSessionRecordLandTest --output-on-failure > logs/test-reapply-2.log 2>&1` (from build/tests) - **EXIT=0** - 1/1 test passed

## Gates

- `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` - EXIT=0 (all rows PASS) - logs/feature-check-2.log
- `ctest -R ControlCommandsSnapshot --output-on-failure` (from build/tests) - EXIT=0, 1/1 passed - logs/test-snapshot-1.log (the command surface is unchanged: no id was added, renamed or removed)

## Files committed

- `src/core/ControlCommandsSessionRecordInternal.h` (the two helpers + their includes)
- `src/core/ControlCommandsSessionRecordLand.cpp` (the per-pair checkpoint removed, the composite one taken, the three texts)
- `src/core/ControlReversibilityTableSessionView.cpp` (the contract row's mechanism text)
- `include/ControlReversibility.h` (the row's prose in the header)
- `tests/src/core/ControlSessionRecordLandTest.cpp` (the two regression tests)
- `docs/qwen-fixes-2026-10/cluster-new-p12-44-port-port.md` (this report, registered in `tests/fork-sources.txt`)

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reverted: `ctest -R ControlSessionRecordLandTest` EXIT=8 (two failures, logs/test-revert-2.log). Restored: EXIT=0 (logs/test-reapply-2.log). The pre-fix run of the same test is logs/test-red-1.log (EXIT=8) and the post-fix run is logs/test-green-2.log (EXIT=0).
- **The test is deterministic.** No wall-clock timing, no sleeps, no races, no threads. The undo bound is set and read through the engine's own accounting (`ProjectJournal::setMaxUndoStates()`, `undoDepth()`, `evictedSteps()`), and the recorded events are written into the recorder ring directly, as the scheduler writes them.
- **The fix is at the cause and complete.** The cause is the checkpoint's POSITION (inside the per-pair `landPairState()`); it is now taken once, before the per-pair loop (`src/core/ControlCommandsSessionRecordLand.cpp:429`). Callers and siblings checked: `landPairState()` has exactly one caller - the land handler's per-pair loop (`src/core/ControlCommandsSessionRecordLand.cpp:437`) - so no other path still checkpoints per clip; `checkpointLandTracks()` reproduces `landPairState()`'s own gates in `landTouchTrack()` (in-range column, span with a length, the column's own track entry) so a pair that lands nothing is not checkpointed either; the touched-track set is de-duplicated, so a track touched by several pairs is captured once. The wider type the value flows into is the undo stack itself: the composite uses the EXISTING `ProjectJournal::addJournalCheckPoint(const QVector<JournallingObject*>&)` seam (include/ProjectJournal.h:120), the same one-object-per-step shape `Track::addJournalCheckPoint()` produced, so an undo restores every touched track exactly as before. The contract table (`src/core/ControlReversibilityTableSessionView.cpp`) and its prose (`include/ControlReversibility.h`) name the new mechanism.
- **No collateral damage.** No saved-data or project format changes, no new command, no new limit: the pass now pushes ONE step where it pushed N, so it evicts FEWER older steps of the user's history than before (1 instead of N at the bound). A pass that touches no track pushes no step at all, as before (no pair -> no checkpoint). The only behaviour that changes is the one the finding names.
- **Realtime, undo and commands.** Nothing here runs on the audio thread: the checkpoint is taken on the command's own (UI) thread, in the handler, exactly where the per-pair checkpoint used to be; the recorder ring is written by the audio thread but only READ here. The undo stays ONE honest step - the pass's single composite step restores every touched track, and the recorded transaction still names the FIRST clip for `clip.delete`. No new control command was added, so the ControlCommandsSnapshot canary does not move.
- **Sibling commands in the files I changed.** `src/core/ControlCommandsSessionRecordLand.cpp` registers exactly one command (`session.arrangement_record_land`); `src/core/ControlCommandsSessionRecordInternal.h` is a private header shared with `src/core/ControlCommandsSessionRecord.cpp` (the TAP half - arm/status/back-to-arrangement), which does not land clips and is not affected; `src/core/ControlReversibilityTableSessionView.cpp` holds the six `session.*` rows, of which only the land row is touched - the other five (`arrangement_record_arm`, `arrangement_record_status`, `back_to_arrangement`, `follow_set`, `follow_get_state`) are not_mutating rows and are unchanged.
- **The ALREADY-FIXED / FALSE-POSITIVE question does not apply here**: the verdict is FIXED. The code that shows the bug WAS present on this branch is `src/core/ControlCommandsSessionRecordLand.cpp:353` before the fix (`track->addJournalCheckPoint();` inside `landPairState()`, called once per pair), and the intended contract it contradicted is `src/core/ControlReversibilityTableSessionView.cpp:119-131` ("taken once per touched track before its first clip is created"). Both were measured RED on this branch (logs/test-red-1.log).
- **The command surface did not move.** The land command's description and the contract row's prose changed, but no command was added, renamed or removed: `ctest -R ControlCommandsSnapshot --output-on-failure` (from build/tests) - EXIT=0, 1/1 passed (logs/test-snapshot-1.log). The snapshot ratchet compares command IDS, and the id set is unchanged.
