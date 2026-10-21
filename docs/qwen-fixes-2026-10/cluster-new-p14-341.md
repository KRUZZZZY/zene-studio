FIXED
new-p14-341: FIXED
new-p10-423: FIXED
new-p12-273: FIXED

new-p14-341 (src/core/UndoHistory.cpp:396): the loader validates the declared `steps` count against the step elements the section REALLY has, before anything is pushed onto the journal - so a history that declares one step and carries none is discarded with a reason instead of being accepted as an empty history. A history that declares no steps is not incomplete: it loads as nothing (found, no steps, nothing discarded).
new-p10-423 (src/core/UndoHistory.cpp:68): stripMetadataNodes now prunes a section root carrying metadata="1" (returns false) and the fingerprint skips it - the writer's own rule, so the save-time and load-time values agree.
new-p12-273 (src/core/UndoHistory.cpp:283, include/DataFile.h:117): the fingerprint covers the root's format attributes - the version and type AS THE FILE HAD THEM, which DataFile now preserves across an upgrade - and the scheme is versioned ("sha256:v2:..."), with scheme v1 still honoured while the file's format did not change under the load.

Evidence/commands (round 2, this branch):
- RED (bug 1, fix reversed): logs/test-2-red.log - `FAIL! aMissingOnlyDeclaredStepIsDiscarded() ... ({"discarded":"","found":true,"loaded_steps":0})`, Totals: 11 passed, 1 failed; EXIT=1.
- GREEN (bug 1): logs/test-3-green.log - ctest -R '^UndoHistoryPersistenceTest$' EXIT=0 (12/12 cases).
- RED (bug 2, fix reversed): logs/test-7-red.log - `FAIL! anOlderFormatVersionInAFileDiscardsItsHistory() ... ({"discarded":"","found":true,"loaded_steps":2})` and `FAIL! aCurrentFormatHistoryUsingTheOldFingerprintStillLoads() ... ({"discarded":"the project was changed ...","found":true,"loaded_steps":0})`, Totals: 8 passed, 3 failed; EXIT=3.
- GREEN (bug 2): logs/test-8-green.log - ctest -R '^UndoHistory(Persistence|Options)Test$' EXIT=0 (2/2 tests).
- feature_check.py: logs/gate-3.log EXIT=0 (all eight gates PASS).
- reversal proof: `git apply -R logs/fix-bug1.patch` -> rebuild EXIT=0, test EXIT=1 (RED); `git apply logs/fix-bug1.patch` -> EXIT=0 (GREEN). Same for logs/fix-bug2.patch -> EXIT=3 (RED) / EXIT=0 (GREEN).

## Round 2 - what Luna's REWORK of new-p14-341.r1 asked for, and where it landed

new-p14-341 - "an empty step list bypasses the count check":
- cause: src/core/UndoHistory.cpp:374 (round 1) returned before the count comparison at :401, so a history that declared `steps="1"` and carried no `<z:undostep>` was accepted with loaded_steps=0 and no discard reason.
- fix: src/core/UndoHistory.cpp:396-414 - `stepCount()` counts the section's `<z:undostep>` elements, and load() compares it with the declared `steps` BEFORE the fingerprint and before any snapshot is pushed. A declared count of 0 with no step elements is a valid empty history and still loads as nothing.
- regressions (tests/src/core/UndoHistoryPersistenceTest.cpp): `aMissingOnlyDeclaredStepIsDiscarded` (the sole declared step removed) and `anEmptyHistoryIsNotIncomplete` (a history that declares no steps loads as nothing, with nothing discarded).

new-p12-273 - "does not detect a root version changed to the previous one" and "invalidates existing histories written with the previous fingerprint":
- cause 1: DataFile::upgrade() (src/core/DataFile.cpp:2585) rewrote the root's version and type before UndoHistory fingerprinted the document (src/core/UndoHistory.cpp:233 in round 1), so the changed version disappeared and the history loaded.
- fix 1: DataFile keeps the root's `version` and `type` AS THE FILE HAD THEM - `originalFileVersion()`, `originalType()` (include/DataFile.h:117, src/core/DataFile.cpp:411 and :2772) - and says whether the load changed the format at all (`sourceFormatChanged()`, set in upgrade(), src/core/DataFile.cpp:2589). The fingerprint is measured over those, so a file whose root version was changed under it no longer matches the history's.
- cause 2: the fingerprint scheme had no marker, so the round-1 change invalidated every history written by the build before it.
- fix 2: the fingerprint value says which scheme wrote it ("sha256:v2:"), and scheme v1 ("sha256:") is still honoured - but only when the file's format did not change under the load, which is exactly what the version the file claims would have told us (UndoHistory::fingerprintMatches(), src/core/UndoHistory.cpp:404).
- regressions (tests/src/core/UndoHistoryPersistenceTest.cpp): `anOlderFormatVersionInAFileDiscardsItsHistory` (a real save, the root version set to the previous one, the song sections untouched - the history is discarded) and `aCurrentFormatHistoryUsingTheOldFingerprintStillLoads` (an unchanged current-format file carrying the previous scheme's fingerprint - the history still loads).

new-p10-423 - the metadata-root fix of round 1 is kept unchanged: `stripMetadataNodes()` (src/core/UndoHistory.cpp:68) reports a section root that carries metadata="1" (the writer drops the whole section), and the fingerprint skips such a section, so a project with a top-level metadata section does not lose its history. Held by `aTopLevelMetadataSectionDoesNotStaleTheFingerprint`.

## The test split (file-length gate)

tests/src/core/UndoHistoryPersistenceTest.cpp carried the whole feature and passed the 500-code-line gate with 24 lines to spare; the regressions of this cluster did not fit. The file is therefore split, with NO assertion changed:
- tests/src/core/UndoHistoryPersistenceTest.cpp - the format half (what a load accepts and what it discards), including every regression of this cluster;
- tests/src/core/UndoHistoryOptionsTest.cpp - the options half (the opt-in, the step and byte caps, what turning persistence off costs), moved out of the format half;
- tests/src/core/UndoHistoryTestSupport.h - the fixtures both halves build, ONE definition (the repo's existing pattern, cf. tests/src/core/TakeLaneTestSupport.h).

## Self-check
- RED/GREEN: both fixes reversed through their product patch (logs/fix-bug1.patch, logs/fix-bug2.patch), rebuilt and re-run: the new cases fail without the fix (EXIT=1 / EXIT=3) and pass with it (EXIT=0). Every command and exit code is copied from the log files above.
- Deterministic: the regressions drive the control surface (project.save / project.open) over real files and read the load report; the fingerprint cases call UndoHistory::fingerprint() directly. No sleeps, no timing, no races.
- Cause/complete: the count check moved ahead of the early return (the only caller of loadSteps()); the fingerprint takes the version and type as arguments, so the save-time and load-time values are measured the same way; DataFile's new accessors are set in every path that builds a document (the Type constructor and loadData). The undo restore path (SongUndoSnapshot.cpp) saves and restores the ProjectState and never re-enters the loader, so it is unaffected.
- No collateral: a project that did not opt in still writes no section; a history with no steps attribute is not counted; a valid steps="0" history still loads as nothing; a history written by the previous build (scheme v1) still loads when the file's format did not change under the load; a file that WAS upgraded is refused, because its sections are no longer the ones the file carries.
- Realtime/undo: nothing on the audio thread changed; the undo stack still holds one honest step per edit (the load report and the journal are untouched by the fingerprint scheme).
- Commands: no new control command was added for this fix.
