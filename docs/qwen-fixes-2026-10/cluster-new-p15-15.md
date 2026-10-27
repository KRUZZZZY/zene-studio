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
- Nothing refused the range. The filter schema is an unbounded `objectProperty()` (the transform_where / select_where
  registration in `src/core/ControlCommandsNoteShape.cpp`), so the handler is the only check. `readRanges`
  (`:282-292`) only checked that each bound was a whole number.
- Both callers read the filter through one function, `filterFrom`: select_where at
  `src/core/ControlCommandsNoteShape.cpp:479-480`, transform_where at `:553-554`. No other `"filter"` argument consumer in
  `src/core` builds a user filter with ranges (checked: `rg` finds only those two). `src/core/ControlScaleSupport.cpp:259-260`
  builds its filter from a scale context only and is not affected.

The finding's line 251 (`restoreTiming`) is not the defect. The defect was the path through `filterFrom`.

## The fix

`src/core/ControlCommandsNoteShape.cpp`:

- `anyRangeEmpty` (`:358-362`): true when any range in use has `min > max` (key, velocity, position, length). Only ranges
  whose `use*` flag is set are checked. An open side keeps the `Filter`'s default bound (`include/NoteTransform.h:66-82`),
  so the same check covers a lone bound beyond the open side.
- `filterFrom` (`:372-373`): after the whole-number check, refuses with `InvalidArgs` before any note is read or changed.
  The refusal happens before `checkpointNoteEdit`, so it takes no undo step, the same as the other filter refusals.

Both `select_where` and `transform_where` get the check, because both go through `filterFrom`.

**Round 2 change (review feedback):** the file-length gate counted `ControlCommandsNoteShape.cpp` at 502 code lines, over
the 500 limit, because the first version of the check added 14 lines. The check is now two lines and the helper is five
(`file-length-1.log`). Behaviour is unchanged. The gate no longer lists the file (`file-length-2.log`).

## Regression test

`tests/src/core/NoteShapeValidationTest.cpp:218` - `anEmptyFilterRangeIsRefusedEvenWhenInverted()`, a new slot in the
existing test (no new file, so no registration change). It covers:

- (a) the finding's filter, key 80..40 inverted and removing: `InvalidArgs`, and every note is still there.
- (b) velocity, position and length each with min above max, inverted: `InvalidArgs`.
- (c) an open side: `key_min: 200` alone and `key_max: -5` alone, inverted: `InvalidArgs`. Also `note.select_where` with the
  finding's filter: `ok == false`, `InvalidArgs`, and no note selected.
- (d) an equal bound is still legal: key 61..61 inverted and removing removes the two other notes and keeps the note at 61.

Deterministic: no timing, sleeps or threads. It is a control-command test on the message thread.

