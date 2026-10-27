FIXED

Cluster new-p15-15 - "An inverted filter with contradictory bounds can remove every note" (Luna BUG 7).

## Verdict and cause

REAL, and FIXED on this branch (`bugs/qwen-new-p15-15`, base `7641e9493`).

- A filter range whose minimum is above its maximum matches no note. `invert: true` then turned that empty match
  into a match of every note, so `note.transform_where` with `action: remove` deleted the clip, and
  `note.select_where` selected all of it.
  - Empty range matches nothing: `src/core/NoteTransform.cpp:73-79` (`rangeAccepts`: `value >= low && value <= high`
    is false for every note when `low > high`).
  - Inversion selects the complement: `src/core/NoteTransform.cpp:154-161` (`select`: `matches(...) != f.invert`).
- Nothing refused the range. The filter schema is an unbounded `objectProperty()` (`src/core/ControlCommandsNoteShape.cpp`
  transform_where / select_where registration), so the handler is the only check. `readRanges` only checked that each
  bound was a whole number.
- Both callers read the filter through one function, `filterFrom`: select_where at
  `src/core/ControlCommandsNoteShape.cpp:479-480`, transform_where at `:553-554`. No other path builds a user filter with
  ranges. `src/core/ControlScaleSupport.cpp:259-260` builds its filter from a scale context only and is not affected.

The finding's line 251 (`restoreTiming`) is not the defect. The cited range handling is `readRanges`, now at
`src/core/ControlCommandsNoteShape.cpp:282-292`; the defect was in the path through `filterFrom`.

## The fix

`src/core/ControlCommandsNoteShape.cpp`:

- `anyRangeEmpty` (`:358-362`): true when any range in use has `min > max` (key, velocity, position, length). Only ranges
  whose `use*` flag is set are checked. An open side keeps the `Filter`'s default bound (`include/NoteTransform.h:66-82`),
  so the same check covers a lone bound beyond the open side.
- `filterFrom` (`:372-377`): after the whole-number check, refuses with `InvalidArgs` before any note is read or changed.
  The refusal happens before `checkpointNoteEdit`, so it takes no undo step, the same as the other filter refusals.

Both `select_where` and `transform_where` get the check, because both go through `filterFrom`.

## Regression test

`tests/src/core/NoteShapeValidationTest.cpp:218` - `anEmptyFilterRangeIsRefusedEvenWhenInverted()`, a new slot in the
existing test (no new file, so no registration change). It covers:

- (a) the finding's filter, key 80..40 inverted and removing: `InvalidArgs`, and every note is still there.
- (b) velocity, position and length each with min above max, inverted: `InvalidArgs`.
- (c) an open side: `key_min: 200` alone and `key_max: -5` alone, inverted: `InvalidArgs`. Also `note.select_where` with the
  finding's filter: `ok == false`, `InvalidArgs`, and no note selected.
- (d) an equal bound is still legal: key 61..61 inverted and removing removes the two other notes and keeps the note at 61.

Deterministic: no timing, sleeps or threads. It is a control-command test on the message thread.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-15.r1/logs`.

| Step | Command (run from the stated directory) | EXIT | Log |
| --- | --- | --- | --- |
| Build, first attempt (see note 1) | `cmake --build build --target NoteShapeValidationTest -j4` | 2 | build-1.log |
| Build, with ccache temp override | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-new-p15-15 cmake --build build --target NoteShapeValidationTest -j4` | 0 | build-2.log |
| RED, test written, fix not yet applied | `ctest -R NoteShapeValidationTest --output-on-failure` (in `build/tests`) | 8 | test-red-1.log |
| RED proof: revert the fix | `git diff -- src/core/ControlCommandsNoteShape.cpp > fix.patch && git apply -R fix.patch` | 0 | (none) |
| RED proof: build, reverted | `cmake --build build --target NoteShapeValidationTest -j4` | 0 | build-5.log |
| RED proof: test, reverted | `ctest -R NoteShapeValidationTest --output-on-failure` (in `build/tests`) | 8 | test-red-2.log |
| Re-apply the fix | `git apply fix.patch` | 0 | (none) |
| Build, fixed | `cmake --build build --target NoteShapeValidationTest -j4` | 0 | build-6.log |
| GREEN, fixed | `ctest -R NoteShapeValidationTest --output-on-failure` (in `build/tests`) | 0 | test-green-2.log |
| GREEN, direct binary | `./NoteShapeValidationTest` (in `build/tests`) | 0 | qtest-direct-green.log |
| Family build | `cmake --build build --target <t> -j4`, for `zene NoteTransformTest NoteTransformActionsTest NoteEditingCommandsTest NoteNoOpEditUndoStepTest NoteToolsTest ControlNoteNoOpUndoTest NoteSelectionIdentityTest NoteCommandTransactionsTest ControlNoteScaleVerbsTest` | 0 for each | build-7.log |
| Family run | `ctest -R "^(NoteTransformTest\|NoteTransformActionsTest\|NoteEditingCommandsTest\|NoteNoOpEditUndoStepTest\|NoteToolsTest\|ControlNoteNoOpUndoTest\|NoteSelectionIdentityTest\|NoteCommandTransactionsTest\|ControlNoteScaleVerbsTest\|NoteShapeValidationTest)$" --output-on-failure` (in `build/tests`) | 0 | test-family-1.log |
| Gates, as shipped | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | gates-1.log (see note 2) |
| Gates, log folder redirected (note 2) | `python3 <scratch>/feature_check_logdir.py <worktree>` | 0 | gates-2.log |

