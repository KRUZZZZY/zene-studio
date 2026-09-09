# Zene Studio

**A free, open-source complete digital audio workstation.**

Zene Studio is a community-built DAW for composing, arranging, mixing, and
recording music. It is derived from [LMMS](https://github.com/LMMS/lmms) and
extends it with a modern audio engine, native plugin hosting, and AI-assisted
DSP — while keeping project files portable and the whole stack open source.

## What is Zene Studio

Zene Studio is an LMMS-derived digital audio workstation that combines:

- **A modern multi-channel engine** — dynamic routing, sidechain sends, and
  parallel buses on an unbounded mixer.
- **Native VST3 + CLAP hosting** — run modern instrument and effect plugins
  alongside the built-in devices.
- **Patcher node-graph signal routing** — wire instruments, effects, and
  utilities into custom signal chains.
- **Two-track recording** — capture two input channels into separate tracks.
- **Lua + WASM scripting** — automate the DAW with Lua or run sandboxed DSP in
  WebAssembly.
- **AI DSP** — RNNoise noise suppression, NAM neural amp modelling, and offline
  HTDemucs stem separation.
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

The product name is **Zene Studio**. The repository rename from `lmms-complete`
to `zene-studio` is deferred to the end of the naming transition; the plan and
checklist live in [DOCS-NAMING.md](DOCS-NAMING.md).
