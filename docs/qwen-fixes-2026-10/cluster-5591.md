FIXED

## Evidence

- `src/core/ControlCommandsAudioSlice.cpp`, `sliceFrames`: the transients branch returned `clipTransientFrames(clip)` unchanged, so the audio before the first transient (the lead-in at the clip's in-point) never became a slice. The beats branch already prepends `clip.sourceFrameAt(clip.startPosition())`.
- `slicerState` points start at 0.0, and the key mapping `base+i+2` in `buildSliceTrack` assumes slice 0 is the implicit start and slice 1 is the in-point.

## Fix

`withLeadIn(clip, transients)` (new, `src/core/ControlCommandsAudioSlice.cpp`):

- no transients: returns the list unchanged (empty, NotFound upstream);
- otherwise: prepends `clip.sourceFrameAt(clip.startPosition())` only when it is lower than the first transient.

The transients branch of `sliceFrames` now returns `withLeadIn(clip, clipTransientFrames(clip))`. The beats branch is unchanged.

## Test matrix

Tests in `tests/src/core/AudioEditingTest.cpp`. `AudioEditingTest` has 15 tests (QTest totals in `test-10-green.log`: 15 passed, 0 failed, 0 skipped).

- `slicingTransientsKeepsTheLeadIn`: new slot. Lead-in present before the first transient; saved slicer state has slice 1 at the in-point and slice 2 at the first transient after it.
- `slicingLeadInStartsAtTheTrimmedInPoint`: new slot. Trimmed in-point > 0; first boundary equals the trimmed in-point.
- `slicingToASamplerTrack`: edited, slice count 4 -> 5 (four bursts plus the lead-in); see note 2 below.
- Undo: `slicingToASamplerTrack` already calls `control.undo`; both new slots also end with `control.undo` (test file lines 343 and 383).

Not covered by a dedicated test:

- No transients: `withLeadIn` returns the empty list unchanged, so the result is still NotFound. Checked by code reading only; no slot in `AudioEditingTest.cpp` targets it.
- Beats path: not changed by this diff. No beats-specific assertion was checked by name. `AudioEditingTest` as a whole passed in `test-6.log` (1 of 1) and `test-10-green.log` (15 of 15).
- The `<` guard in `withLeadIn`: no test (see note 3).

## RED / GREEN evidence

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/5591.r1/logs`. Results below are read from the logs. Which tree each run used comes from the run notes, not from the logs.

- `test-1.log` (RED per the run notes): QTest Totals 14 passed, 2 failed. Failing: `slicingTransientsKeepsTheLeadIn`, `slicingLeadInStartsAtTheTrimmedInPoint`.
- `test-8-red.log` (fix reversed per the run notes): Totals 12 passed, 3 failed. Failing: `slicingToASamplerTrack`, `slicingTransientsKeepsTheLeadIn`, `slicingLeadInStartsAtTheTrimmedInPoint`. `slicerWaveformEditsPublishChanges` passes here.
- `test-10-green.log` (with the fix per the run notes): Totals 15 passed, 0 failed, 0 skipped; ctest `100% tests passed out of 1`.
- `test-6.log`: ctest `AudioEditingTest` Passed; `100% tests passed out of 1`.
- `test-2.log`, `test-2-final.log`: Totals 14 passed, 2 failed. Failing: `slicerWaveformEditsPublishChanges` and `slicingTransientOnTheInPointAddsNoDuplicate`. The second name is not in the current `AudioEditingTest.cpp`, so these runs used an earlier test file. They are not evidence for the current tree.
- `build-6.log`, `build-7.log`: `[100%] Built target AudioEditingTest` (line 36 of each).

Family run (unrelated failure; per the run notes, `test-7.log` is with the fix and `test-9-red.log` has the fix reversed):

- `test-7.log`: 10 of 11 passed; `ControlStableIdsSlice2` failed with `'ladspaeffect' is not in this build`.
- `test-9-red.log`: same 10 of 11; same failure and message.

`ControlStableIdsSlice2` fails in both runs, so it is not caused by this change.

## Exit codes

No log in the logs folder records an exit code. The numbered quiet-run logs for p4 (001-005) are not on disk: the duet worker tmp folder (`/home/kruzzzzy/.config/kads/zene/state/runs/5591.r1/duet/18-worker/tmp`) holds no `NNN-*.log` files. `quiet-run` writes the command's output to its log and prints `exit N` only to its own stdout. The `EXIT=` values in earlier notes do not appear in any log, so each code below is marked.

Command text is not recorded in these logs (no command header). The step labels come from the run notes.

| Step | Log | What the log shows | Exit code |
|---|---|---|---|
| fix.patch, `apply -R` | none | `fix.patch` and `fix-reapplied.patch` are patch files; no run output | not found in logs |
| apply | none | no log for this step | not found in logs |
| build 2 | `build-2.log` | AudioEditingTest built (1 match) | not found in logs |
| build 6 | `build-6.log` | `Built target AudioEditingTest` (line 36) | not found in logs |
| build 7 | `build-7.log` | `Built target AudioEditingTest` (line 36) | not found in logs |
| test 1 (RED) | `test-1.log` | Totals 14 passed, 2 failed | not found in logs |
| test 2 | `test-2.log` | Totals 14 passed, 2 failed | not found in logs |
| test 2 final | `test-2-final.log` | Totals 14 passed, 2 failed | not found in logs |
| test 6 | `test-6.log` | AudioEditingTest 1 of 1 passed | not found in logs |
| test 7 | `test-7.log` | 10 of 11 passed | not found in logs |
| test 8 (red) | `test-8-red.log` | Totals 12 passed, 3 failed | not found in logs |
| test 9 (red) | `test-9-red.log` | 10 of 11 passed | not found in logs |
| test 10 (green) | `test-10-green.log` | Totals 15 passed, 0 failed | not found in logs |
| re-check (`cmake --build` of AudioEditingTest, `ctest -R '^AudioEditingTest$'`) | none | an earlier note says it ran on the current tree; no log in the logs folder | not found in logs |

## Notes on the change

1. Existing test `slicerWaveformEditsPublishChanges` got a `QResizeEvent` after `waveform->resize`. Inferred, not probed: the widget's cached editor width stayed 248 while the test computed x positions from width 640, so the baseline drag grabbed point index 4 by accident. All assertions are unchanged, and the test passes both with and without the fix (it is not among the failures in `test-8-red.log` or `test-10-green.log`). Probe line, verbatim from `p3c-test-probe.log`:

   `PROBE pre pts: 0.0000@x0 0.0000@x0 0.0520@x33 0.2926@x187 0.5331@x341 0.7672@x490 1.0000@x640 2.3226@x1486 sourceX=33 targetX=52 width=640 editorY=64 height=128`

2. Contract change: `slicingToASamplerTrack` 4 -> 5. The old count encoded the bug (no lead-in slice).
3. The `<` guard in `withLeadIn` is defensive code that the tests do not exercise. The test slot that tried this (`slicingTransientOnTheInPointAddsNoDuplicate`) appears in `test-2.log` and `test-2-final.log` and is absent from the current file. The reason it was removed (it could not fail without the fix) and the claim that the detector reports onsets about 512 frames after the in-point are from code reading, not from logs or probes.
4. Duplicate 0.0 slice point: `slicerState`'s implicit 0.0 plus the lead-in at in-point 0 gives a zero-length slice 0. Code reading, not probed: the beats path already produces the same shape, so this is pre-existing behaviour and is unchanged. The key mapping `base+i+2` is still correct.

## Gates and file size

- `gates-2.log` (the later gate run, `RESULT: PASS`): `complexity` PASS "no new item in your files"; `file-length` PASS "no new item in your files". The log prints no CCN value, so no CCN number is recorded here.
- `gates-1.log`: same PASS lines. Neither gate log mentions lizard, so these logs do not show that the complexity gate was vacuous.
- `/tmp/kads-lizard-venv` and `/tmp/kads5591-p3c-backup`: both absent when checked on 2026-11-19. The logs do not show whether lizard was used.
- `tests/src/core/AudioEditingTest.cpp` grew from 431 to 552 raw lines (`git show fa4a5f2e78:tests/src/core/AudioEditingTest.cpp` = 431; `142394e0ec:...` = 552). The `file-length` line in `gates-2.log` does not name the file, so the log does not show that this growth was checked.

## Other findings

Not fixed here, not verified beyond reading the code:

- Reversed-source slicing: the in-point is not the sounding frame. The transients path yields about one note because of the `next <= at` skip. The beats path mixes play and buffer domains.

## Self-check

To be answered in p6.
