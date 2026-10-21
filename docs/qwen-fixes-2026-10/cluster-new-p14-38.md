FIXED
new-p14-38: FIXED
new-p17-19: FIXED

## What was wrong

Both reports come from ONE shared cause: a conditional-checkpoint note transform that
returned its normal reply - which carries the `__transaction` descriptor that advertises
`control.undo` - even when it had changed nothing. `note.invert` had the second half of
the same shape: it counted the notes it was GIVEN rather than the notes it MOVED.

Shared cause, fixed in the place all of them go through:

- `src/core/ControlCommandsNoteTools.cpp:78-90` (`invert`): counts the notes whose target
  key differs from their current key (`changedCount`), returns early with `inverted: 0`
  when nothing moves - BEFORE `clip->addJournalCheckPoint()` and before `finish(...)`,
  so no checkpoint is taken and no `__transaction` is advertised. When notes do move it
  reports `inverted: changedCount`, so a note that stays on the pivot key is no longer
  counted as inverted.
- `src/core/ControlCommandsNoteTools.cpp:247-250` (`transposeInKey`): a zero-step
  transpose in key now returns `moved: 0` before the checkpoint and before `finish(...)`,
  so it advertises no undo transaction.
- `src/core/ControlCommandsNoteTransform.cpp:252, 296, 338` (`transpose`, `velocityOffset`,
  `velocityScale`): the `__transaction` descriptor is inserted only when the transform
  actually changed something. The checkpoint was already conditional (`if (changes)` at
  lines 234, 286, 328), so the descriptor and the checkpoint now agree.

The rule the fix implements: a call that changed nothing must leave no journal step AND
advertise no reversible transaction, because the advertised inverse (`control.undo`) would
otherwise undo the PREVIOUS edit.

## Regression tests (tests/src/core/ControlNoteScaleVerbsTest.cpp)

Every test below fails when the production change is reversed and passes with it.

| Test | Line | Proves |
| --- | --- | --- |
| `invertNoOpDoesNotAdvertiseAnUndo` | 598 | a no-op invert records NO transaction (transaction count only - the moved-count assertion lives in its own test, so this test cannot fail early on the count and never reach the transaction assertion) |
| `invertNoOpReportsNoNotesInverted` | 611 | a no-op invert reports `inverted: 0` and leaves the key alone |
| `invertCountsOnlyNotesThatMove` | 649 | of three notes (60, 62, 64) only the two that move are counted (new-p17-19) |
| `zeroStepTransposeInKeyDoesNotAdvertiseAnUndo` | 534 | a zero-step `note.transpose_in_key` records NO transaction |
| `zeroVelocityOffsetDoesNotAdvertiseAnUndo` | 623 | a zero-delta `note.velocity_offset` records NO transaction |
| `identityVelocityScaleDoesNotAdvertiseAnUndo` | 636 | a factor-1.0 `note.velocity_scale` records NO transaction |
| `zeroSemitoneTransposeDoesNotAdvertiseAnUndo` | 521 | a zero-semitone `note.transpose` records NO transaction |

Deterministic: the transaction count is read from `control.transactions` before and after
the call; no sleeps, no clocks, no races.

## Commands run (exact, with exit codes)

Build (product + focused test target):

```
cmake --build build --target zene ControlNoteScaleVerbsTest -j12   # EXIT=0
```

RED - production changes reversed only (`git diff HEAD~1 HEAD -- src/core/ControlCommandsNoteTools.cpp src/core/ControlCommandsNoteTransform.cpp > fix.patch; git apply -R fix.patch`):

```
cmake --build build --target zene ControlNoteScaleVerbsTest -j12   # EXIT=0
ctest -R ControlNoteScaleVerbsTest --output-on-failure              # EXIT=8
```

Failing test functions in the reversed run (7 FAIL, 0 passed):
`zeroSemitoneTransposeDoesNotAdvertiseAnUndo`, `zeroStepTransposeInKeyDoesNotAdvertiseAnUndo`,
`invertNoOpDoesNotAdvertiseAnUndo`, `invertNoOpReportsNoNotesInverted`,
`zeroVelocityOffsetDoesNotAdvertiseAnUndo`, `identityVelocityScaleDoesNotAdvertiseAnUndo`,
`invertCountsOnlyNotesThatMove`.

