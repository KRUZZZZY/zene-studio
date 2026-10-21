# Cluster new-p14-271: the SampleDecoder's Ogg and bounded-decode defects

new-p14-269: FIXED

Verdict: **FIXED** for new-p14-269 (the other two bugs of this batch are
reported in the sections below, each with its own decision)

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