## Commands and exit codes (round 2, final code)

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-15.r2/logs`. Round-1 logs (earlier code state,
same test) are in `new-p15-15.r1/logs`.

| Step | Command (run from the stated directory) | EXIT | Log |
| --- | --- | --- | --- |
| File-length, before the round-2 edit | `bash tests/file-length-gate.sh --check` | 1 | file-length-1.log (lists `ControlCommandsNoteShape.cpp (502)`) |
| File-length, after the edit | `bash tests/file-length-gate.sh --check` | 1 | file-length-2.log (no line for this file; exit 1 is from other files, see note 3) |
| Build of the final code, all 10 targets | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-new-p15-15 cmake --build build --target <t> -j4`, for `NoteShapeValidationTest zene NoteTransformTest NoteTransformActionsTest NoteEditingCommandsTest NoteNoOpEditUndoStepTest NoteToolsTest ControlNoteNoOpUndoTest NoteSelectionIdentityTest NoteCommandTransactionsTest ControlNoteScaleVerbsTest` | 0 for each | build-r2-1.log |
| Family run, final code | `ctest -R "^(NoteTransformTest\|NoteTransformActionsTest\|NoteEditingCommandsTest\|NoteNoOpEditUndoStepTest\|NoteToolsTest\|ControlNoteNoOpUndoTest\|NoteSelectionIdentityTest\|NoteCommandTransactionsTest\|ControlNoteScaleVerbsTest\|NoteShapeValidationTest)$" --output-on-failure` (in `build/tests`) | 0 (10/10) | test-r2-green-family.log |
| RED proof: revert the whole fix against base | `git diff 7641e9493 -- src/core/ControlCommandsNoteShape.cpp > fix-r2.patch && git apply -R fix-r2.patch` | 0 | fix-r2.patch |
| RED proof: rebuild, reverted | `cmake --build build --target NoteShapeValidationTest -j4` | 0 | build-r2-revert.log |
| RED proof: run, reverted | `ctest -R NoteShapeValidationTest --output-on-failure` (in `build/tests`) | 8 | test-r2-red.log |
| Re-apply the fix | `git apply fix-r2.patch` (then `git diff 7641e9493 -- <file>` equals the patch) | 0 | build-r2-fixed.log |
| GREEN, fixed | `cmake --build build --target NoteShapeValidationTest -j4`; `ctest -R NoteShapeValidationTest --output-on-failure` (in `build/tests`) | 0; 0 | build-r2-fixed.log, test-r2-green.log |
| GREEN, direct binary | `./NoteShapeValidationTest` (in `build/tests`) | 0 | qtest-r2-direct.log |
| Gates, as shipped | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 (PermissionError, note 2) | gates-shipped-r2.log |
| Gates, log folder redirected (note 2) | `python3 $TMPDIR/gate-new-p15-15/feature_check_logdir.py <worktree>` | 0 (RESULT: PASS) | gates-r2.log |

Results:

- RED: the test fails with `Compared values are not the same` at `tests/src/core/NoteShapeValidationTest.cpp:224`. The
  finding's filter returned `ControlErrorKind::None` where `InvalidArgs` was expected. This holds with the fix reverted
  against base (test-r2-red.log), and was observed earlier in round 1 (test-red-1.log, test-red-2.log).
- GREEN: `NoteShapeValidationTest` passes. QTest reports `Totals: 11 passed, 0 failed, 0 skipped` (qtest-r2-direct.log).
- Family: 10 of 10 tests passed (test-r2-green-family.log).
- Gates, redirected copy: all eight checks PASS and `RESULT: PASS` (gates-r2.log). The changed-file list it reports is 4
  files: the three committed paths plus the product file edit that is not yet committed.

Tests executed: 1 for `NoteShapeValidationTest` (ctest) with 11 QTest slots; 10 for the family.

## Notes and choices for review

1. **Build environment.** The first build failed with `ccache: error: failed to create temporary file ... dsh/home/ccache-tmp-r6 ... Permission denied`
   (round 1, build-1.log, EXIT=2). The compile itself was not the problem. It was the ccache temp directory in this sandbox.
   I built with `CCACHE_TEMPDIR` pointed at `/home/kruzzzzy/.cache/zene-tmp/ccache-new-p15-15` and changed no build settings.
   The loop may need the same override.
2. **Gate log folder (not fixed in-jail).** `feature_check.py` creates its log folder beside the worktree
   (`<worktree>-feature-check` in the projects directory, `feature_check.py:70-71`). That is outside my write-jail, so the
   shipped script fails with `PermissionError` (gates-shipped-r2.log, EXIT=1). `submit_check.py` runs the same script, so its
   `gates (feature_check.py)` item shows FAIL for this reason alone. I did not change the script. I ran a scratch copy,
   `$TMPDIR/gate-new-p15-15/feature_check_logdir.py`, that differs from it only in two paths: the log folder is the run
   folder's `logs/gates`, and the baseline root is the original `qwen` folder. The checks are otherwise unchanged. The
   copy passes (gates-r2.log, EXIT=0). The shipped script needs to run where it can write its logs.
3. **File-length standalone.** `tests/file-length-gate.sh --check` exits 1 in this worktree because of regressions in files I did
   not touch (for example `plugins/Vst3Effect/Vst3Host.cpp`, `src/core/TakeLane.cpp`, `src/wasm/WasmSandbox.cpp`, several test files),
   visible in file-length-2.log. `feature_check.py`'s file-length item only fails on items new in changed files, and it passes.
