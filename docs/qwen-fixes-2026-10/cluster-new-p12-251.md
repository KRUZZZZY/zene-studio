# Cluster new-p12-251: SlicerT fade timing with playback speed

FALSE-POSITIVE

The finding claims `framesLeft * speedRatio` is wrong because `framesLeft` is a source-sample distance while `i`/`fadeOutFrames` are output frames, and proposes `framesLeft / speedRatio`. But `speedRatio` is the output/input ratio, not input/output:

- `AudioResampler` convention (include/AudioResampler.h:98-119): `ratio` is output/input; `src_ratio = 2.0` consumes 4096 input frames and generates 8192 output frames.
- `Sample::play` (src/core/Sample.cpp:125) multiplies `ratio` (speedRatio) into `setRatio(sampleRateRatio * freqRatio * ratio)`, so 1 input frame yields `speedRatio` output frames.
- `SlicerT::getMidi` (plugins/SlicerT/SlicerT.cpp:348) computes duration as `sampleSize * speedRatio`, confirming output = input * speedRatio.

Therefore remaining output frames at output frame `i` = `framesLeft * speedRatio - i`, which is the original formula. The proposed `/` reverses the timing (output remaining would shrink when speed > 1). The r1 commit's change to `/` is the regression; reverted to `*`.

Evidence:
- plugins/SlicerT/SlicerT.cpp:158 restored to `framesLeft * speedRatio - static_cast<int>(i)`.
- plugins/SlicerT/SlicerT.cpp:348 `outFrames = m_originalSample.sampleSize() * speedRatio`.
- include/AudioResampler.h:119 ratio convention output/input.
- src/core/Sample.cpp:125 `setRatio(sampleRateRatio * freqRatio * ratio)`.

Regression test removed: tests/src/core/SlicerTPlayheadTest.cpp `fadeTimingStaysCorrectWhenSpeedDiffers` (asserted the reversed formula).

## Evidence table

| Check | Result |
| --- | --- |
| Production reverted | plugins/SlicerT/SlicerT.cpp:158 back to `*` |
| Test removed | SlicerTPlayheadTest.cpp fade method removed |
| upstream-modifications entry removed | yes |

## Self-check

- No production code change beyond reverting the r1 patch.
- No new tests; existing SlicerT tests unchanged in behaviour.
- Gates: to be run.