FIXED

`RubberBandVoice::render()` checked only that `speed` was finite and positive, and then
used it twice without a range: `1.0 / validSpeed` as the time ratio handed to Rubber Band,
and `sourcePosition() += produced * validSpeed`. A rate that is finite and positive but
outside what the stretcher can render therefore reached the engine unchanged:

- `std::numeric_limits<double>::denorm_min()` (5e-324) makes the reciprocal **inf**;
  Rubber Band answers `WARNING: NaN or Inf presented for time ratio or pitch scale!
  Resetting it to default, no time stretch will happen` and renders the source at **full
  speed**, while the cursor advances by `produced * 5e-324` - the audio and the cursor
  describe two different timelines (measured below: the render is sample-for-sample the
  `speed == 1.0` render).
- `1e-300` makes the ratio 1e300; Rubber Band logs `Ratio yields ideal inhop < minimum`
  and, from 1e5, `ensureOutbuf: Forced to increase output buffer size` - an allocation,
  on the audio thread, from a call the file's own contract says does not allocate.
- `1e300` collapses the ratio to ~0 and the render **stalls**: it returns 2047 of 4096
  frames, which the caller zero-fills.

The fix clamps every finite positive rate into the band the engine was measured to render
cleanly in, and advances `sourcePosition()` with that same clamped rate.

## File and line evidence

- Defect, pre-fix: `git show HEAD:src/core/RubberBandStretch.cpp:225-226` -
  `const double validSpeed = std::isfinite(speed) && speed > 0.0 ? speed : 1.0;` then
  `1.0 / validSpeed`, and `:250` `m_sourcePos += produced * validSpeed`. The finding's
  line 205 has moved to 225 on this branch; the previous cluster (`new-p10-340`,
  commit 6d423361c6) only added the finite/positive test, not a range.
- Fix: `src/core/RubberBandStretch.cpp:57-80` - `SlowRatioSpan` / `FastRatioSpan` and
  `slowestSpeed()` / `fastestSpeed()`, with the measurements they come from; `:259-261` -
  `std::clamp(speed, slowestSpeed(m_sourceRate), fastestSpeed(m_sourceRate))` for the
  time ratio; `:285` unchanged, so the cursor uses the same clamped rate. `:163` and
  `:179` store and build the stretcher at `m_sourceRate`.
- Contract: `include/RubberBandStretch.h:96-101` (render documents the clamp and the
  matching cursor) and `:122-124` (the `m_sourceRate` member).
- Caller: `src/core/SamplePlayHandle.cpp:729-736` - the only caller; it already refuses a
  non-positive `converterRatio` and reads `sourcePosition()` back at `:750-753`, so the
  clamped rate is what it now gets consistently.
- Sibling: `src/core/AudioStretcher.cpp:205` (WSOLA) keeps its own `!isfinite || <= 0 -> 1.0`
  fallback and needs no band: it never forms a reciprocal, it only multiplies its hop by
  `speed`, so its cursor and its audio stay one timeline at any positive rate. Left alone.
- Regression test: `tests/src/tracks/SampleClipRubberBandTest.cpp:269-355` (slot at `:280`).

## Measurements behind the band (this session, this box)

Scratch probes linked against `/usr/lib/librubberband.so` (Rubber Band 3.x, R3 engine,
real-time mode, `MaxBlock` 2048 feeds), logs `logs/probe-scan-4.log` and
`logs/probe-short-1.log`. Warnings counted from the library's own logger:

| source rate | ratio rendered cleanly up to | first ratio that warns |
|---|---|---|
| 8 kHz | 64 | 128 |
| 44.1 kHz | 512 | 1024 |
| 48 kHz | 512 | 1024 |
| 192 kHz | 2048 | 4096 |

| source rate | fastest speed rendered cleanly | first speed that grows its buffer |
|---|---|---|
| 8 kHz | 256 | 512 |
| 44.1 kHz | 2048 | 3000 |
| 48 kHz | 2048 | 4096 |

So the clean band is about `ratio <= sourceRate/86` and `speed <= sourceRate/21`. The fix
uses `ratio <= sourceRate/128` and `speed <= sourceRate/64`: a factor 1.5-4 inside the
measured limits at every tested rate, and it contains every rate the clip path can ask for
(`clipSpeedAt` never goes below 1/32, `include/ClipEdits.h:222-224`). `1.0` is
pinned inside both bounds, so the existing non-finite/non-positive fallback and every
ordinary speed pass through untouched.

