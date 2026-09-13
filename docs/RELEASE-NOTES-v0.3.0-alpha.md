# Zene Studio 0.3.0-alpha

**An engine-and-agent release.** Everything that can exist in the backend and be driven through the
control surface is folded in; the interface stays deliberately minimal. The release's own promise, in one
sentence: **everything is operable through `--control-socket` and the MCP bridge; almost nothing is
operable from the interface.** Read `docs/KNOWN-LIMITATIONS.md` before you install — where a capability
has no interface, that page says so rather than leaving you to find out.

> **Status of this file.** 0.3.0-alpha is in preparation; this file is being written as the release's
> capabilities land, one section per merged lane, so that the notes and the build agree when the tag is
> cut. Each capability claim below names the engine change, the control-surface command group and the
> test that proves it — the rule this project holds every release to. Anything not yet verifiable is
> marked, not asserted.

## Export: TPDF dither and an explicit sample-rate-conversion quality

- **Dither, off by default.** The WAV export can apply TPDF (triangular-PDF) dither immediately before
  quantisation. TPDF is the distribution that makes the total quantisation error's variance exactly
  LSB²/4 with zero mean *independent of the signal*, so a low-level passage stops being quantised into a
  correlated distortion. It is **off unless you ask for it** — the reproducibility claim above depends on
  byte-identical renders, and an always-on dither would falsify it — and it is **deterministic**, seeded
  from a constant, so a dithered render is reproducible too.
- **An explicit SRC quality.** The sample-rate converter the render uses is now selectable:
  `linear` (the default, and the converter this engine has always used), or libsamplerate's
  `sinc_fastest` / `sinc_medium` / `sinc_best`. The default is the historical converter, so a render that
  asks for nothing is unchanged.
- **Engine:** `include/ExportDither.h` + `src/core/ExportDither.cpp`,
  `include/SrcQuality.h` + `include/ExportRenderSettings.h` + `src/core/ExportRenderSettings.cpp`, the
  `OutputSettings` fields, `AudioFileWave::writeBuffer`, and `Sample::play`.
- **Control surface:** the `export.*` group — `export.get_settings`, `export.set_dither`,
  `export.set_src_quality` — with argument/result schemas and A16 reversibility metadata (both setters are
  `true_inverse`: an action checkpoint restores the previous value, so `control.undo` reverses them).
- **Proof:** `ExportDitherTest` (the TPDF moments, the decorrelation of the quantisation error with an
  inverted control, determinism, and a byte-level off-by-default assertion on real WAVs) and
  `AudioResamplerRatioTest` (the converter ratio convention, and the chosen quality reaching the
  resampler). Both are registered ctests.
- **The converter ratio convention was corrected in the documentation, not in the code.** Three prose
  sites claimed libsamplerate's `src_ratio` was inverted relative to the engine's documented
  output/input convention, and `docs/WARP.md` §3.1 concluded from that a live defect on the
  mismatch-rate path. Measured on the library this build links, `src_ratio` *is* output/input
  (`src_ratio = 2.0` consumes 4096 input frames and generates 8192 output frames), so the engine was
  always right and the "fix" the note implied would have made every 48 kHz source in a 44.1 kHz project
  play ~8.8 % fast and sharp. The convention is now pinned by `AudioResamplerRatioTest`, and
  `docs/WARP.md` §3.1 carries the correction.
- **Interface absence:** drivable through the socket, not from the interface. Neither setting appears on
  the export dialog.

## Provenance

Written during the 0.3.0-alpha programme from the merged lane branches on `release/0.3.0`; the base of
record is `post-alpha/integration` @ `70f2d087c`. This file is extended by each lane's merge, and
re-checked claim by claim against the built artefact before the tag is cut
(`[VERIFY AT FREEZE]` marks a claim that must be re-checked against the built artefact; nothing carrying
that marker is published as-is, and no unverified claim is published without one).
