# cluster new-p8-41 — `note.randomize` accepts NaN jitter as a successful no-op

Verdict: **FIXED**

## The bug

`src/core/ControlCommandsNoteRandom.cpp:185-207` (`readJitter`): the JSON type
check (`value.isDouble()`) is true for NaN, and both range comparisons
(`number < minimum || number > maximum`) are false for NaN — every comparison
with NaN is false — so NaN passed the whole validator. The schema path agrees:
`note.randomize` declares `velocity_jitter`/`position_jitter` as plain
`numberProperty()` (ControlCommandsNoteRandom.cpp:449-450), and the schema
subset's `checkRange` (src/core/ControlSchema.cpp:84-100) uses the same
comparisons, so NaN reaches the handler.

Downstream, `rollNotes` (ControlCommandsNoteRandom.cpp:124 and 133) guards the
edits with `velocityJitter > 0.0` / `positionJitter > 0.0` — false for NaN — so
nothing moves, while `randomize()` (line 338-347) still takes the journal
checkpoint, calls `dataChanged()`, and answers `ControlResult::success`: a
no-op reported as an edit.

## The fix

`readJitter` now rejects non-finite values with `std::isfinite` before the
range comparisons, with a typed `InvalidArgs` refusal — the same "refused
rather than clamped" rule the function already follows (SPEC A11). `<cmath>`
was already included (line 69). No header changed; no new files.

## Regression proof

New test slot `nonfiniteJitterIsRefused()` in the existing family file
`tests/src/core/ControlNoteScaleVerbsTest.cpp` (after
`theRollRefusesWhatItCannotMean`): supplies NaN for `velocity_jitter` and for
`position_jitter` through the registry and demands a typed refusal.

RED on the original code — the failing assertion:

    FAIL!  : ControlNoteScaleVerbsTest::nonfiniteJitterIsRefused()
    '!velocityNan.ok' returned FALSE. (NaN velocity_jitter was accepted
    as a successful no-op roll)

GREEN after the fix: `100% tests passed, 0 tests failed out of 1`.

## Exact commands and exit codes (logs in this run's LOG FOLDER)

| step | command | EXIT |
|---|---|---|
| gates before | `bash tests/file-length-gate.sh --check` (gate-0-filelength-before.log) | 0 |
| gates before | `bash tests/complexity-gate.sh --check` (gate-0-complexity-before.log) | 0 |
| build test target (pre-fix) | `cmake --build build --target ControlNoteScaleVerbsTest > LOGS/build-1.log 2>&1` | 0 |
| RED run | `QT_QPA_PLATFORM=offscreen ctest -R ControlNoteScaleVerbsTest --output-on-failure > LOGS/test-1-red.log 2>&1` (from build/tests) | 8 |
| rebuild test target (post-fix) | `cmake --build build --target ControlNoteScaleVerbsTest > LOGS/build-2.log 2>&1` | 0 |
| rebuild zene | `cmake --build build --target zene > LOGS/build-3.log 2>&1` | 0 |
| GREEN run | `QT_QPA_PLATFORM=offscreen ctest -R ControlNoteScaleVerbsTest --output-on-failure > LOGS/test-2-green.log 2>&1` (from build/tests) | 0 |
| complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gate-1-complexity-after.log 2>&1` | 0 |
| file-length gate | `bash tests/file-length-gate.sh --check > LOGS/gate-1-filelength-after.log 2>&1` | 0 |

ctest family: `-R ControlNoteScaleVerbsTest` selects exactly 1 test — RED ran
1 and failed 1 (`0% tests passed, 1 tests failed out of 1`), GREEN ran 1 and
passed 1.

## Files committed

- `src/core/ControlCommandsNoteRandom.cpp` — the `std::isfinite` refusal in `readJitter`
  (already registered in `tests/fork-sources.txt:1205`; no registration change needed)
- `tests/src/core/ControlNoteScaleVerbsTest.cpp` — the regression slot (already
  registered as fork-authored; not a new file)
- `docs/qwen-fixes-2026-10/cluster-new-p8-41.md` — this report
