# Zene Studio

**A free, open-source, complete digital audio workstation.**

Zene Studio is a community-built DAW for composing, arranging, mixing and recording music. It is
derived from [LMMS](https://github.com/LMMS/lmms), keeps project files portable, and keeps the whole
stack open source. This repository is the **0.2.1-alpha** tree.

This repository ships no screenshot. For what this release is, read
[`docs/RELEASE-NOTES-v0.4.0-alpha.md`](docs/RELEASE-NOTES-v0.4.0-alpha.md); for what it cannot do,
[`docs/KNOWN-LIMITATIONS.md`](docs/KNOWN-LIMITATIONS.md); for a dated, feature-by-feature status of
this exact commit, [`docs/STATUS.md`](docs/STATUS.md).

## Download

**Zene Studio 0.4.0-alpha is the version this text documents** — [get it from the releases
page](https://github.com/KRUZZZZY/zene-studio/releases/tag/v0.4.0-alpha). That page appears when the
release is published; until then the previous release,
[v0.3.0-alpha](https://github.com/KRUZZZZY/zene-studio/releases/tag/v0.3.0-alpha), is the one you can
download (its notes are [`docs/RELEASE-NOTES-v0.3.0-alpha.md`](docs/RELEASE-NOTES-v0.3.0-alpha.md)).

The six release build jobs cover seven platform builds — Linux (x86_64 and aarch64, AppImage), macOS
(Apple Silicon and Intel, `.dmg` — one job, a matrix over two architectures) and Windows (x64 — two
toolchains — and Arm64); a platform has a package only when its
job is green, and packages come from the release page and nowhere else. Each published file's SHA-256
digest is generated from the release itself by the publish step (`docs/RELEASING.md`), never typed
into a file.

**macOS support floor: 10.13 (High Sierra).** That is Qt 5.15's own minimum deployment target, and it
is what the Apple build is configured and tested against (`CMakeLists.txt` prints the floor and warns
if `MACOSX_DEPLOYMENT_TARGET` is set below it). The CI job's deployment target is derived from a
dependency binary and today is macOS 11 or later — above the floor and supported — so the floor is a
statement about the oldest OS the project supports, not the version CI happens to build for. Below
10.13 nothing is claimed; the audio worker wake is one reason, because its lock-free path (the
`__ulock` compare-and-wait the platform uses instead of `std::atomic::wait`, unavailable before macOS
11) needs 10.12 or later.

The builds are **unsigned**, so Windows SmartScreen and macOS Gatekeeper warn about an unknown
developer. Read [`docs/KNOWN-LIMITATIONS.md`](docs/KNOWN-LIMITATIONS.md) before you install or run
anything: this alpha is unfinished, and that page lists what it is missing.

## What is in this alpha

This is an **early alpha** — expect crashes and rough edges, and treat the known-limitations page as
required reading. The release's own promise, in one sentence: **everything is operable through
`--control-socket` and the MCP bridge, and the core editing workflows are also operable from the interface; some
advanced or diagnostic operations remain socket-only.**

**The DAW core inherited from LMMS**: the Piano Roll, Song Editor and Beat/Bassline editor; a mixer
with a multi-channel port model, sidechain sends and no fixed channel limit; 15+ built-in
synthesizers; SoundFont2; VST2 (Vestige); LADSPA; LV2; GUS patches — this inherited format list is the
**Linux** build's, and availability varies by platform (see the *plug-in formats vs. tested
compatibility* paragraph below, which keeps loading apart from tested third-party support); full MIDI
import/export; Lua 5.4 scripting; slide notes; HiDPI scaling; and self-contained, XML-based
`.mmp`/`.mmpz` project files that travel with your version control (the format is **not yet stable** —
a file saved by this build may not open in a later or older build, so keep backups).

**The agent control surface** — launch with `--control-socket <path>` and another program, not a
person, can drive the open session over a local UNIX socket. It is opt-in and off by default: an
instance not started that way has no socket at all. The tree's committed command snapshot
(`tools/mcp-zene-control/zene_control/commands_snapshot.json`, captured from a live instance and held
equal to the binary by the `ControlCommandsSnapshot` test) holds **<!-- canary -->853 ids / 76 groups<!-- /canary -->**;
the eight `wasm.*` commands are registered on top of that only where the wasmtime C API is found,
which no release job provisions. Every command carries a JSON schema and a declared reversibility
class (the A16 contract; in the reference configuration 361 rows - 166 `true_inverse` / 39
`snapshot` / 13 `irreversible` / 143 `not_mutating` - as published in
`docs/RELEASE-NOTES-v0.3.0-alpha.md` and measured by `bash tools/dawproject-proof.sh`), with
`control.undo` reaching the same undo history as the GUI's Ctrl+Z. Exactly one command is registered
with a full schema and **refuses every call** rather than fake a write: `mixer.set_pan`, because a
mixer channel in this tree carries no pan property. The two further refusals the 0.2.1 text named are
implemented in 0.3.0 — measured on the built binary, `automation.mode_set` changes a parameter's mode
(`read` → `touch`) and `track.set_arm` arms a route and opens its take file.

**VST3 hosting — effects and instruments.** VST3 effects run, and a VST3 **instrument** can be
loaded on a track: MIDI in, audio out, and its state saved in the project. Instrument hosting is
narrow: one instrument per track. When the plug-in's editor view supports **X11 embedding**
(`X11EmbedWindowID`), its **own editor opens** in a window of its own (the instrument view's "Show
plugin editor" button, or `plugin.editor_open`), next to the host's generated knob grid; a view that
does not offer X11 embedding is refused with a reason, so this editor path is **X11-only**.
**Output buses past the main one are supported**: the instrument's other buses are listed
under Instrument Setup > Output Buses and each can be routed to a mixer channel
(`plugin.output_*`); a bus count is the plug-in's own. The routing is proven against a stand-in
instrument and the VST3 bus-map unit test: the in-tree VST3 fixture has a single output bus, so no
multi-output VST3 plug-in has been run. The instrument **reports its latency** to delay compensation
(a routed bus carries it; `InstrumentLatencyPdcTest` measures the fixture's). Still missing: the plug-in's own preset (program) lists are
not read, and there is no out-of-process hosting. All of this is proven only against the
purpose-built MIT test instruments that ship in the source tree - the editor against a single
offscreen contract test, no editor of a third-party plug-in has been opened on a real display - and **no
third-party VST3 instrument has been tested by us**. Its own in-tree regression suites sit behind the
`WANT_VST3_TEST_INSTRUMENT` option (default OFF); the `linux-x86_64` release job passes it `ON`, so
they run there on every push, and the other six jobs keep the default.

**CLAP hosting.** CLAP effect hosting is in the release on **every platform** — all six build jobs
(seven platform builds) pass `-DWANT_CLAP=ON`, and the loader's Windows half is `LoadLibraryW`/`GetProcAddress` rather
than `dlopen`. A CLAP generator can also be loaded onto an instrument track
(`plugins/ClapInstrument`), drivable through the socket: like the VST3 instrument its own editor opens
in a window of its own (`ClapInstrument::openNativeEditor`, `plugins/ClapInstrument/ClapInstrument.cpp:171`;
X11-embedded editors only), and the notes a user plays reach it through the track's MIDI path.

**Plug-in formats vs. tested compatibility — two different claims.** On **Linux** — one of the
platforms these release builds target — this build **loads** the formats it is built with: VST3
effects and instruments, CLAP effects and instruments, VST2 through Vestige, LV2, LADSPA, SoundFont2
and GUS patches, plus the built-in instruments. **Format availability varies by platform**: each
platform's build enables its own set, so that list is the Linux one — CMake disables VST hosting on
Apple builds, so a macOS build has no VST2/Vestige path. **Loading a format is not the same as having
tested a particular maker's product.** Apart from the MIT fixtures in the source tree and the pinned plug-ins measured in
`docs/PLUGIN-COMPATIBILITY.md` (load, editor and state round-trip, with what failed), **no other third-party
plug-in has been tested by us**, there is **no verified Wine/yabridge path** for plug-ins
built for Windows, and there is **no Kontakt runtime**: a **Kontakt** library needs Native Instruments'
own installer and authorization, which are external to this project, and no Kontakt library or
Kontakt-format runtime has been verified. SoundFont2 is the sampled-instrument format this build
supports and that a user can rely on. Which files the scan found, with their per-file status and
quarantine controls, is in **Project tools ▸ Plug-in Modules...** (see the limitations page).

**AI DSP** — RNNoise noise suppression and NAM neural amp modelling are in the release.

**Crash reporter** — offline and local: a crash writes a small report you can attach to a bug report.
No network, no telemetry, and sending anything is a separate, opt-in choice.

**Autosave recovery** — recovery is gated on the file belonging to *this* project and being newer
than it, so a stale recovery file no longer strands you at a prompt before autosave has started.

**Also in this alpha**, each with its own limits (the dated detail is in
[`docs/STATUS.md`](docs/STATUS.md)):

- a plugin scan cache with a quarantine list — the JSON data layer the socket drives **and a GUI**:
  **Project tools ▸ Plug-in Modules...** lists every file the scan found with its per-file status (and
  its name, or the error it failed with) and offers **Quarantine...** (with a reason), **Release** and
  **Rescan**; **Edit ▸ Plugin Quarantine** adds and releases entries too. The same actions are the
  commands `plugin.scan_cache_list` / `plugin.scan_cache_quarantine_add` /
  `plugin.scan_cache_quarantine_remove` / `plugin.rescan`, so the interface and the agent surface are
  one implementation;
- an offline BS.1770-4 / EBU R128 loudness meter and a `.loudness.txt` report written on render and
  export, plus a live master meter drivable through the socket (`meter.arm`, `meter.get_state`) —
  **no meter appears in the interface**;
- **stem export** from the command line (`zene exportstems`), rendering a project's tracks as
  separate files that line up and sum back to the mix — **CLI/headless only, no dialog control**;
- MIDI learn (Edit ▸ MIDI Learn), with the binding saved in the project;
- a warp engine: markers pinned to the audio that let a clip follow or lead the project tempo, with
  a pitch-preserving `warp.stretch` beside the resampling one — **`warp.*` is socket-only**, and a
  resampling warp **changes pitch** (2× is an octave up);
- seeded note probability and velocity jitter, saved per note, plus a note search-and-transform API —
  **no UI reaches it** (`note.probability_set` is the verb);
- per-note MPE expression — **all three axes are applied on playback**: pitch as a frequency ratio,
  pressure and timbre as channel pressure / CC74 sent on the note's own channel — and there is still
  **no expression editor**;
- racks (parallel chains and a chain selector on a channel) and VCA / mix-and-edit groups, both saved
  in the project. A mixer channel's menu has a **Rack** submenu to add, route and remove parallel chains,
  split the input, wrap a chain, add macros and zones (**Zones...**, **Add Zone...**); the Device panel shows
  each parallel chain, an empty one has an **Add device...** button on its heading, and the panel's **Add device**
  fills the selected parallel chain (a device on a card can also Insert Effect Before/After);
  VCA groups are made from a mixer channel's menu (**New VCA group**,
  `src/gui/VcaStripView.cpp:195-201`) and have a strip in the mixer; the `rack.*` and `vca.*` groups are the full surface over the socket;
- auto-mastering wave 1 on the command line (`zene master`): one render, several measured candidates
  against a named loudness target — it **generates and measures; it does not pick a "best"**;
- a reproducible export render (exports render on one thread; live playback still uses the pool) — 7
  of the 9 projects the determinism sweep covers are bit-reproducible, and the two that are not are
  named, with the cause in their own instruments (`bash tools/render-determinism-probe.sh --all`;
  the figures are `docs/RENDER-DETERMINISM.md` §7);
- mixer concurrency fixes carried from an external audit, save/load integrity fixes, a fix for the
  exit-time teardown abort, and four test sources recovered into the suite;
- a two-track recording **prototype** (`MultiTrackRecorder`, `RecordRingBuffer`) — capture exists,
  while `Song::record()` is still a stub, so treat it as a prototype rather than a shipped feature;
- the versioned Lua API with a compatibility policy and console, and `tools/mmpz-git/` (developer
  tooling, not part of the binary).

## Deliberately off or absent

Not in this build, and not claimed by it:

- **Session View / clip launcher** — the data layer, the launch scheduler and the seventeen
  `session.*` commands are in every build (native engine code since ARCH-4 removed the last
  `WANT_SESSION_VIEW` build option), but there is still **no clip-launch grid and no scene
  launcher** in the interface, and a launched slot does not render audio.
- **Offline HTDemucs stem separation** — in the build by default (`WANT_STEM_SPLIT` defaults ON since
  owner decision 14): the pinned model (~166 MB) is fetched on first use, and separation runs through the
  bundled CLI, which needs Python with numpy and onnxruntime, unless the build found the ONNX Runtime SDK.
  This is not the command-line stem *export* above.
- **WASM DSP sandbox** — `WANT_WASM` defaults ON, but the build falls back to OFF when the wasmtime C
  API is absent, and CI provisions none, so it is compiled out in practice.
- **The patcher canvas edits cables only where the engine routes the chain through its graph** (Rack > Chain Canvas... draws every chain as nodes and cables and adds, reorders and removes devices on any chain; the shipped devices keep a chain off the graph, so their cables are shown read-only with the reason) (a hosted plug-in's own X11-embedded editor does open, see above; there is no editor path off X11) — and no third-party plug-in of any kind has
  been proven outside the MIT fixtures this tree ships. (Ableton Link IS in the build since WF-051:
  the arranger toolbar's **Ableton Link** toggle syncs tempo, beat phase and start/stop with Live,
  Bitwig and other Link apps - proven against the reference library and between two Zene instances,
  not against Live itself; [`docs/LINK-SYNC.md`](docs/LINK-SYNC.md).)
- **Socket and bridge only, with no interface at all**, each with its stated limits in
  [`docs/KNOWN-LIMITATIONS.md`](docs/KNOWN-LIMITATIONS.md): automation modes (selectable and
  observable through `automation.mode_set` / `automation.get_state`, never in the interface); clip
  fades, crossfades and clip gain; take lanes and comping; punch in/out; freeze and
  bounce-in-place; sample-accurate automation (`automation.ramp_*` — the tick-resolution path is the
  one the interface uses); MIDI controller surfaces; the groove pool and quantise; the Ableton Link
  quantum and start/stop sync (the toggle itself is in the arranger toolbar); the patcher's wiring edit; a live loudness meter; the
  `clip.trim` / `clip.slip` verbs for a clip's source window (**clip *length* resize does work** in
  the interface); and the whole `vca.*`, `rack.*`, `modulator.*`, `comp.*`, `loop`/`chain` and
  `export.*` surface. The three things the socket itself cannot do are named too: `mixer.set_pan`
  refuses every call, a clip's source window still has no authoring gesture, and no command picks a
  "best" auto-mastering candidate.

## Build status

`build.yml` compiles the tree on six jobs covering seven platform builds: Linux x86_64 and aarch64,
macOS Intel and Apple Silicon (one job, a matrix over two architectures), and Windows (msvc-x64,
mingw64, arm64). `quality-gates` runs its **static** gates (3, 4, 6, 7, 8 and 9, with the tools-scope
steps beside them) on every push and pull request; four of its jobs are `workflow_dispatch`-only
(unit tests, coverage, the MCP bridge's end-to-end suite and the VST3 instrument fixture), so a green
`quality-gates` check covers the static gates and the bridge's own Python unit tests — and proves
nothing about whether the tree compiles. `build.yml` is what compiles
the tree.

This file does not record the live state of any CI run. [`docs/STATUS.md`](docs/STATUS.md) is the
dated status of record for the **0.2.1-alpha** tree (`post-alpha/integration` @ `5565b4b1b`,
2026-09-13), and names what could not be determined from a local worktree; the figures measured
against this tree's own release binary — command and group counts, the A16 histogram, the registered
test count, the MCP tool coverage — are in
[`docs/reports/DOCS-AGREE-REPORT.md`](docs/reports/DOCS-AGREE-REPORT.md), and the release's
capabilities are in [`docs/RELEASE-NOTES-v0.4.0-alpha.md`](docs/RELEASE-NOTES-v0.4.0-alpha.md) (and, for
what 0.3.0 introduced, [`docs/RELEASE-NOTES-v0.3.0-alpha.md`](docs/RELEASE-NOTES-v0.3.0-alpha.md)) and
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

Optional feature flags:

- `-DWANT_STEM_SPLIT=OFF` — leave out offline HTDemucs stem separation (on by default since owner
  decision 14; the published 0.3.0 alpha builds had it off).
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

**Accessibility feedback.** If a workflow is blocked or unclear with a screen reader, magnifier or
other assistive technology, use the [accessibility blocker form](https://github.com/KRUZZZZY/zene-studio/issues/new?template=accessibility-report.yml). It asks for the affected
workflow, the assistive technology and version, and the steps to reproduce - nothing about your
health or a medical diagnosis. The same route is in Help > Manual > Access.

## License and attribution

Zene Studio is licensed under the **GNU General Public License, version 2 or later**
(GPL-2.0-or-later). See [LICENSE.txt](LICENSE.txt).

## Free and activation model (current build)

**The license.** Zene Studio is licensed under the GNU General Public License, version 2 or later
(GPL-2.0-or-later); see [LICENSE.txt](LICENSE.txt). That license governs freedom, not price: it explicitly
permits anyone who distributes a copy to do so "gratis or for a fee" (`LICENSE.txt:34`), while passing the
recipients the same rights. The license text therefore settles nothing about whether any build, present or
future, is sold or has tiers.

**The current build and its distribution (current facts, as of this repository now).** The build this
repository produces has **no paid tier**, **no registration**, and **no activation**: there is no free/pro/
enterprise edition, no account, licence-key, serial-number or purchase step, and no online activation or
periodic validation. This state is grounded in the repository itself, not in the license:

- a search of the product-owned C/C++ sources (`src/`, `include/`, `plugins/`, with the vendored
  third-party trees excluded - the same exclusions as `tests/all-sources.txt`: `src/3rdparty/`, the
  `plugins/LadspaEffect/{calf,cmt,swh,tap}`, `CarlaBase/carla`, `FreeBoy/game-music-emu`,
  `MidiImport/portsmf`, `OpulenZ/adplug`, `Sid/resid`, `Xpressive/exprtk`, `ZynAddSubFx/zynaddsubfx`
  submodules, `plugins/NeuralAmp/{rtneural,nam,tests}` and `plugins/RnnoiseDenoiser/rnnoise`) for
  licence-key, activation-code, serial-number, product-registration, purchase, premium, free-trial and
  paid-tier handling returns **no product-licensing match at all**;
  the only remaining matches for broader words ("tier", "subscription", "activation") are unrelated
  technical uses: two comments that measure this product against a *competitor's* pro tier
  (`include/AudioInputPath.h:84`, `include/MultiTrackRecorder.h:86`), MIDI port and event subscription,
  and Qt signal / plugin-instance "activation". The search is repeated automatically by
  `tests/doc-pricing-model-gate.py` (test `DocPricingModelGate`).
- the build and release process documented in this repository has no commerce step: the build
  instructions above and `docs/RELEASING.md` name no account, licence key, purchase or price
  anywhere, and `docs/RELEASING.md` records that the release page is the only distribution
  point.

**Future policy changes: where they would be announced.** Nothing in this document promises a future
pricing, tiering or licensing policy; none is decided. If a future release of the project ever changes
these terms, the change is announced in that release's notes - authored as
`docs/RELEASE-NOTES-<version>.md` and published in full on the GitHub Releases page for
`KRUZZZZY/zene-studio` (the only release channel this project has; see `docs/RELEASING.md`) - so one
place to check for a policy change is the current release page and its notes.


Zene Studio is built on LMMS (https://github.com/LMMS/lmms), licensed GNU GPL v2. All upstream notices
retained. Zene Studio is not affiliated with or endorsed by the LMMS project.

Third-party code vendored in this tree keeps its own licence and notices beside it. Among it: **Ableton
Link 4.1** (Copyright Ableton AG, GPL-2.0-or-later) and the **standalone asio 1.38.2** it bundles (Copyright
Christopher M. Kohlhoff, Boost Software License 1.0), in `src/3rdparty/ableton-link/` - pin, licences and
per-file provenance in its `README.lmms` and `MANIFEST.tsv`. "Ableton" and "Ableton Link" are Ableton AG's
names; Zene Studio is not affiliated with or endorsed by Ableton.

## Naming

The product name is **Zene Studio**. The repository was renamed to `zene-studio` on 2026-09-09, and
the deferred code and branding items (wave R of [DOCS-NAMING.md](docs/DOCS-NAMING.md)) landed on top of the
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
