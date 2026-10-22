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
otherwise undo the PREVIOUS edit. The sibling no-work paths `note.flam`
(ControlCommandsNoteTools.cpp:203) and `note.rotate` (:313) already return before their
checkpoints when they have nothing to add or rotate. `note.arpeggiate` (:151) replaces
notes, and `note.repeat` (:286) adds copies, so their checkpoints remain unconditional.

## The regression test: tests/src/core/ControlNoteNoOpUndoTest.cpp (fork-NEW, ctest #475)

The whole no-op rule lives in one suite, so the proof is one target:

| Test | Line | Proves |
| --- | --- | --- |
| `noOpInvertAdvertisesNoUndo` | 138 | a no-op invert records NO transaction - both the transaction COUNT and the transaction ON TOP are asserted, so the test cannot pass by failing early on another field |
| `noOpInvertReportsNoNotesInverted` | 150 | a no-op invert reports `inverted: 0` and leaves the key alone |
| `invertCountsOnlyTheNotesThatMove` | 160 | of three notes (60, 62, 64) only the two that move are counted (new-p17-19) |
| `noOpTransposeInKeyAdvertisesNoUndo` | 176 | a zero-step `note.transpose_in_key` records NO transaction |
| `noOpTransposeAdvertisesNoUndo` | 198 | a zero-semitone `note.transpose` records NO transaction |
| `noOpVelocityOffsetAdvertisesNoUndo` | 210 | a zero-delta `note.velocity_offset` records NO transaction |
| `noOpVelocityScaleAdvertisesNoUndo` | 222 | a factor-1.0 `note.velocity_scale` records NO transaction |
| `aTransformThatChangesSomethingStillAdvertisesItsUndo` | 235 | the POSITIVE control: a transform that did change something still records its transaction, still advertises `control.undo` and still undoes in one step (a guard that swallowed every transaction would pass every other test here and fail this one) |

The seams are the registry's own: the transaction count from `control.transactions`, the
transaction on top from `ControlRegistry::lastTransaction()`, the notes from
`roll.get_state`. Deterministic: nothing reads a clock, sleeps or races.

