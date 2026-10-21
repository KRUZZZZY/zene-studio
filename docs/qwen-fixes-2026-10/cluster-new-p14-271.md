# Cluster new-p14-271: the SampleDecoder's Ogg and bounded-decode defects

new-p14-269: FIXED
new-p14-271: FIXED
new-p14-276: FIXED

Verdict: **FIXED** for all three bugs of this batch - each with its own
decision, its own regression test and its own RED/GREEN proof

Same-file batch: all three findings are in `src/core/SampleDecoder.cpp`.

## Bug new-p14-269 — Stereo Ogg imports lose half their frames and mix the wrong channels

**Verdict: FIXED.** This is REAL.

### Evidence

`src/core/SampleDecoder.cpp`, `decodeSampleOggVorbis()` (the Ogg Vorbis
decoder, the fallback `SampleDecoder::decode()` reaches when libsndfile cannot
read the file). Before the fix the read loop was:

```cpp
auto buffer = std::vector<float>(numSamples);
...
std::copy_n(*output, samplesRead, buffer.begin() + totalSamplesRead);   // channel 0 only
...
auto result = std::vector<SampleFrame>(totalSamplesRead / numChannels);
for (auto i = std::size_t{0}; i < result.size(); ++i)
{
	if (numChannels == 1) { result[i] = {buffer[i], buffer[i]}; }
	else if (numChannels > 1) { result[i] = {buffer[i * numChannels], buffer[i * numChannels + 1]}; }
}
```

`ov_read_float()` returns **planar** data: `output[c][i]` is channel `c`'s
`i`-th frame. The old code copied channel 0 into `buffer` and then indexed
`buffer` as if it were interleaved, so a stereo file lost half of its frames
(`totalSamplesRead / numChannels`) and its "right" channel was the next
left-channel frame.

### Fix

`src/core/SampleDecoder.cpp` (`decodeSampleOggVorbis()`): the frames are now
built from the channels themselves - `output[0][i]` is the left frame and
`output[1][i]` the right one (the right one repeats the left one for a mono
file, as the other decoders do). The frame count is the number of decoded
samples, not a half of it.

### Regression test

`tests/src/core/SampleDecoderOggChannelTest.cpp` (fork-NEW): a stereo Ogg
Vorbis file with two different constant levels (left `0.5`, right `-0.25`) is
decoded with the Ogg decoder alone (`SampleDecoder::decodeOggVorbisOnlyForTesting()`,
an LMMS_TESTING-only seam) and

- `stereoOggKeepsBothChannels()`: the frame count is the file's length (not half
  of it) and every sampled frame carries the left level in the left channel and
  the right level in the right channel;
- `stereoOggMatchesTheSndfileDecoder()`: the same file through libsndfile and
  through the Ogg decoder yields the same number of frames and the same
  samples.

### RED/GREEN proof

| Stage | Exact command (from the worktree) | Exit | Result |
|---|---|---:|---|
| RED (original code) | `cmake --build build --target zene && cmake --build build --target SampleDecoderOggChannelTest && cd build/tests && ctest -R '^SampleDecoderOggChannelTest$' --output-on-failure` | 8 | `red-SampleDecoderOggChannelTest.log`: `FAIL! ... 'decoded->data.size() >= static_cast<std::size_t>(Rate) - 1024' returned FALSE` and `FAIL! ... both decoders return the same number of frames`; `Totals: 2 passed, 2 failed` |
| GREEN (fix applied) | same commands | 0 | `green-SampleDecoderOggChannelTest.log`: `100% tests passed out of 1` |
| Reversal | `git diff -- src/core/SampleDecoder.cpp include/SampleDecoder.h > logs/fix-269.patch && git apply -R logs/fix-269.patch` | 0 | patch removed, rebuilt |
| Reversal RED | rebuild + `ctest -R '^SampleDecoderOggChannelTest$' --output-on-failure` | 8 | `test-red-269-reversed.log`: the same two `FAIL!` lines (`Totals: 2 passed, 2 failed`) |
| Restore | `git apply logs/fix-269.patch` | 0 | patch restored, rebuilt |
| Restored GREEN | rebuild + `ctest -R '^SampleDecoderOggChannelTest$' --output-on-failure` | 0 | `test-green-269-reapplied.log`: `100% tests passed out of 1` |

