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

`logs/exits.log` (written by the p4b run) is the only exit-code record. Each code below is copied from it. The numbered quiet-run logs for p4 (001-005) are not on disk (the duet worker tmp folder holds no `NNN-*.log` files), so no other exit codes exist for this work.

"proof field" means the command is the one in the p6 packet's proof fields (`red_command` / `green_command`). The p4b command text itself is not on disk, so "not on disk" means no command was recorded.

| Step | Command | Log | EXIT |
|---|---|---|---|
| patch | not on disk (patch file `logs/fix.patch`) | none | 0 |
| apply-R | `git apply -R fix-committed.patch` (proof field) | none | 0 |
| build-8 | `cmake --build build --target zene AudioEditingTest` (proof field) | `build-8.log` | 0 |
| test-11-red | `cd build/tests && ctest -R AudioEditingTest -V`, fix reversed (proof field) | `test-11-red.log` | 8 |
| test-12-red | not on disk (family run, 11 tests) | `test-12-red.log` | 8 |
| apply | not on disk (patch file `logs/fix-committed.patch`) | none | 0 |
| build-9 | `cmake --build build --target zene AudioEditingTest` (proof field) | `build-9.log` | 0 |
| test-13-green | `cd build/tests && ctest -R AudioEditingTest -V`, fix applied (proof field) | `test-13-green.log` | 0 |
| test-14-green | not on disk (family run, 11 tests) | `test-14-green.log` | 8 |
| gates-3 | not on disk | `gates-3.log` | 0 |

- `test-12-red` and `test-14-green` both fail on `ControlStableIdsSlice2` only (1 of 11), with `'ladspaeffect' is not in this build`. The failure is the same as in `test-7.log` and `test-9-red.log`, so it is not caused by this change.
- The authoritative RED/GREEN pair is `test-11-red` (EXIT=8) and `test-13-green` (EXIT=0). RED: 12 passed, 3 failed, 0 skipped, with the fix reversed via `fix-committed.patch`; the failing tests are `slicingToASamplerTrack`, `slicingTransientsKeepsTheLeadIn`, `slicingLeadInStartsAtTheTrimmedInPoint`. GREEN: 15 passed, 0 failed, 0 skipped.
- The earlier logs have no exit codes: `test-1.log`, `test-2.log`, `test-2-final.log`, `test-6.log`, `test-7.log`, `test-8-red.log`, `test-9-red.log`, `test-10-green.log`, and the build logs `build-2.log`, `build-6.log`, `build-7.log`. Their results are in the RED / GREEN evidence section above.
- `test-1.log` is the pre-fix RED of the two new slots (`slicingTransientsKeepsTheLeadIn`, `slicingLeadInStartsAtTheTrimmedInPoint`). It has no exit code.

## Notes on the change

1. Existing test `slicerWaveformEditsPublishChanges` got a `QResizeEvent` after `waveform->resize`. Inferred, not probed: the widget's cached editor width stayed 248 while the test computed x positions from width 640, so the baseline drag grabbed point index 4 by accident. All assertions are unchanged, and the test passes both with and without the fix (it is not among the failures in `test-8-red.log` or `test-10-green.log`). Probe line, verbatim from `p3c-test-probe.log`:

   `PROBE pre pts: 0.0000@x0 0.0000@x0 0.0520@x33 0.2926@x187 0.5331@x341 0.7672@x490 1.0000@x640 2.3226@x1486 sourceX=33 targetX=52 width=640 editorY=64 height=128`

2. Contract change: `slicingToASamplerTrack` 4 -> 5. The old count encoded the bug (no lead-in slice).
3. The `<` guard in `withLeadIn` is defensive code that the tests do not exercise. The test slot that tried this (`slicingTransientOnTheInPointAddsNoDuplicate`) appears in `test-2.log` and `test-2-final.log` and is absent from the current file. The reason it was removed (it could not fail without the fix) and the claim that the detector reports onsets about 512 frames after the in-point are from code reading, not from logs or probes.
4. Duplicate 0.0 slice point: `slicerState`'s implicit 0.0 plus the lead-in at in-point 0 gives a zero-length slice 0. Code reading, not probed: the beats path already produces the same shape, so this is pre-existing behaviour and is unchanged. The key mapping `base+i+2` is still correct.

## Gates and file size

