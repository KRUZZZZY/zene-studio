# Export dither and the sample-rate-conversion quality

The 0.3.0-alpha engine additions behind the `export.*` control-surface group: a TPDF dither for the
integer export depths, and an explicit choice of sample-rate-conversion converter. Both are **off /
default** in the sense that matters — a render that asks for nothing is byte-for-byte the render the
engine produced before either existed.

## The files

| File | What it is |
|---|---|
| `include/ExportDither.h` + `src/core/ExportDither.cpp` | `lmms::ExportDither` — the TPDF generator and the two in-place appliers (SampleFrames, interleaved floats) |
| `include/SrcQuality.h` | `lmms::SrcQuality` (`Linear`, `SincFastest`, `SincMedium`, `SincBest`) and its wire/CLI names |
| `include/ExportRenderSettings.h` + `src/core/ExportRenderSettings.cpp` | the process-wide selection both ends of a render can read |
| `include/OutputSettings.h` | the two new fields: `dither()`, `srcQuality()` |
| `src/core/audio/AudioFileWave.cpp` | where the dither is applied: immediately before the quantiser |
| `src/core/Sample.cpp` | where the SRC quality reaches the converter: `Sample::play` syncs its resampler's mode |
| `src/core/ProjectRenderer.cpp` | publishes the `OutputSettings` it was handed for the render's duration, restores after |
| `src/core/ControlCommandsExport.cpp` | the `export.*` command group (schemas + A16 records) |
| `tests/src/core/ExportDitherTest.cpp` | the dither proof (statistics + byte-level default) |
| `tests/src/core/AudioResamplerRatioTest.cpp` | the ratio convention + the quality reaching the resampler |

## Why the two settings live outside `OutputSettings`

`OutputSettings` is a *value*, built per render (`main.cpp` for the CLI, the export dialog for the GUI) and
gone when the render is. The two ends that must agree are far apart: the quantiser
(`AudioFileWave::writeBuffer`) does receive the `OutputSettings`, but the resampler that must obey the SRC
quality is built deep inside `Sample::play`, which never sees one. `ExportRenderSettings` is the one place
both ends can read. Its defaults are the pre-existing behaviour (`dither = false`, `srcQuality = Linear`),
`OutputSettings`' two fields default from it so an agent's `export.set_*` choice reaches the next render,
and `ProjectRenderer` publishes what it was handed for the render's duration and restores the previous
values in its destructor whichever route the render leaves by.

## The dither

**What it does.** Quantising a float to N bits replaces each sample with the nearest N-bit level. The error
is a deterministic function of the signal, so at low levels it is correlated distortion rather than noise.
The dither adds a small random offset *before* the quantiser; the error then becomes signal-independent
noise at a known level.

**Why TPDF.** The offset is the sum of two independent uniforms on [-0.5, +0.5) LSB — triangular on
[-1, +1) LSB, variance 1/6 LSB². That is the distribution for which the *total* error (dither plus
rounding) has variance LSB²/4 with zero mean independent of the signal. A single uniform (RPDF, variance
1/12 LSB²) leaves the error's first moment signal-dependent; `ExportDitherTest` asserts 1/6 LSB² and, as a
discriminator, that it is more than 1.5× the RPDF value.

**Determinism.** The generator is a fixed splitmix64 seeded from a constant (`ExportDither::DefaultSeed`),
not `<random>`: `std::uniform_real_distribution` is not specified to produce the same sequence on two
implementations, and this project's claim is byte-identical renders. `ExportDitherTest` asserts both
directions — same seed, same bytes; different seed, different bytes.

**Scope, and the honest edges.**

- Applied to the **WAV** path at the depth actually written: 16-bit (the integer path) and 24-bit (the
  float path, which libsndfile then quantises). FLAC, OGG and MP3 do not take it yet.
- **32-bit float is never dithered** — it has no fixed quantisation step, so `lsbForBitDepth(32)` is 0 and
  the operations return without touching a sample. Asserted.