Logs live in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-271.r1/logs`.

## Bug new-p14-271 — Ogg read errors leak decoder state

**Verdict: FIXED.** This is REAL.

### Evidence

`src/core/SampleDecoder.cpp`, `decodeSampleOggVorbis()`: after a successful
`ov_open_callbacks()` the decoder state belongs to libvorbisfile, and every
exit has to release it with `ov_clear()`. Before the fix the error paths
returned without it:

- `if (vorbisInfo == nullptr) { return std::nullopt; }` (no `ov_clear`),
- `if (numSamples < 0) { return std::nullopt; }` (no `ov_clear`),
- `if (samplesRead < 0) { return std::nullopt; }` — the read-error path (no
  `ov_clear`).

Only the success path called `ov_clear(&vorbisFile)`, so a file that failed
while decoding leaked libvorbisfile's decoder state on every attempt.

### Fix

`src/core/SampleDecoder.cpp`: a small RAII wrapper, `OggVorbisFileGuard`
(`src/core/SampleDecoder.cpp:117-131`), releases the state in its destructor.
Both Ogg decoders take the guard right after a successful
`ov_open_callbacks()` and no longer call `ov_clear()` by hand, so every exit
after a successful open releases the state - the error paths included. The
sibling decoder `boundedOggVorbis()` (which released the state by hand on
every path) uses the same guard, so the two can no longer drift apart.

### Regression test

`tests/src/core/SampleDecoderOggStateTest.cpp` (fork-NEW): the decoder's
file-close callback is what libvorbisfile calls when the decoder state is
released (`ov_clear`), so the callback count is the number of states that
were released. The test decodes a valid Ogg (the control: exactly one
release), then makes the next read report an error
(`SampleDecoder::failNextOggReadForTesting()`, an LMMS_TESTING-only seam that
returns the same negative value libvorbisfile returns for a read error) and
pins that the state is released on that path too.

### RED/GREEN proof

| Stage | Exact command (from the worktree) | Exit | Result |
|---|---|---:|---|
| RED (original code) | `cmake --build build --target zene && cmake --build build --target SampleDecoderOggStateTest && cd build/tests && ctest -R '^SampleDecoderOggStateTest$' --output-on-failure` | 8 | `red-SampleDecoderOggStateTest.log`: `FAIL! : readErrorPathReleasesTheDecoderState() Compared values are not the same` (`Actual: 0`, `Expected: 1`); `Totals: 2 passed, 1 failed` |
| GREEN (fix applied) | same commands | 0 | `green-SampleDecoderOggStateTest.log`: `100% tests passed out of 1` |
| Reversal | `git diff -- src/core/SampleDecoder.cpp include/SampleDecoder.h > logs/fix-271.patch && git apply -R logs/fix-271.patch` | 0 | patch removed, rebuilt |
| Reversal RED | rebuild + `ctest -R '^SampleDecoderOggStateTest$' --output-on-failure` | 8 | `test-red-271-reversed.log`: `FAIL! : readErrorPathReleasesTheDecoderState() Compared values are not the same` (`Totals: 2 passed, 1 failed`) |
| Restore | `git apply logs/fix-271.patch` | 0 | patch restored, rebuilt |
| Restored GREEN | rebuild + `ctest -R '^SampleDecoderOggStateTest$' --output-on-failure` | 0 | `test-green-271-reapplied.log`: `100% tests passed out of 1` |

## Bug new-p14-276 — Infinite decode limits reach an out-of-range integer conversion

**Verdict: FIXED.** This is REAL.

### Evidence

`src/core/SampleDecoder.cpp`, `SampleDecoder::decodeLimited()` rejected only
the nonpositive limits:

```cpp
if (!(maxSeconds > 0.0)) { return std::nullopt; }
```

`+infinity` passed that check, and the frame count was then computed as
`static_cast<std::int64_t>(std::ceil(maxSeconds * sfInfo.samplerate))` in
`boundedSF()` (and in the same shape in `boundedOggVorbis()` and in the
DrumSynth path of `decodeLimited()`). The conversion of a floating-point value
that is out of range is undefined behaviour; on this platform it produced a
frame count of `INT64_MIN`, and the allocation that followed threw
`std::length_error` ("cannot create std::vector larger than max_size()") - the
measured RED output of the regression below.

### Fix

`src/core/SampleDecoder.cpp`:

- `decodeLimited()` refuses a limit that is not finite (`std::isfinite`), so
  an infinite window is refused before anything is decoded.
- The frame count is computed in one place, `boundedFrameCount()`
  (`src/core/SampleDecoder.cpp:243-250`), which checks the window against the
  supported maximum (`MaxBoundedFrames`, 2^32 frames - about 27 hours at
  44.1 kHz) before the conversion and before the allocation. A window beyond
  it is refused (`std::nullopt`) instead of being converted out of range.
- All three places that turned a window into a frame count use it:
  `boundedSF()`, `boundedOggVorbis()` and the DrumSynth path of
  `decodeLimited()`.

### Regression test

`tests/src/core/SampleDecoderLimitTest.cpp` (fork-NEW):

- `ordinaryLimitStillDecodesTheWindow()` (the control): an ordinary window
  still decodes the file's leading window and reports its full length;
- `limitBeyondTheSupportedMaximumIsRefused()`: a window of 1e12 seconds is
  refused instead of being converted out of range;
- `infiniteLimitIsRefused()`: `+infinity` is refused, and the test fails if
  the conversion throws instead of refusing.

### RED/GREEN proof

| Stage | Exact command (from the worktree) | Exit | Result |
|---|---|---:|---|
| RED (original code) | `git diff -- src/core/SampleDecoder.cpp include/SampleDecoder.h > logs/fix-276.patch && git apply -R logs/fix-276.patch`, then `cmake --build build --target zene && cmake --build build --target SampleDecoderLimitTest && cd build/tests && ctest -R '^SampleDecoderLimitTest$' --output-on-failure` | 8 | `test-red-276.log`: `FAIL! : limitBeyondTheSupportedMaximumIsRefused() '!bounded.has_value()' returned FALSE` and `FAIL! : infiniteLimitIsRefused() the infinite limit was converted out of range: cannot create std::vector larger than max_size()`; `Totals: 3 passed, 2 failed` |
| GREEN (fix applied) | `git apply logs/fix-276.patch`, rebuild, `ctest -R '^SampleDecoderLimitTest$' --output-on-failure` | 0 | `test-green-276.log`: `100% tests passed out of 1` |

### Affected family

`ctest -R '^(SampleDecoderOggChannelTest|SampleDecoderOggStateTest|SampleDecoderLimitTest|BrowserFeaturesWindowTest|ImportDetectionTest)$' --output-on-failure`
from `build/tests`: **5 tests, 5 passed** (`test-family-final.log`, exit 0).
`BrowserFeaturesWindowTest` is the pre-existing test of this file's bounded
decode path and `ImportDetectionTest` is the pre-existing test of its
refusal path; both still pass.

## Self-check

- **Does the regression test FAIL with the product change reverted and PASS with
  it?** Yes, for all three bugs, with the exit codes recorded above:
  - new-p14-269: `ctest -R '^SampleDecoderOggChannelTest$'` -> exit **8**
    (reverted, `test-red-269-reversed.log`) / exit **0** (re-applied,
    `test-green-269-reapplied.log`).
  - new-p14-271: `ctest -R '^SampleDecoderOggStateTest$'` -> exit **8**
    (reverted, `test-red-271-reversed.log`) / exit **0** (re-applied,
    `test-green-271-reapplied.log`).
  - new-p14-276: `ctest -R '^SampleDecoderLimitTest$'` -> exit **8**
    (reverted, `test-red-276.log`) / exit **0** (re-applied,
    `test-green-276.log`).
- **Is the test deterministic (no wall-clock timing, sleeps or races)?** Yes.
  No test in this batch sleeps, times or races. The values are injected:
  - the Ogg read error is injected by the LMMS_TESTING-only seam
    `SampleDecoder::failNextOggReadForTesting()`, which returns the same
    negative value libvorbisfile returns for a read error
    (`src/core/SampleDecoder.cpp:110-116`);
  - the decoder-state release is observed through the decoder's own file-close
    callback, the callback libvorbisfile runs exactly once per released state
    (`src/core/SampleDecoder.cpp:73-84`) - a counter, not a timing;
  - the stereo fixture is written with libsndfile at a fixed rate and level,
    and the limits are the literal values `+infinity` and `1.0e12`.
- **Is the fix at the cause and complete (every caller and sibling checked)?**
  Yes:
  - new-p14-269: the planar layout of `ov_read_float()` is honoured in
    `decodeSampleOggVorbis()`; the sibling `boundedOggVorbis()` already built
    its frames from `output[0][i]` / `output[1][i]`, and the two now agree.
    The libsndfile decoder (`decodeSampleSF()`) keeps its own interleaved
    handling, which is correct for libsndfile's interleaved output.
  - new-p14-271: the guard is taken by both Ogg decoders
    (`decodeSampleOggVorbis()`, `boundedOggVorbis()`), so every exit after a
    successful `ov_open_callbacks()` releases the state; the callers
    (`SampleDecoder::decode()`, `SampleDecoder::decodeLimited()`) are
    unchanged.
  - new-p14-276: all three places that turn a window into a frame count use
    the one checked conversion (`boundedSF()`, `boundedOggVorbis()`, the
    DrumSynth path of `decodeLimited()`), and `decodeLimited()` refuses a
    non-finite window up front.
- **No collateral damage?** The only caller of `decodeLimited()` in the
  product is `BrowserFeatures::analyse()` (`src/core/BrowserFeatures.cpp:123`),
  which passes `BrowserFeatures::FeatureSeconds` (20 s, `include/BrowserIndex.h:49`)
  - 882000 frames at 44.1 kHz, far below the new maximum of 2^32 frames, and
  finite. `BrowserFeaturesWindowTest` (the pre-existing test of that path)
  passes unchanged. The new checks only refuse a limit that is not finite or
  whose frame count does not fit; a limit that could not be honoured before
  either (it threw, or it decoded a window nobody asked for) is refused now.
  The Ogg changes touch no saved data, no project file and no control command:
  the decoder's output is only the imported sample, and a stereo Ogg import now
  yields the frames it always should have yielded. Nothing new runs on the
  audio thread (the decoder is called from the import path, not from a render
  callback), and no command was added.
- **ALREADY-FIXED / FALSE-POSITIVE?** Not applicable: all three findings are
  REAL and fixed. The evidence for each is the code quoted above and the RED
  run that reproduces the defect on the original code.