- `gates-3.log` (`EXIT=0` in `exits.log`, `RESULT: PASS`): `complexity` PASS "no new item in your files"; `file-length` PASS "no new item in your files".
- Lizard is not installed: `command -v lizard` EXIT=1 and `python3 -I -c 'import lizard'` EXIT=1, checked 2026-11-19. So the CCN of `withLeadIn` (3, by code reading) is not measured by the tool, and the `complexity` PASS may not have measured anything. Not verified: the logic of `feature_check.py` was not read.
- `gates-2.log` (no exit code in `exits.log`, `RESULT: PASS`): same PASS lines. The log prints no CCN value, so no CCN number is recorded here.
- `gates-1.log`: same PASS lines. No gate log mentions lizard.
- `/tmp/kads-lizard-venv` and `/tmp/kads5591-p3c-backup`: both absent when checked on 2026-11-19. The logs do not show whether lizard was used.
- `tests/src/core/AudioEditingTest.cpp` grew from 431 to 552 raw lines (`git show fa4a5f2e78:tests/src/core/AudioEditingTest.cpp` = 431; `142394e0ec:...` = 552). The `file-length` line in `gates-2.log` does not name the file, so the log does not show that this growth was checked.

## Other findings

Not fixed here, not verified beyond reading the code:

- Reversed-source slicing: the in-point is not the sounding frame. The transients path yields about one note because of the `next <= at` skip. The beats path mixes play and buffer domains.

## Self-check

`submit-1.log`: no FAIL; five `?` items, answered below (`submit-1 EXIT=0` in `exits.log`).

1. **Fails reverted, passes with the change (exit codes).** Yes. `test-11-red` EXIT=8 (`exits.log`); `test-11-red.log:396` "Totals: 12 passed, 3 failed, 0 skipped", and `slicingTransientsKeepsTheLeadIn` is one of the 3 failures. `test-13-green` EXIT=0; `test-13-green.log:396` "Totals: 15 passed, 0 failed, 0 skipped". The logs do not record the tree state; the fix reversed via `fix-committed.patch` comes from the packet's proof field, not from a log.
2. **Deterministic.** The test uses fixed synthetic audio, `test::burstAudio(200, {58, 106, 154})` (`tests/src/core/AudioEditingTest.cpp:305`), and asserts the detector count (`:311`). The slice request is injected with the `at` argument (`:314-317`). `AudioEditingTest.cpp` has no sleep, `qWait`, timer, clock or random call (grep, no matches). Not verified: the body of `test::burstAudio` was not read.
3. **Fix at the cause; callers and siblings checked.** The defect was in the transients branch of `sliceFrames`; the fix is `ControlCommandsAudioSlice.cpp:211` (`withLeadIn(clip, clipTransientFrames(clip))`), and `withLeadIn` is at `:199-205`. The beats branch already starts at the in-point (comment `:207-208`). Callers (grep over `src`): `sliceFrames` is called only from `sliceToInstrument` (`:263`); `withLeadIn` only from `sliceFrames`. `clipTransientFrames` is not changed by the product commit (`git show --stat 142394e0ec` lists `ControlCommandsAudioSlice.cpp`, the test file and the report only), so its other callers (`ClipTransients.cpp:56`, `ControlCommandsTransportMap.cpp:526`, `ControlCommandsWarpQuantize.cpp:102`, `SampleClip.cpp:776`, `SampleClipView.cpp:237`, `ControlCommandsAudioSlice.cpp:138`) keep their behaviour. The `transients` command (`:131-141`) reports detector output and is unchanged. Not verified: no test runs the other callers. Reversed-source slicing is a separate finding (see "Other findings").
4. **No collateral damage.** Partly, with one real effect. (a) Saved projects: `sliceFrames` has one caller, a command handler (`:263`), and no load path calls it (grep). Slices already saved stay as stored. Not verified: whether recorded commands are replayed on load (the replay code was not read). (b) Limit: the existing cap `MaxInstrumentSlices` (`:62`, message at `:299` says "at most 69 slices") is applied after `sliceFrames` (`:265-266`). The lead-in takes one of those slots. A clip with 69 transients and an in-point before the first one now yields 70 frames, is cut to 69, drops its last transient and reports `capped: true` (`:273`). Before the fix such a clip was not capped. This is not covered by a test and is not changed here; fixing it needs a product change (the cap check or the lead-in count).
5. **ALREADY-FIXED or FALSE-POSITIVE verdict.** Not applicable: the verdict is FIXED (line 1 of this report).