Results:

- RED: the test fails with `Compared values are not the same` at `tests/src/core/NoteShapeValidationTest.cpp:224`
  (the finding's filter returned `ControlErrorKind::None`, expected `InvalidArgs`). This was observed both before any
  fix (test-red-1.log) and with the fix reverted (test-red-2.log).
- GREEN: `NoteShapeValidationTest` passes, `Totals: 11 passed, 0 failed, 0 skipped` (qtest-direct-green.log).
- Family: 10 of 10 tests passed.
- Gates: all eight checks PASS and `RESULT: PASS` (gates-2.log).

Tests executed: 1 for `NoteShapeValidationTest` (ctest) with 11 QTest slots; 10 for the family.

## Notes and choices for review

1. **Build environment.** The first build failed with `ccache: error: failed to create temporary file ... dsh/home/ccache-tmp-r6 ... Permission denied`
   (build-1.log, EXIT=2). The compile itself was not the problem. It was the ccache temp directory in this sandbox. I
   built with `CCACHE_TEMPDIR` pointed at `/home/kruzzzzy/.cache/zene-tmp/ccache-new-p15-15` and changed no build settings.
   The loop may need the same override.
2. **Gate log folder.** `feature_check.py` creates its log folder beside the worktree
   (`<worktree>-feature-check` in the projects directory, `feature_check.py:70-71`). That is outside my write-jail, so the
   shipped script fails with `PermissionError` (gates-1.log, EXIT=1). I did not change the script. I ran a scratch copy,
   `$TMPDIR/gate-new-p15-15/feature_check_logdir.py`, that differs from it only in two paths: the log folder is the run
   folder's `logs/gates`, and the baseline root is the original `qwen` folder. The checks are otherwise unchanged. The
   copy passes (gates-2.log, EXIT=0). The shipped script still needs to be run from a place that can write its logs.
3. **Behaviour change, deliberate.** A range with min above max is now refused even without `invert`. Before, that
   range was a silent no-op (no note matched, `matched: 0`). The finding asks for this ("reject any supplied range whose
   minimum exceeds its maximum"). Inverting was only the way it did damage.
4. **Behaviour change, wider than the finding's wording.** An open side is compared at the `Filter`'s default, so
   `key_min: 200` alone (above the key ceiling 127) and `key_max: -5` alone (below the key floor 0) are refused now. They
   matched nothing before, and with `invert` they matched everything, so they are the same bug. I checked that no existing
   test or GUI menu passes a min above max: `tests/src/core/NoteNoOpEditUndoStepTest.cpp:257` uses 60/60 (equal, still legal),
   and the ranges in `tests/src/core/NoteEditingCommandsTest.cpp` are all ordered.
5. **Not changed.** Command descriptions, the schema and the snapshot are untouched, so the canary does not move. No command
   was added.
6. **Adjacent, not investigated.** A velocity bound is parsed as `volume_t` (`uint8_t`), so `velocity_max: -5` is clamped to
   0 by `readBound` before the new check sees it. That is existing behaviour outside this finding. I did not change it.

## Self-check

- **Each new test FAILS with the product change reverted, then PASSES restored.** Yes. The reverse patch was
  `git diff -- src/core/ControlCommandsNoteShape.cpp > fix.patch && git apply -R fix.patch` (EXIT 0), the rebuild exited 0, and
  `ctest -R NoteShapeValidationTest` exited 8 (test-red-2.log, failing at `NoteShapeValidationTest.cpp:224`). Re-applying
  the patch (EXIT 0), rebuilding (EXIT 0) and running gave EXIT 0 (test-green-2.log).
- **Acceptance criteria each proven by a test run.** The criteria are: the finding's inverted filter with a contradictory
  range is refused for removal (`anEmptyFilterRangeIsRefusedEvenWhenInverted` a and b, EXIT 0, and the family). The same for
  select_where (the same test, EXIT 0). An equal bound is still legal (the same test, d).
- **One undo step, honest reversibility.** A refused filter returns before `checkpointNoteEdit`, so no undo step is taken
  and nothing is recorded. A valid filter is unchanged. No new command was added, and nothing changed the reversibility
  table.
- **Nothing added runs on the audio thread.** The change is a comparison in `anyRangeEmpty` and a failure return in
  `filterFrom`, both in the control-command handler. Neither allocates on any audio path, and neither locks, waits, does I/O or
  signals. I did not touch any audio-path code; the change sits in `ControlCommandsNoteShape.cpp`, a message-thread command handler.
- **Old projects still load and render as before.** The change is in command argument validation. It does not touch the
  save or load path, the project format, or the render path. Old saved data never goes through `filterFrom`.
- **Portability.** Only comparisons and the existing `QStringLiteral` form are used in the product change. There is no new Qt
  API and no new platform code. The test uses `std::none_of`, which the test already has available.
- **Other callers consistent.** `filterFrom` has exactly two callers, `select_where` and `transform_where`, and both now refuse the
  same filters. `ControlScaleSupport.cpp:259-260` builds its own filter from a scale context and is not affected.
- **Gates and submit check.** `feature_check.py` as shipped exits 1 because of its log-folder write outside the jail (note 2).
  The same checks pass with the log folder redirected (gates-2.log, EXIT 0).
