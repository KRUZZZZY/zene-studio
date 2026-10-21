FIXED
new-p14-341: FIXED
new-p10-423: FIXED
new-p12-273: FIXED

new-p14-341 (src/core/UndoHistory.cpp:397): the loader validates the declared `steps` count against the step elements the section REALLY has, before anything is loaded - so a history that declares one step and carries none is discarded with a reason instead of being accepted as an empty history. A history that declares no steps is not incomplete: it loads as nothing (found, no steps, nothing discarded).
new-p10-423 (src/core/UndoHistory.cpp:62): stripMetadataNodes now prunes a section root carrying metadata="1" (returns false) and fingerprint skips it - the writer's own rule, so the save-time and load-time values agree.
new-p12-273 (src/core/UndoHistory.cpp:233): the fingerprint now covers the root's format attributes - see the round-2 evidence below.

Evidence/commands:
- RED: ctest -R '^UndoHistoryPersistenceTest$' EXIT=8 (anIncompleteSavedHistoryIsDiscarded loaded_steps=1; fingerprint mismatches)
- GREEN: same ctest EXIT=0 (13/13 tests passed)
- feature_check.py EXIT=0
- git apply -R .../fix.patch then rebuild/test RED EXIT=8; git apply ... GREEN EXIT=0

## Round 2 (Luna's REWORK of new-p14-341.r1)

new-p14-341 - the empty step list bypassed the count check:
- cause: src/core/UndoHistory.cpp:374 (round 1) returned before the count comparison at :401, so a history that declared `steps="1"` and carried no `<z:undostep>` was accepted with loaded_steps=0 and no discard reason.
- fix: src/core/UndoHistory.cpp:396-414 - `stepCount()` counts the section's `<z:undostep>` elements, and load() compares it with the declared `steps` BEFORE the fingerprint and before any snapshot is pushed. A declared count of 0 with no step elements is a valid empty history and still loads as nothing.
- regressions (tests/src/core/UndoHistoryPersistenceTest.cpp): `aMissingOnlyDeclaredStepIsDiscarded` (one declared step, the step element removed - the sole-step removal) and `anEmptyHistoryIsNotIncomplete` (a history that declares no steps loads as nothing, with nothing discarded).
- RED (fix reverted): logs/test-2-red.log - `FAIL! aMissingOnlyDeclaredStepIsDiscarded() ... ({"discarded":"","found":true,"loaded_steps":0})`, Totals: 11 passed, 1 failed; EXIT=1.
- GREEN: logs/test-1b.log - Totals: 12 passed, 0 failed; EXIT=0. ctest -R '^UndoHistoryPersistenceTest$' -> logs/test-3-green.log, EXIT=0.
- reversal proof: `git diff -- src/core/UndoHistory.cpp > logs/fix-bug1.patch && git apply -R logs/fix-bug1.patch` -> rebuild EXIT=0, test EXIT=1 (RED); `git apply logs/fix-bug1.patch` -> rebuild EXIT=0, test EXIT=0 (GREEN).

## Self-check
- RED/GREEN: reversed product hunks via fix.patch, rebuilt UndoHistoryPersistenceTest, ctest EXIT=8 with the new tests failing; restored, ctest EXIT=0.
- Deterministic: tests use injected DOM attribute counts and direct UndoHistory::fingerprint calls; no sleeps/timing/races.
- Cause/complete: loadSteps count check before push; stripMetadataNodes root handling mirrored in fingerprint; root version/type hashed. Callers (beginLoad/seal) unchanged.
- No collateral: missing `steps` attribute skips validation; old projects without undohistory unaffected; fingerprint change only affects new saves.