- The engine's float→int conversion *truncates* rather than rounds (`AudioDevice::convertToS16`), so the
  dither is drawn against the same number that conversion uses (`OUTPUT_SAMPLE_MULTIPLIER` = 32767). The
  test asserts the step matches; the composite total error therefore stays within one LSB, which the test
  also asserts.

## The SRC quality

`SrcQuality` selects the libsamplerate converter: `Linear` is `AudioResampler::Mode::Linear` /
`SRC_LINEAR`, the converter this path has always used and therefore the default; the three sinc
converters are progressively better and more expensive. `Sample::play` compares its resampler's current
mode with the render's selection and re-creates the converter only when they differ — false for the
default, so the default path pays one atomic load and a comparison and changes nothing. Because the
quality is set once per render (by `ProjectRenderer`), the converter's filter history is never dropped
mid-render.

`AudioResamplerRatioTest` proves the quality *reaches the converter* rather than being stored: `Linear` and
`SincBest` must produce different samples for the same input, both directly through `AudioResampler` and
end to end through `Sample::play`.

## The ratio convention — corrected in the prose, not the code

`docs/WARP.md` §3.1, `src/core/SamplePlayHandle.cpp` and a comment in `SampleClipWarpTest.cpp` all reported
that libsamplerate reads `SRC_DATA::src_ratio` as input/output while the engine documents it as
output/input — an "inversion" — and §3.1 concluded from that a live defect on the pre-existing
mismatch-rate path (a 48 kHz source in a 44.1 kHz project playing ~8.8 % fast and sharp).

**The premise is false.** Measured on the library this build links:

```
src_ratio = 2.00  ->  input_used=4096  output_gen=8192   (out/in = 2.0000)
src_ratio = 1.00  ->  input_used=4096  output_gen=4096   (out/in = 1.0000)
src_ratio = 0.50  ->  input_used=4096  output_gen=2048   (out/in = 0.5000)
```

`src_ratio` is output frames per input frame — the same convention the engine documents — so
`outputSampleRate / sampleRate` is correct, the mismatch-rate path consumes the pitch-preserving
48000/44100 = 1.0884 source frames per output frame, and inverting `AudioResampler::process()` to "fix"
the phantom inversion is what would have introduced the defect. `AudioResamplerRatioTest` pins all three
numbers and the mismatch-rate figure, so an inversion fails by name instead of silently changing the pitch
of every mismatched-rate project. The three prose sites now state the measured convention;
`docs/WARP.md` §3.1 carries the correction and the original wording, struck through.

## Driving it

```
# over the socket
{"id":"export.get_settings"}
{"id":"export.set_dither","params":{"dither":true}}
{"id":"export.set_src_quality","params":{"src_quality":"sinc_best"}}

# from the CLI
lmms render song.mmpz -o out.wav --dither --src-quality sinc_best
```

`export.set_dither` and `export.set_src_quality` are `true_inverse` in the A16 contract: each records an
action checkpoint that restores the previous value, so `control.undo` reverses them and the step appears on
the engine's own undo stack beside a user's Ctrl+Z. `export.get_settings` writes nothing and records no
transaction.

## Proving it

```
cd <build>/tests
ctest -R 'ExportDitherTest|AudioResamplerRatioTest' --output-on-failure   # both registered by tests/CMakeLists.txt
./ExportDitherTest -v1        # DITHER_EVIDENCE lines: every measured number, checkable by hand
./AudioResamplerRatioTest -v1 # SRC_EVIDENCE lines
```

The dither test's second case is the one that matters and it carries its own inverted control: on a
low-level sine the *undithered* quantisation error correlates with the signal by more than 0.8 (the
assertion would fail if the fixture were not low-level enough to be a control), and with TPDF dither the
same measurement falls below 0.05. `ExportDitherTest` also asserts the default export byte-for-byte
against the pre-change truncation, so the off-by-default claim is made in bytes and not only in values.
