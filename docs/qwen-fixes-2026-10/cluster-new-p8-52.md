# cluster new-p8-52 — the sample-generation cap double-counts stereo channels

Verdict: **FIXED**

## Finding

`sample.generate`'s allocation guard estimated the buffer as
`frames * 2 * sizeof(SampleFrame)`, but `SampleFrame` is already a full
stereo frame, so the channel count was applied twice and the usable
generation limit was halved (256 MiB instead of the documented 512 MiB).

## Evidence

- `src/core/ControlCommandsSample.cpp:155` (original):
  `const qint64 bytes = static_cast<qint64>(spec->frames) * 2 * static_cast<qint64>(sizeof(SampleFrame));`
  compared against `MaxGeneratedBytes = 512ll * 1024 * 1024`
  (`src/core/ControlCommandsSample.cpp:76`).
- `include/SampleFrame.h:189`: the frame's storage is
  `std::array<sample_t, DEFAULT_CHANNELS> m_samples;` — one frame already
  holds every channel.
- `include/lmms_constants.h:38`: `inline constexpr auto DEFAULT_CHANNELS = ch_cnt_t{2};`
  so `sizeof(SampleFrame)` already covers both channels; the extra `* 2`
  double-counted them.
- The handler allocates exactly `spec.frames` `SampleFrame`s
  (`sampleops::generate(..., spec.frames, ...)` →
  `std::vector<SampleFrame>` at `src/core/SampleOperators.cpp:127`), so the
  honest byte estimate is `frames * sizeof(SampleFrame)`.

## Fix

`src/core/ControlCommandsSample.cpp:155` — drop the channel multiplier:

    const qint64 bytes = static_cast<qint64>(spec->frames) * static_cast<qint64>(sizeof(SampleFrame));

with a comment naming why. No other behaviour touched; the existing
oversize-guard slot (`guardsAndDeferredVerbsAreNamed`, MaxSongLength ask)
still refuses under the corrected estimate, so it passes unchanged.

## Regression

New slot `theCapCountsStereoFramesOnce()` in the existing
`tests/src/core/ControlSampleOperatorTest.cpp`: computes the smallest
whole-tick length whose real byte size sits under the 512 MiB cap while the
double-counted estimate crosses it (fixture-derived, not a lucky constant),
and asserts the command ACCEPTS it and reports the expected frame count.
Before the fix the command refused it (RED); after the fix it succeeds
(GREEN).

## Commands and exit codes (logs in the run's LOG FOLDER)

| step | command | EXIT |
|---|---|---|
| build test target (original code; first attempt hit a test-compile error, `QJsonValue` has no `toLongLong`) | `cmake --build build --target ControlSampleOperatorTest > LOGS/build-1.log 2>&1` | 2 |
| build test target (original code, test corrected) | `cmake --build build --target ControlSampleOperatorTest > LOGS/build-2.log 2>&1` | 0 |
| RED run | `cd build/tests && ctest -R ControlSampleOperatorTest --output-on-failure > LOGS/test-1.log 2>&1` | 8 |
| rebuild with fix | `cmake --build build --target ControlSampleOperatorTest > LOGS/build-3.log 2>&1` | 0 |
| GREEN run | `cd build/tests && ctest -R ControlSampleOperatorTest --output-on-failure > LOGS/test-2.log 2>&1` | 0 |
| complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gates-1.log 2>&1` | 0 |
| file-length gate | `bash tests/file-length-gate.sh --check >> LOGS/gates-1.log 2>&1` | 0 |

RED evidence (test-1.log): `1/1 Test #44: ControlSampleOperatorTest ... ***Failed`,
`Totals: 10 passed, 1 failed, 0 skipped`, failing assertion
`theCapCountsStereoFramesOnce() 'generated.ok' returned FALSE. (within the 512 MiB cap but refused: 85219 ticks at the engine's rate would be 536879696 bytes of samples, over this verb's 512 MiB generation cap ...)`.

GREEN evidence (test-2.log): `100% tests passed, 0 tests failed out of 1`
(11/11 internal slots passed, including the pre-existing oversize-guard slot).

Family: `ctest -R ControlSampleOperatorTest` executed 1 test binary (11 slots)
in both runs.

## Files committed

- `src/core/ControlCommandsSample.cpp` (the one-line cap fix; already registered in `tests/fork-sources.txt`)
- `tests/src/core/ControlSampleOperatorTest.cpp` (the regression slot; already registered in `tests/CMakeLists.txt`)
- `docs/qwen-fixes-2026-10/cluster-new-p8-52.md` (this report)

No new product files, so `tests/fork-sources.txt` and
`tests/upstream-modifications.txt` needed no changes.
