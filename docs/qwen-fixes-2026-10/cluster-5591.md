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

Tests in `tests/src/core/AudioEditingTest.cpp`:

- `slicingTransientsKeepsTheLeadIn`: lead-in present before the first transient; saved slicer state has slice 1 at the in-point and slice 2 at the first transient after it.
- `slicingLeadInStartsAtTheTrimmedInPoint`: trimmed in-point > 0; first boundary equals the trimmed in-point.
- `slicingToASamplerTrack`: slice count 4 -> 5 (four bursts plus the lead-in); see note 2 below.
- Packet coverage also includes: no transients left unchanged; beats path unchanged; undo covered by an existing assertion.

Verification status: the test run below passes all 15 sub-checks. The individual assertions for the no-transient and beats cases were not re-read in this session.

## RED / GREEN evidence

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/5591.r1/logs`.

- `test-1.log` (RED, before the fix): AudioEditingTest 14 passed, 2 failed.
- `test-8-red.log` (fix reversed): AudioEditingTest 12 passed, 3 failed. Recorded exit code EXIT=8 (from the run notes; the log file itself does not record exit codes).
- `test-10-green.log` (with the fix): AudioEditingTest 15 passed, 0 failed, 0 skipped. Recorded exit code EXIT=0 (same caveat).
- `build-6.log`, `build-7.log`: AudioEditingTest linked ("Built target AudioEditingTest").
- Re-check on the current tree, this session: `cmake --build build --target AudioEditingTest` exit 0; `ctest -R '^AudioEditingTest$' --output-on-failure` (from `build/tests`) exit 0, 100% passed out of 1.

Family run (unrelated failure, same in both):

- `test-7.log` (with the fix): 10 of 11 passed; `ControlStableIdsSlice2` failed with `'ladspaeffect' is not in this build`.
- `test-9-red.log` (fix reversed): same 10 of 11; `ControlStableIdsSlice2` failed with the same message.

`ControlStableIdsSlice2` fails with or without this fix, so it is not caused by this change.

## Notes on the change

1. Existing test `slicerWaveformEditsPublishChanges` got a `QResizeEvent` after `waveform->resize`. The widget's cached editor width stayed 248 while the test computed x positions from width 640. With the stale width, the baseline drag grabbed point index 4 by accident. All assertions are unchanged, and the test passes both with and without the fix (`test-8-red.log`, `test-10-green.log`). Probe output (`p3c`): 0.0520 at x=33 with the width-640 layout. The packet also records the widget reading 0.133 at x=33; that value was not found in the logs directory when searched in this session.
2. Contract change: `slicingToASamplerTrack` 4 -> 5. The old count encoded the bug (no lead-in slice).
3. The `<` guard in `withLeadIn` is defensive code that the tests do not exercise. The detector reports onsets about 512 frames after the in-point, so the burst fixture cannot place a transient on the in-point. The test slot that tried this was removed because it could not fail without the fix.
4. Duplicate 0.0 slice point: `slicerState`'s implicit 0.0 plus the lead-in at in-point 0 gives a zero-length slice 0. The beats path already produces the same shape, so this is pre-existing behaviour and is unchanged. The key mapping `base+i+2` is still correct.

## Other findings

Not fixed here, not verified beyond reading the code:

- Reversed-source slicing: the in-point is not the sounding frame. The transients path yields about one note because of the `next <= at` skip. The beats path mixes play and buffer domains.

## Self-check

To be answered in p6.
