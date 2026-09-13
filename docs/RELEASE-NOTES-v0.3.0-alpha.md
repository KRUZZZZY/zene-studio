# Zene Studio 0.3.0-alpha — release notes (in progress)

Zene Studio 0.3.0-alpha — release notes (in progress)

**Status: DRAFT, not a release record.** There is no `v0.3.0-alpha` tag yet, and this file is opened by the
warp-marker lane rather than by the release lane: the 0.3.0 scope contract
(`NEXT-0.3.0-AGENT-PROMPT.md` §3.1, `V0.3-ALPHA-PLAN.md` §1) requires every feature that ships in 0.3.0 to
write its **UI absence** down in one line in the release notes *and* in `docs/KNOWN-LIMITATIONS.md`. The
user-first rewrite of this file, the version bump and the rest of the notes are wave **W12**'s deliverable;
this draft carries the entries the earlier lanes are obliged to add as they land.

Released before this one: **v0.1.0-alpha** (`docs/RELEASE-NOTES-v0.1.0-alpha.md`) and **v0.2.1-alpha**
(`docs/RELEASE-NOTES-v0.2.1-alpha.md`, the patch line that made the seven-platform matrix build and the
release claims true).

## The shape of 0.3.0

0.3.0 is an **engine-and-agent release, not a UI release**. Everything that can exist in the backend and be
driven through the control surface is folded in; the interface stays deliberately minimal; and the taste work
(stock devices, factory content, the design system) plus every UX/UI item is deferred. Stated as one line:

> **Everything is operable through `--control-socket` and the MCP bridge; almost nothing is operable from the
> interface.**

## Warp marker editing (`warp.*`) — added 2026-09-13

- **New: the warp map has an editing surface.** `warp.list`, `warp.add`, `warp.move`, `warp.remove` and
  `warp.set` are registered control-surface commands, so an agent can pin a frame of a sample clip's audio to a
  timeline position, move and delete markers, replace the whole marker list, and choose the clip's warp tempo
  mode (`follow` the project, or `source` — lead with the tempo the clip was recorded at) and its declared
  source tempo in BPM. Each mutating call records its SPEC A16 reversibility class (`true_inverse`), how it is
  reversed (the clip's own ProjectJournal checkpoint) and its before-state, so one `control.undo` takes the
  edit back.
- **The engine half already shipped in 0.2.1.** The markers, the monotonic map, the `<warp>` element and the
  measured render are #597's and are unchanged by this addition; see `docs/WARP.md` for the implementing lane's
  report and its numbers. Nothing about the stretching was touched here.
- **UI absence — one line: warp marker editing is drivable through the socket, not from the interface.**
  There is no marker handle, no drag, no grid snap and no groove template; nothing in the interface can place
  the first marker. `docs/KNOWN-LIMITATIONS.md` carries the same sentence.
- **Still true, and still not fixed:** the stretch is resampling, so a warp changes pitch (2× is an octave
  up); there is no pitch-preserving time-stretch.

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
## Not in this draft yet

The Session View, racks, comping, MPE modulation, Link sync, browser search and the engine-gap items of the
0.3.0 scope, plus the release-bar statements, are the responsibility of their own lanes and wave W12. This
file grows as those land; it is not a summary of 0.3.0 and must not be read as one.

Zene Studio 0.3.0-alpha

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
