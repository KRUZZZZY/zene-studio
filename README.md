# Zene Studio

**A free, open-source complete digital audio workstation.**

Zene Studio is a community-built DAW for composing, arranging, mixing, and
recording music. It is derived from [LMMS](https://github.com/LMMS/lmms) and
extends it with a modern audio engine, native plugin hosting, and AI-assisted
DSP — while keeping project files portable and the whole stack open source.

## Download

**Zene Studio 0.1.0-alpha is the first public alpha** — [get it from the releases
page](https://github.com/KRUZZZZY/zene-studio/releases/tag/v0.1.0-alpha). Packages
exist for Linux (x86_64 and aarch64, AppImage), macOS (Apple Silicon and Intel,
`.dmg`) and Windows (x64 — two installers — and Windows on Arm), and only platforms
whose build job is green have a package.

They are **unsigned**, so Windows SmartScreen and macOS Gatekeeper warn about an
unknown developer; the release notes carry the one-time steps, and each file's
SHA-256 digest is shown on the release page so you can check your download before
running it. Read [`docs/KNOWN-LIMITATIONS.md`](docs/KNOWN-LIMITATIONS.md) before you
install: this alpha cannot host instrument plugins, cannot edit clips or take lanes,
and hosts third-party plugins as **effects only**.

## What is Zene Studio

Zene Studio is an LMMS-derived digital audio workstation that combines:

- **A modern multi-channel engine** — dynamic routing, sidechain sends, and
  parallel buses on an unbounded mixer.
- **Native VST3 + CLAP effect hosting** — run modern VST3 and CLAP effects
  alongside the built-in devices. Instrument hosting is **not** implemented yet:
  the hosts are `Vst3Effect` and `ClapEffect`, effects only.
- **Two-track recording** (prototype) — capture two input channels into separate
  tracks. The capture path is hardware-verified; the surrounding workflow is not
  finished, so treat this as a prototype rather than a shipped feature.
- **Lua + WASM scripting** — automate the DAW with Lua or run sandboxed DSP in
  WebAssembly. The WASM sandbox needs the wasmtime C API and is **off in the
  published alpha builds** (`WANT_WASM=OFF`); Lua 5.4 is in.
- **AI DSP** — RNNoise noise suppression, NAM neural amp modelling, and offline
  HTDemucs stem separation — the stem separator is opt-in at configure time and
  **off in the published alpha builds** (`WANT_STEM_SPLIT=OFF`).
- **Slide notes** — pitch-slide and glide editing in the Piano Roll.
- **HiDPI scaling** — crisp, scalable UI on high-resolution displays.
- **Git-friendly `.mmpz` project files** — self-contained, XML-based project
  files that can travel with your version control.

Everything inherited from LMMS is still here: the Piano Roll, Beat/Bassline
editor, Song Editor, a flexible mixer, 15+ built-in synthesizers, SoundFont2,
VST2 (Vestige), LADSPA, LV2, GUS patches, and full MIDI import/export.

## Zene Studio roadmap

Eight waves are planned on top of the current tree. They are **in development**
and **not available in the current build** — treat the list below as direction,
not as shipped features:

1. **Session View / clip launcher** — clip-based, non-linear performance
   workflow.
2. **Warp engine** — time-stretching and pitch-shifting for audio.
3. **Racks + Chain Selector + macros** — instrument and effect racks with macro
   controls.
4. **Comping / take lanes** — multi-take recording and comping.
5. **MPE** — MIDI Polyphonic Expression support.
6. **Modulation as a layer** — first-class modulation across devices.
7. **Ableton Link / Link Audio** — tempo and transport sync with other apps.
8. **Tag + sound-similarity browser** — tag-based browsing and similarity
   search for samples and presets.

Separately from the waves above, the **Patcher node-graph engine**
(`RoutingGraph` plus a small set of built-in node types) is in the tree and
unit-tested, but no GUI or audio-path code instantiates it yet — node-graph
patching is **not available in the current build**.

## Build status

`main` builds on CI. All seven build jobs (`linux-x86_64`, `linux-arm64`,
`macos-x86_64`, `macos-arm64`, `msvc-x64`, `mingw64`, `windows-arm64`) are green on
`0c23587d2` (run `34634112663`) and again on the `v0.1.0-alpha` tag (run
`34639862178`), and `checks` and `doxygen` pass. Read the live state from
`gh run list --repo KRUZZZZY/zene-studio` rather than from this file.

`quality-gates` runs its static gates (3, 4, 6, 7 and 8) on every push and pull
request; the two build-backed jobs (unit tests + mutation, coverage) stay
dispatch-only, so a green check here covers the static gates only — and it proves
nothing about whether the tree compiles. `build.yml` is what compiles the tree. See
[`docs/STATUS.md`](docs/STATUS.md) for the current, dated status of every feature
above.

## Building

Configure, build, and test from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWANT_QT6=ON
cmake --build build -j4
```

Run the tests **from `build/tests`**, not from the top level:

```sh
cd build/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

The top-level build directory has no `CTestTestfile.cmake`, so `ctest` run
there reports **0 tests**. Treat 0 tests as an error, never as a pass.

Optional feature flags:

- `-DWANT_STEM_SPLIT=ON` — offline HTDemucs stem separation via ONNX Runtime
  (off by default).
- `-DWANT_WASM=ON` — the WASM DSP sandbox (on by default; it needs the wasmtime
  C API and is switched off automatically when that is not found).

For example, a build with everything enabled:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWANT_QT6=ON \
      -DWANT_STEM_SPLIT=ON -DWANT_WASM=ON
```

## License and attribution

Zene Studio is licensed under the **GNU General Public License, version 2 or
later** (GPL-2.0-or-later). See [LICENSE.txt](LICENSE.txt).

Zene Studio is built on LMMS (https://github.com/LMMS/lmms), licensed GNU GPL v2. All upstream notices retained. Zene Studio is not affiliated with or endorsed by the LMMS project.

## Naming

The product name is **Zene Studio**. The repository was renamed to
`zene-studio` on 2026-09-09; the remaining code/branding items (wave R) and the
checklist live in [DOCS-NAMING.md](DOCS-NAMING.md).