4. **Behaviour change, deliberate.** A range with min above max is now refused even without `invert`. Before, that
   range was a silent no-op (no note matched, `matched: 0`). The finding asks for this ("reject any supplied range whose
   minimum exceeds its maximum"). Inverting was only the way it did damage.
5. **Behaviour change, wider than the finding's wording.** An open side is compared at the `Filter`'s default, so
   `key_min: 200` alone (above the key ceiling 127) and `key_max: -5` alone (below the key floor 0) are refused now. They
   matched nothing before, and with `invert` they matched everything, so they are the same bug. No existing test or GUI menu
   passes a min above max: `tests/src/core/NoteNoOpEditUndoStepTest.cpp:257` uses 60/60 (equal, still legal), and the ranges
   in `tests/src/core/NoteEditingCommandsTest.cpp` are all ordered.
6. **Not changed.** Command descriptions, the schema and the snapshot are untouched, so the canary does not move. No command
   was added.
7. **Adjacent, not investigated.** A velocity bound is parsed as `volume_t` (`uint8_t`), so `velocity_max: -5` is clamped to
   0 by `readBound` before the new check sees it. That is existing behaviour outside this finding. I did not change it.

## Self-check

### ASK items (submit_check.py)

1. **Regression test FAILS with the change reverted, PASSES with it.** Reverted against base: `ctest -R NoteShapeValidationTest`
   EXIT 8, failing at `tests/src/core/NoteShapeValidationTest.cpp:224` (test-r2-red.log). Restored: EXIT 0 (test-r2-green.log),
   and the direct binary reports `Totals: 11 passed, 0 failed` (qtest-r2-direct.log). Exit codes are from the commands in the
   table above.
2. **Deterministic.** No wall-clock time, sleep, thread or race. The test builds its clip in `freshClip()` with fixed notes
   (keys 60, 61, 67; positions 48, 12, 12; velocities 100, 40, 160) and fixed filter bounds. The only state it reads back is the
   clip's note list and the command's `ControlErrorKind`. No seam is needed: the refusal is decided by the bound values.
3. **Fix at the cause; every caller and sibling checked.** The cause is `filterFrom`, which both callers use (select_where at
   `src/core/ControlCommandsNoteShape.cpp:479-480`, transform_where at `:553-554`). The check covers all four ranges (key,
   velocity, position, length) in one function, `anyRangeEmpty` (`:358-362`). The other filter builder,
   `src/core/ControlScaleSupport.cpp:259-260`, sets only `scaleMatch` and has no ranges, so it is unaffected. No other command in
   `src/core` takes a `"filter"` argument (`rg`), so there are no further siblings to keep consistent.
4. **No collateral damage.** Saved projects and the save/load path are not touched, because `filterFrom` runs only on command
   arguments. The only new refusals are contradictory ranges: min above max, with an open side at the filter's default bound.
   The existing ranges in `tests/src/core/NoteNoOpEditUndoStepTest.cpp:257` (60/60) and in `NoteEditingCommandsTest.cpp` (all
   ordered) stay legal, and the family run passes 10 of 10. Notes 4 and 5 above list the two behaviour changes for reviewers.
5. **Verdict FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** The code evidence is in "Verdict and cause": `NoteTransform.cpp:73-79`
   (an empty range matches nothing) and `:154-161` (invert selects the complement), reached through the unchecked `filterFrom`
   path at `src/core/ControlCommandsNoteShape.cpp:364-373` before the fix.

### Other self-check items

- **Audio thread.** Nothing added runs on the audio thread. The change is a comparison in `anyRangeEmpty` and a failure return in
  `filterFrom`, both in a control-command handler on the message thread. Nothing allocates, locks, waits, does I/O or signals there.
  No audio-path code was touched.
- **Undo.** A refused filter returns before `checkpointNoteEdit`, so no undo step is taken. A valid filter is unchanged.
- **Portability.** Only comparisons and the existing `QStringLiteral` form are used in the product change. No new Qt API or platform code.
- **Gates.** The shipped `feature_check.py` exits 1 because of the log-folder write outside the jail (note 2). The same checks pass
  with the log folder redirected (gates-r2.log, EXIT 0). The file-length item no longer names this file (file-length-2.log).