Registration (same commit): `tests/CMakeLists.txt` (the file's entry in LMMS_TESTS),
`tests/fork-sources.txt` (the fork-NEW source), `tests/all-sources.txt` (re-derived with
the manifest's own recipe - `bash tests/all-sources-reproduce.sh` prints REPRODUCES),
`tests/upstream-modifications.txt` (the reason for the CMakeLists.txt change).

## Why the round-2 proof was invisible (and what changed)

Round 2 put the regressions in `tests/src/core/ControlNoteScaleVerbsTest.cpp`, whose ctest
number is 78. The negative control reads the test list out of `ctest -N` with the pattern
`Test #\d+: (\S+)`, and ctest pads the number column to the width of the largest test
number - so a two-digit number is printed as `Test   #78:` and does NOT match that
pattern, while a three-digit one (`Test #138:`, `Test #475:`) does. The control therefore
never saw ControlNoteScaleVerbsTest, rebuilt the app alone (the test binaries were stale),
and ran four OTHER suites - ControlSuccessPathsTest, ControlNoteExpressionCommandsTest,
NoteRandomTest and NoteTransformTest - which do not exercise the command layer at all.
They pass in both states BY DESIGN (measured: 8/8 pass with the product change reverted,
logs/test-family-3-reverted.log) and prove nothing about this fix; they are not part of
the proof.

The regressions now live in a fork-NEW target of their own, `ControlNoteNoOpUndoTest`
(ctest #475), which the control does see and does rebuild. `tests/src/core/ControlNoteScaleVerbsTest.cpp`
is back at its upstream state, so the regressions have one home.

## Commands run (exact, with exit codes)

Build (product + the proof target):

```
cmake --build build --target zene ControlNoteNoOpUndoTest -j12   # EXIT=0
```

RED - the production change reversed only
(`git diff b61e5f965 HEAD -- src/core/ControlCommandsNoteTools.cpp src/core/ControlCommandsNoteTransform.cpp > fix.patch; git apply -R fix.patch`):

```
cmake --build build --target zene ControlNoteNoOpUndoTest -j12   # EXIT=0
ctest -R ControlNoteNoOpUndoTest --output-on-failure              # EXIT=8
```

7 of the 8 test functions fail with the change reverted, and each fails on the field it
claims: the four no-op transaction tests on the transaction COUNT (one higher than
expected, e.g. `noOpInvertAdvertisesNoUndo` at ControlNoteNoOpUndoTest.cpp(141): Actual
(transactionCount()) 4, Expected (before) 3), the two count tests on `inverted` (1 vs 0 and
3 vs 2). The positive control passes in both states, as it must. Log: `logs/test-red-3.log`.

GREEN - the production change restored (`git apply fix.patch`):

```
cmake --build build --target zene ControlNoteNoOpUndoTest -j12   # EXIT=0
ctest -R ControlNoteNoOpUndoTest --output-on-failure              # EXIT=0, 1/1 ctest passed (8 test functions)
```

Wider family (a sanity check, NOT the proof - these suites do not touch the changed code):
the invocation is the first line of the log.

```
logs/test-family-2.log      # with the fix:      8/8 passed, EXIT=0
logs/test-family-3-reverted.log  # without it: 8/8 passed, EXIT=0 - the same in both states
```

Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>   # EXIT=0, RESULT: PASS
```

Logs (this run): `logs/build-5-newtest.log`, `logs/build-6-newtest.log`, `logs/build-7-reversed.log`,
`logs/build-8-restored.log`, `logs/build-9-family.log`, `logs/build-10-reversed-family.log`,
`logs/build-11-restored-family.log`, `logs/test-red-3.log`, `logs/test-green-3.log`,
`logs/test-green-4.log`, `logs/test-green-5.log`, `logs/test-family-2.log`,
`logs/test-family-3-reverted.log`, `logs/fix.patch`.

## Files committed

- `src/core/ControlCommandsNoteTools.cpp` - invert counts moved notes and returns before the
  checkpoint when nothing moves; transposeInKey returns before the checkpoint when nothing moves.
- `src/core/ControlCommandsNoteTransform.cpp` - the three transforms advertise a `__transaction`
  only when they changed something.
- `tests/src/core/ControlNoteNoOpUndoTest.cpp` (fork-NEW) - the proof suite above.
- `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt`,
  `tests/upstream-modifications.txt` - the registrations the new test target needs.
- `docs/qwen-fixes-2026-10/cluster-new-p14-38.md` - this report.

## Self-check

- Does the test fail without the fix? YES - RED ctest EXIT=8 with 7 of the 8 test functions
  failing, each on the field it claims (the four transaction tests on the transaction COUNT,
  the two count tests on `inverted`); GREEN ctest EXIT=0. Logs: `logs/test-red-3.log`,
  `logs/test-green-4.log`, `logs/test-green-5.log`.
- Is the test deterministic (no wall-clock, sleeps or races)? YES - the seams are the
  registry's own state: the transaction COUNT from `control.transactions`, the transaction
  ON TOP from `ControlRegistry::lastTransaction()`, the notes from `roll.get_state`. The
  notes are fixed keys (60, 62, 64) and the scale is fixed ("Major", root 0). Nothing here
  reads a clock, sleeps or waits on another thread.
- Is the fix at the cause and complete? YES - the shared conditional-checkpoint pattern is
  fixed in the handlers themselves: `note.invert` and `note.transpose_in_key` in
  src/core/ControlCommandsNoteTools.cpp, and `note.transpose`, `note.velocity_offset`,
  `note.velocity_scale` in src/core/ControlCommandsNoteTransform.cpp. Those five are every
  conditional-checkpoint transform in this area; the sibling no-work paths (`flam` and
  `rotate`) retain their early returns, while `arpeggiate` and `repeat` retain their
  unconditional checkpoints because they replace notes or add copies.
- No collateral damage? NONE - only a call that changed NOTHING takes a different path. A
  transform that moves a note still checkpoints, still advertises its transaction and still
  undoes in one step (proved by the positive control in the new suite and by the existing
  `theThreeTransformsMoveNotesAndUndoPutsThemBack` regression). No saved-data format, no new
  limit or check on other callers, no new control command, and old projects load as before.
- Verdict ALREADY-FIXED / FALSE-POSITIVE? NOT APPLICABLE - both verdicts are FIXED, proved
  by the RED run above.
- Realtime? The changed code is command-handler code on the control thread; nothing new
  allocates, frees, locks, waits or logs on the audio thread. Undo stays one honest step.
- Gates? `feature_check.py` EXIT=0, RESULT: PASS - all nine gates PASS.