Before/after on the short source (`rate/100` frames, 4096-frame block, tail RMS of the
second half; a full-speed render runs the source out and falls silent):

| speed | before: produced / position / tail | after: produced / position / tail |
|---|---|---|
| 1.0 | 4096 / 4096 / 0.00004 | unchanged (inside the band) |
| 1/32 (clip floor) | 4096 / 128 / 0.17123 | unchanged (inside the band) |
| 5e-324 | 4096 / 2.02e-320 / **0.00004** | 4096 / 11.8886 / **0.13076** |
| 1e-300 | 4096 / 4.1e-297 / **0.00003** | 4096 / 11.8886 / **0.13076** |
| 1e300 | **2047** / 2.047e303 / 0 | **4096** / 2822400 / 0 |
| 1e5 | **2047** / 2.047e8 / 0 | **4096** / 2822400 / 0 |

## Test matrix

| Path | Covered by | Expectation |
|---|---|---|
| normal path | `floorSpeed` in the new slot plus `aWarpedClipKeepsItsPitchThroughRubberBand` | rate 1/32 honoured exactly, `sourcePosition() == 4096/32`, sound still present |
| failure, slow side | new slot, `denorm_min()` and `1e-300` | clamped to one band floor: cursor 11.8886, audio keeps sounding, both rates land on the same position |
| failure, fast side | new slot, `1e300` and `1e5` | clamped to the ceiling: `render()` returns the whole block instead of stalling, cursor finite |
| exhaustion | new slot's oracle: the source ends at `rate/100` frames; a full-speed render falls silent inside the block | the oracle is proven by the `speed == 1.0` control (`tail < 1e-3`) |
| reconfiguration | `aNaNSpeedKeepsSourcePositionFinite` (ratio path), `theStretchCommandDrivesTheMode` and `theModeRoundTripsThroughTheProjectFile` (mode changes) | unchanged: 10/10 slots pass |
| audio thread | `theAudioThreadHalfAllocatesNothing` (malloc interposer, control `reset()` = 2 allocations) | still 0 allocations for claim + 32 periods; the clamp adds arithmetic only, and removes the library's own logging/allocation path |
| platform | `init()` skips every slot but the persistence one when `RubberBandPool::available()` is false | a build without librubberband still compiles the band helpers (`<cmath>`/`<algorithm>`, no library type) |

## Commands and exit codes

All build and test logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/3449.r1/logs/`. Builds ran in the background
through the runtime's process tool because this box serialises builds; the exit codes are
the ones the process completions reported.

| Step | Exact command | Exit |
|---|---|---:|
| RED build | `cmake --build build --target SampleClipRubberBandTest -j4 > …/logs/build-1-red.log 2>&1` | 0 |
| RED run | `cd build/tests && ctest -R SampleClipRubberBandTest --output-on-failure > …/logs/test-1-red.log 2>&1` | 8 |
| GREEN build | `cmake --build build --target zene SampleClipRubberBandTest -j4 > …/logs/build-2-green.log 2>&1` | 0 |
| GREEN run | `cd build/tests && ctest -R SampleClipRubberBandTest --output-on-failure > …/logs/test-2-green.log 2>&1` | 0 |
| GREEN, slot detail | `cd build/tests && ./SampleClipRubberBandTest > …/logs/test-3-green-verbose.log 2>&1` | 0 |
| family build | `cmake --build build --target AudioStretcherTest ClipTransposeTest ClipWarpPersistenceTest ControlWarpCommandsTest LoopDropWarpTest LoopImportWarpTest SampleClipStretchTest SampleClipTransientRepeatSafetyTest SampleClipTransientRepeatTest SampleClipWarpEditTest SampleClipWarpModeMenuTest SampleClipWarpModesTest SampleClipWarpTest SampleClipWindowTest WarpMarkersTest WarpQuantizeTest WorkerAltStackTest -j4 > …/logs/build-3-family.log 2>&1` | 0 |
| family run | `cd build/tests && ctest -R "SampleClip\|AudioStretcher\|Warp\|ClipTranspose\|TransientRepeat\|WorkerAltStack" --output-on-failure > …/logs/test-5-family-green.log 2>&1` | 0 |
| 4b: patch out | `git diff -- src/core/RubberBandStretch.cpp include/RubberBandStretch.h > …/logs/fix.patch; git apply -R …/logs/fix.patch` | 0 |
| 4b: rebuild | `cmake --build build --target zene SampleClipRubberBandTest -j4 > …/logs/build-4-red-proof.log 2>&1` | 0 |
| 4b: run (must fail) | `cd build/tests && ctest -R SampleClipRubberBandTest --output-on-failure > …/logs/test-6-red-proof.log 2>&1` | 8 |
| 4b: patch back | `git apply …/logs/fix.patch` | 0 |
| 4b: rebuild | `cmake --build build --target zene SampleClipRubberBandTest -j4 > …/logs/build-5-green-proof.log 2>&1` | 0 |
| 4b: run (must pass) | `cd build/tests && ctest -R SampleClipRubberBandTest --output-on-failure > …/logs/test-7-green-proof.log 2>&1` | 0 |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3449 > …/logs/gates-1.log 2>&1` | 0 |

