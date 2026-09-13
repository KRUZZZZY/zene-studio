# Zene Studio

**A free, open-source, complete digital audio workstation.**

Zene Studio is a community-built DAW for composing, arranging, mixing and recording music. It is
derived from [LMMS](https://github.com/LMMS/lmms), keeps project files portable, and keeps the whole
stack open source. This repository is the **0.2.1-alpha** tree.

This repository ships no screenshot. For what this release is, read
[`docs/RELEASE-NOTES-v0.2.1-alpha.md`](docs/RELEASE-NOTES-v0.2.1-alpha.md); for what it cannot do,
[`docs/KNOWN-LIMITATIONS.md`](docs/KNOWN-LIMITATIONS.md); for a dated, feature-by-feature status of
this exact commit, [`docs/STATUS.md`](docs/STATUS.md).

## Download

**Zene Studio 0.2.1-alpha is the version this text documents** — [get it from the releases
page](https://github.com/KRUZZZZY/zene-studio/releases/tag/v0.2.1-alpha). That page appears when the
release is published; until then the previous release,
[v0.1.0-alpha](https://github.com/KRUZZZZY/zene-studio/releases/tag/v0.1.0-alpha), is the one you can
download (the `v0.2.0-alpha` tag's build failed on every platform, and 0.2.1-alpha supersedes it, so
there is no 0.2.0-alpha download).

The seven release build jobs cover Linux (x86_64 and aarch64, AppImage), macOS (Apple Silicon and
Intel, `.dmg`) and Windows (x64 — two toolchains — and Arm64); a platform has a package only when its
job is green, and packages come from the release page and nowhere else. Each published file's SHA-256
digest is generated from the release itself by the publish step (`docs/RELEASING.md`), never typed
into a file.

The builds are **unsigned**, so Windows SmartScreen and macOS Gatekeeper warn about an unknown
developer. Read [`docs/KNOWN-LIMITATIONS.md`](docs/KNOWN-LIMITATIONS.md) before you install or run
anything: this alpha is unfinished, and that page lists what it is missing.

## What is in this alpha

This is an **early alpha** — expect crashes and rough edges, and treat the known-limitations page as
required reading.

**The DAW core inherited from LMMS**: the Piano Roll, Song Editor and Beat/Bassline editor; a mixer
with a multi-channel port model, sidechain sends and no fixed channel limit; 15+ built-in
synthesizers; SoundFont2; VST2 (Vestige); LADSPA; LV2; GUS patches; full MIDI import/export; Lua 5.4
scripting; slide notes; HiDPI scaling; and self-contained, XML-based `.mmp`/`.mmpz` project files that
travel with your version control (the format is **not yet stable** — a file saved by this build may not
open in a later or older build, so keep backups).

**The agent control surface** — launch with `--control-socket <path>` and another program, not a
person, can drive the open session over a local UNIX socket. It is opt-in and off by default: an
instance not started that way has no socket at all. There are 19 command groups holding 74 commands —
`app`, `arrangement`, `audio`, `automation`, `clip`, `control`, `dsp`, `midi`, `mixer`, `note`,
`plugin`, `project`, `render`, `roll`, `script`, `settings`, `telemetry`, `track`, `transport` — each
command carrying a JSON schema and a declared reversibility class (the A16 contract), with
`control.undo` reaching the same undo history as the GUI's Ctrl+Z. Three commands are registered
with full schemas but **refuse every call** rather than fake a write: `automation.mode_set`,
`mixer.set_pan` and `track.set_arm`.

**VST3 hosting — effects and instruments.** VST3 effects run, and a VST3 **instrument** can be
loaded on a track: MIDI in, audio out, and its state saved in the project. Instrument hosting is new
and narrow: one instrument per track; the plugin's **own editor does not open** (its parameters
appear as the host's generated knob grid); there is no multi-out, no preset management and no
instrument latency compensation; and it is proven only against the purpose-built MIT test instrument
that ships in the source tree — no third-party VST3 instrument has been tested by us. Its own
in-tree regression suites sit behind the `WANT_VST3_TEST_INSTRUMENT` option (default OFF), so CI does
not build or run them.

**CLAP hosting — effects only, and on Linux and macOS only.** `plugins/ClapEffect` is an effect host
and there is no CLAP instrument hosting. The three Windows build jobs compile it out
(`-DWANT_CLAP=OFF`) because the loader needs `dlopen`; the release manifest records the claim per
platform.

**AI DSP** — RNNoise noise suppression and NAM neural amp modelling are in the release.

**Crash reporter** — offline and local: a crash writes a small report you can attach to a bug report.
No network, no telemetry, and sending anything is a separate, opt-in choice.

**Autosave recovery** — recovery is gated on the file belonging to *this* project and being newer
than it, so a stale recovery file no longer strands you at a prompt before autosave has started.

**Also in this alpha**, each with its own limits (the dated detail is in
[`docs/STATUS.md`](docs/STATUS.md)):

- a plugin scan cache with a quarantine list — a JSON data layer, **not a GUI** (quarantining a
  plugin means hand-writing an entry in the cache file);
- an offline BS.1770-4 / EBU R128 loudness meter and a `.loudness.txt` report written on render and
  export — this is a **render-path report, not a live meter**;
- **stem export** from the command line (`zene exportstems`), rendering a project's tracks as
  separate files that line up and sum back to the mix — **CLI/headless only, no dialog control**;
- MIDI learn (Edit ▸ MIDI Learn), with the binding saved in the project;
- a warp engine: markers pinned to the audio that let a clip follow or lead the project tempo — the
  stretch is resampling, so it **changes pitch**;
- seeded note probability and velocity jitter, saved per note, plus a note search-and-transform API —
  **no UI or command reaches it yet**;
- per-note MPE expression — **pitch is applied on playback; pressure and timbre are captured and
  stored but not applied**, and there is no expression editor;
- racks (parallel chains and a chain selector on a channel) and VCA / mix-and-edit groups, both saved
  in the project — **you cannot create one from the interface**, only load a project that already
  contains one;
- auto-mastering wave 1 on the command line (`zene master`): one render, several measured candidates
  against a named loudness target — it **generates and measures; it does not pick a "best"**;
- a reproducible export render (exports render on one thread; live playback still uses the pool) — 7
  of the 9 projects the determinism sweep covers are bit-reproducible, and the two that are not are
  named, with the cause in their own instruments;
- mixer concurrency fixes carried from an external audit, save/load integrity fixes, a fix for the
  exit-time teardown abort, and four test sources recovered into the suite;
- a two-track recording **prototype** (`MultiTrackRecorder`, `RecordRingBuffer`) — capture exists,
  while `Song::record()` is still a stub, so treat it as a prototype rather than a shipped feature;
- the versioned Lua API with a compatibility policy and console, and `tools/mmpz-git/` (developer
  tooling, not part of the binary).

## Deliberately off or absent

Not in this build, and not claimed by it:

- **Session View / clip launcher** — the data layer and launch scheduler are in the source but
  compiled out (`WANT_SESSION_VIEW` defaults OFF and no release job passes it); even a flag-ON build
  has no clip launcher and no grid.
- **Offline HTDemucs stem separation** — opt-in at configure time and off in every release build
  (`WANT_STEM_SPLIT` defaults OFF). This is not the command-line stem *export* above.
- **WASM DSP sandbox** — `WANT_WASM` defaults ON, but the build falls back to OFF when the wasmtime C
  API is absent, and CI provisions none, so it is compiled out in practice.
- **Automation modes are not usable** — the Read/Touch/Latch/Write engine is implemented and tested,
  but nothing can select or persist a mode, and `automation.mode_set` refuses every call.
- **No CLAP instrument hosting, no plugin editor, no patcher GUI, no live LUFS meter.**
- **No fades, crossfades or clip gain; no trim or slip gesture for a clip's source window** (clip
  *length* resize does work); no take lanes, comping or punch in/out; no freeze or bounce-in-place;
  no sample-accurate automation (values land on a tick boundary); no controller surfaces; no groove
  pool; no Ableton Link.

## Build status

`build.yml` compiles the tree on seven jobs: Linux x86_64 and aarch64, macOS Intel and Apple Silicon,
and Windows (msvc-x64, mingw64, arm64). `quality-gates` runs its **static** gates (3, 4, 6, 7, 8 and
9, with the tools-scope steps beside them) on every push and pull request; its two build-backed jobs
(unit tests, and coverage) stay `workflow_dispatch`-only, so a green `quality-gates` check covers the
static gates only — and proves nothing about whether the tree compiles. `build.yml` is what compiles
the tree.

This file does not record the live state of any CI run. [`docs/STATUS.md`](docs/STATUS.md) carries the
dated status of record for this commit, names what could not be determined from a local worktree, and
points at [`docs/RELEASE-NOTES-v0.2.1-alpha.md`](docs/RELEASE-NOTES-v0.2.1-alpha.md) and
[`docs/KNOWN-LIMITATIONS.md`](docs/KNOWN-LIMITATIONS.md).

## Building

Configure, build and test from the repository root (the executable is `build/zene`):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWANT_QT6=ON
cmake --build build -j4
```

Run the tests **from `build/tests`**, not from the top level:

```sh
cd build/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

The top-level build directory has no `CTestTestfile.cmake`, so `ctest` run there reports **0 tests**.
Treat 0 tests as an error, never as a pass.

Optional feature flags, both **off in the published alpha builds**:

- `-DWANT_STEM_SPLIT=ON` — offline HTDemucs stem separation via ONNX Runtime (off by default).
- `-DWANT_WASM=ON` — the WASM DSP sandbox (on by default; it needs the wasmtime C API and is switched
  off automatically when that is not found).

For example, a build with both requested:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWANT_QT6=ON \
      -DWANT_STEM_SPLIT=ON -DWANT_WASM=ON
```

## Contributing

Build, test and gate instructions are in [CONTRIBUTING.md](CONTRIBUTING.md); local CI is documented in
[docs/LOCAL-CI.md](docs/LOCAL-CI.md), and the gate definitions and their known open defects are in
[tests/QA-GATES.md](tests/QA-GATES.md). Security problems go through [SECURITY.md](SECURITY.md), not
the public issue tracker. Release publishing is documented in [docs/RELEASING.md](docs/RELEASING.md).

## License and attribution

Zene Studio is licensed under the **GNU General Public License, version 2 or later**
(GPL-2.0-or-later). See [LICENSE.txt](LICENSE.txt).

Zene Studio is built on LMMS (https://github.com/LMMS/lmms), licensed GNU GPL v2. All upstream notices
retained. Zene Studio is not affiliated with or endorsed by the LMMS project.

## Naming

The product name is **Zene Studio**. The repository was renamed to `zene-studio` on 2026-09-09, and
the deferred code and branding items (wave R of [DOCS-NAMING.md](DOCS-NAMING.md)) landed on top of the
v0.1.0-alpha release: the CMake project is `zene`, the built executable is `zene`, the desktop entry,
the window title and `--version` all say Zene Studio, and the release packages are named `zene-*`.

Two things are kept from LMMS on purpose: the `lmms::` C++ namespace and the `.mmp`/`.mmpz` project
format, plus the licence headers; the plugin entry symbol `lmms_plugin_main` is also unchanged, which
is why existing native plugins still load. The config file is now `~/.zenestudio.xml` and the working
folder `~/Documents/Zene Studio/`, and **both are adopted rather than re-pointed**: on first run the
pre-rename file and folder are moved into the new names, and if that is not possible the old location
keeps being used, so nothing is orphaned for an existing install. The full list of deliberate
residuals is in [docs/WAVE-R-RENAME.md](docs/WAVE-R-RENAME.md) and
[docs/RENAME-COMPLETE.md](docs/RENAME-COMPLETE.md).