The four ISOLATED transaction-count tests failed on the transaction count itself
(actual count one higher than expected), e.g.
`invertNoOpDoesNotAdvertiseAnUndo` at ControlNoteScaleVerbsTest.cpp(607): Actual (after) 72, Expected (before) 71.

GREEN - production changes restored (`git apply fix.patch`):

```
cmake --build build --target zene ControlNoteScaleVerbsTest -j12   # EXIT=0
ctest -R ControlNoteScaleVerbsTest --output-on-failure              # EXIT=0  (1/1 ctest passed, 0 failed)
```

Affected family (built and run from `build/tests`):

```
cmake --build build --target ControlVerbInverseTest ControlSuccessPathsTest ControlNoteExpressionCommandsTest NoteTransformTest NoteRandomTest ControlEditCommandsTest ControlRegistryTest -j12   # EXIT=0
ctest -R "ControlNoteScaleVerbsTest|ControlVerbInverseTest|ControlSuccessPathsTest|ControlNoteExpressionCommandsTest|NoteTransformTest|NoteRandomTest|ControlEditCommandsTest|ControlRegistryTest" --output-on-failure   # EXIT=0, 8/8 tests passed
```

Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>   # EXIT=0, RESULT: PASS (all 9 gates PASS)
```

Logs (this run): `logs/build-1.log`, `logs/build-2-reversed.log`, `logs/build-3-restored.log`,
`logs/build-4-family.log`, `logs/test-red-1.log`, `logs/test-green-1.log`, `logs/test-green-2.log`,
`logs/test-family-1.log`, `logs/gate-feature-check.log`, `logs/fix.patch`.

## Files committed

- `src/core/ControlCommandsNoteTools.cpp` - invert counts moved notes and returns before the
  checkpoint when nothing moves; transposeInKey returns before the checkpoint when nothing moves.
- `src/core/ControlCommandsNoteTransform.cpp` - the three transforms advertise a `__transaction`
  only when they changed something.
- `tests/src/core/ControlNoteScaleVerbsTest.cpp` - the regressions above (already registered in
  `tests/upstream-modifications.txt`).
- `docs/qwen-fixes-2026-10/cluster-new-p14-38.md` - this report.

## Self-check

- Does the test fail without the fix? YES - see the RED run above (EXIT=8, 7 failing test
  functions, each of the four isolated transaction-count tests failing on the count).
  Logs: `logs/test-red-1.log`. RED ctest exit 8 / GREEN ctest exit 0.
- Is the test deterministic (no wall-clock, sleeps or races)? YES - the seam is the
  `control.transactions` command's `count` field, read immediately before and after the
  call under test; the notes are fixed keys (60, 62, 64) and a fixed scale ("Major", root 0).
  Nothing in these tests reads a clock, sleeps, or races on another thread.
- Is the fix at the cause and complete? YES - the shared conditional-checkpoint pattern is
  fixed in the handlers themselves, not in a caller: `note.invert` and `note.transpose_in_key`
  in src/core/ControlCommandsNoteTools.cpp, and `note.transpose`, `note.velocity_offset`,
  `note.velocity_scale` in src/core/ControlCommandsNoteTransform.cpp. Those five are every
  conditional-checkpoint transform in this area; the sibling note edits (slide_set/clear,
  randomize, note.add, note.move) either always change something or already guard their
  checkpoint, and their existing regressions (`clearingSlidesOnAClipWithoutAnyRecordsNoUndoStep`,
  `theThreeTransformsMoveNotesAndUndoPutsThemBack`) still pass unchanged.
- No collateral damage? NONE - only a call that changed NOTHING takes a different path.
  A transform that moves a note still checkpoints, still advertises its transaction and still
  undoes in one step. No saved-data format, no new limit or check on other callers, no new
  control command, and old projects load exactly as before.
- Verdict ALREADY-FIXED / FALSE-POSITIVE? NOT APPLICABLE - both verdicts are FIXED, proved
  by the RED run above (the pre-fix code advertises the undo and miscounts the inverted notes).
- Realtime? The changed code is command-handler code on the control thread; nothing new
  allocates, frees, locks, waits or logs on the audio thread. Undo stays one honest step.
- Gates? `feature_check.py` EXIT=0, RESULT: PASS, all nine gates PASS.