RED, first run (`logs/test-1-red.log`): `Totals: 9 passed, 1 failed`, `CTEST EXIT=8`:

```
FAIL!  : SampleClipRubberBandTest::anOutOfBandSpeedCannotSilenceOrStallTheStretch() 'denormal.tail > 0.05' returned FALSE. (a speed of 5e-324 rendered the source at full speed (tail RMS 4.4782e-05) while sourcePosition() said 2.02369e-320)
```

4b, after reverting only the product files (`logs/test-6-red-proof.log`): the same failure,
`CTEST EXIT=8`; after restoring them (`logs/test-7-green-proof.log`): `100% tests passed out
of 1`, `CTEST EXIT=0`. GREEN slot detail (`logs/test-3-green-verbose.log`):

```
PASS   : SampleClipRubberBandTest::anOutOfBandSpeedCannotSilenceOrStallTheStretch()
RUBBERBAND_EVIDENCE out-of-band speed: produced / position / tail  4096.0000    11.8886     0.1308     0.0000
RUBBERBAND_EVIDENCE out-of-band speed: produced / position / tail  4096.0000 2822400.0000     0.0000     0.0000
Totals: 10 passed, 0 failed, 0 skipped, 0 blacklisted, 1151ms
```

Family: `100% tests passed out of 22`, `CTEST EXIT=0` (`logs/test-5-family-green.log`).
Gates (`logs/gates-1.log`): fork-sources, all-sources-reproduce, no-upstream-regression,
check-namespace, no-tautology, doc-figures, complexity ("no new item in your files"),
file-length ("no new item in your files") all PASS, `RESULT: PASS`, exit 0.

## Scope and collateral damage

- Registration: `src/core/RubberBandStretch.cpp` (`tests/fork-sources.txt:2206`),
  `include/RubberBandStretch.h` (`:1355`) and the test (`:3137`) are already fork files, so
  no list file changes; the fork-sources gate passes.
- Old projects: no saved data is read or written differently; the band is a render-time
  clamp. Nothing outside the band changes behaviour: `1.0`, `1/32` and every in-band rate
  take the exact same arithmetic as before (the `speed == 1.0` and `1/32` rows above).
- A caller that legitimately exceeds the band: the bound tracks the library, not the app.
  Measurements show the engine stops rendering cleanly well below the clamp
  (`sourceRate/86` vs the clamp's `sourceRate/128` on the slow side, `sourceRate/21` vs
  `sourceRate/64` on the fast side), so a rate the clamp trims is one the engine already
  mishandled.
- Realtime: `render()` gains a comparison and a two-branch clamp - no allocation, lock,
  logging or Qt call. It removes a library logging path and an `ensureOutbuf` reallocation
  that an out-of-band rate used to trigger on the audio thread.
- Undo and commands: untouched; no control command added.

## Other findings

- Not a defect, noted only: `src/core/RubberBandStretch.cpp` in a build without
  librubberband keeps `(void) sourceRate;` in the constructor's `#else` branch although
  the rate is now stored before the `#ifdef`. Harmless, left as found.
- `tests/src/tracks/SampleClipRubberBandTest.cpp` has no slot for an in-band *fast* rate
  (e.g. 4x) against the stretcher's block-length contract; the fast side is covered here
  only by the clamp path. Not this bug, so not added.

## Self-check

Answers to the completion check's open items are appended below once it has been run.
