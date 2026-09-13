> **This page describes the tree at `post-alpha/integration` @ `5565b4b1b` — version **0.2.1-alpha**, verified
> **2026-09-13**.** For what that release changes read
> [`docs/RELEASE-NOTES-v0.2.1-alpha.md`](RELEASE-NOTES-v0.2.1-alpha.md); for what it does not do,
> [`docs/KNOWN-LIMITATIONS.md`](KNOWN-LIMITATIONS.md).

# Zene Studio — status

**Verified 2026-09-13** against `post-alpha/integration` @ `5565b4b1b`; `CMakeLists.txt` declares
`VERSION_MAJOR/MINOR/RELEASE/STAGE` = `0`/`2`/`1`/`alpha`. This page replaces the 2026-09-11 text, which
described the `v0.1.0-alpha` line (`main` @ `b61e14c75`) and is false about this tree in fourteen places (§
Claims that were false). Its evidence is four read-only audits run 2026-09-13, merged in the program workspace
**outside this repository**: `projects/lmms-fl-research/STATUS-CORRECTION-2026-09-13.md` (the corrected
picture), `PLANNED-WORK-MASTER-LIST-2026-09-13.md` and `ITEM-NUMBERING-CROSSWALK-2026-09-13.md`. Where a
measurement and this page disagree, the measurement wins — re-run it.

Verdicts are the audits' own: `HAVE` = in the tree, with a registered test, reachable in the release
configuration · `PARTIAL` = some of it exists and is reachable, the rest does not · `IN TREE, OFF IN RELEASE` =
compiled out by an option no release job passes · `ABSENT` = no code · `UNVERIFIABLE HERE` = not determined from
this worktree. An item may appear twice: `HAVE` states the code and its test, `PARTIAL` states what is missing,
not rounded up.

## Have (corrected, 2026-09-13)

- **Rename to Zene Studio** (wave R + completion layers) — `cmake/linux/zene`, the `zene` install names and
  packages; `<zene-project>` written and `<lmms-project>` still read; user state adopted, not orphaned.
  `ConfigMigrationTest`, `ProjectVersionTest`, `RelativePathsTest`, `PluginPortsMigrationTest`. Kept on purpose:
  `lmms::`, the `LMMS_*` macros and `lmms_plugin_main` — renaming the plugin entry symbol is an ABI break.
- **Brand placeholders** — 41 shipped identity images that were still upstream LMMS artwork replaced with
  hand-authored stand-ins; the real mark is not in this release. `tools/brand/rasterise-placeholders.py`,
  `tests/brand-resource-sweep.py`; `PluginLogoResourceTest`.
- **Agent control surface** — one command registry plus an opt-in JSON-RPC control socket (SPEC A11–A16), 19 groups
  / 74 commands: `app`, `arrangement`, `audio`, `automation`, `clip`, `control`, `dsp`, `midi`, `mixer`, `note`,
  `plugin`, `project`, `render`, `roll`, `script`, `settings`, `telemetry`, `track`, `transport`.
  `src/core/ControlRegistry.cpp`, `ControlServer.cpp`, `ControlServerSocket.cpp`, `ControlSchema.cpp`,
  `ControlSession.cpp`, `include/ControlRegistry.h`, 19 `ControlCommands*.cpp`; `ControlRegistryTest`,
  `ControlSocketIntegration`, `ControlShutdown`, `ControlReadiness`, `ControlNoAudioDevice`,
  `ControlNegativeControl`, `agent_surface`, `ControlSocketPathSafety`. In every release job; opt-in via
  `--control-socket <path>`.
- **A16 reversibility, stable ids, unattended operation** — one table row per command shipped as data,
  anti-drift-tested both ways (`src/core/ControlReversibility.cpp`, `ControlReversibilityTable.cpp`,
  `src/core/ProjectRevisions.cpp`; `ReversibilityContractTest`, `ReversibilityUndoTest`;
  `docs/A16-REVERSIBILITY.md`); `trk-<n>` created and written into the project file, so it survives save/reload
  (`src/core/ProjectIds.cpp`, `include/ProjectIds.h`; `StableTrackIdsTest` — the other five id families are
  index-derived, § PARTIAL); and no modal may block a `--control-socket` instance (`src/core/UnattendedRun.cpp`,
  `include/UnattendedRun.h`; `ControlHeadlessProjectOpen`, `ControlHeadlessWorkingDirectory`,
  `ControlHeadlessNoAudioDevice`).
- **`control.undo` crash fix; `--control-socket` path safety** — the socket path SIGSEGV'd the DAW via
  `PatternStore::updateComboBox()` and the GUI's Ctrl+Z reaches the same fault; an arbitrary socket path is no
  longer `unlink`ed before bind, and an over-cap connection is drained. `src/core/ControlServerSocket.cpp`;
  `ReversibilityUndoTest`, `ControlSocketPathSafety`.
- **Telemetry v1 + its kill switch** — opt-in, default off, a closed 24-field allowlist and a payload preview;
  `ZENE_TELEMETRY=OFF` compiles it out and drops the registry 74 → 72. `src/core/Telemetry.cpp`,
  `TelemetryNetworkTransport.cpp`, `src/gui/TelemetryConsentDialog.cpp`; `TelemetryTest`, `agent_surface`.
- **Crash reporter; autosave / session recovery** — offline local reporter; recovery gated on the file belonging to
  *this* project and being newer than it. `src/core/CrashReporter.cpp`, `src/core/ProjectRecovery.cpp` and their
  headers (all fork-NEW); `CrashReporterTest`, `ProjectRecoveryTest`.
- **Plugin scan cache + quarantine** — a JSON cache with `isQuarantined`/`addToQuarantine`, wired through
  `src/core/PluginFactory.cpp` and off the startup path. `src/core/PluginScanCache.cpp`,
  `include/PluginScanCache.h`; `PluginScanCacheTest`. Data layer only — no GUI, and quarantining is a hand-written
  JSON entry.
- **LUFS metering + the loudness report** — BS.1770-4 / EBU R128 integrated loudness, short-term max and true peak,
  as a `.loudness.txt` sidecar on render/export, with an export-dialog checkbox and the CLI `--loudness-report`.
  `src/core/LufsMeter.cpp`, `LoudnessReport.cpp`, `src/core/ProjectRenderer.cpp`, `src/core/main.cpp`;
  `LufsMeterTest`, `LoudnessReportTest`; `docs/LUFS-WIRING.md` (`docs/LUFS-METER.md` marks its disarmed statements
  `[SUPERSEDED]`). Offline only (§ PARTIAL).
- **Stem export** — `RenderManager::exportStems` and the CLI action `exportstems`. `include/RenderManager.h`,
  `src/core/RenderManager.cpp`, `src/core/main.cpp`; `StemExportTest`, `StemJobManagerTest`, `StemModelStoreTest`,
  `StemSplitPipelineTest`. Not gated by `WANT_STEM_SPLIT`, which gates the separation path only. CLI/headless only
  (§ PARTIAL).
- **Warp engine** — markers pinned to source frames; a clip follows or leads the project tempo.
  `include/WarpMarkers.h`, `<warp>` read in `src/core/SampleClip.cpp`; `WarpMarkersTest`, `ClipWarpPersistenceTest`,
  `SampleClipWarpTest`. Resampling stretch, so it changes pitch.
- **Racks, VCAs, and `RoutingGraph` in the live audio path** — racks are parallel chains plus a chain selector,
  persisted as `<rack>` (`include/Rack.h`, `src/core/Rack.cpp`, `RackNodes.cpp`; `RackTest`); VCAs are relative
  volume/mute/solo with bit-exact reversibility, persisted as `<vcagroup>` (`src/core/VcaGroup.cpp`,
  `include/VcaGroup.h`, `Mixer::createVcaGroup`; `VcaGroupTest`); `RoutingGraph` is instantiated by
  `src/core/Rack.cpp`, `EffectChain.cpp`, `Mixer.cpp`, `RoutingChainNodes.cpp` (`RoutingGraphTest`,
  `RoutingGraphLiveTest`, byte-identical against a pre-change reference render). No UI, no scripting path, no
  patcher GUI (§ PARTIAL).
- **MIDI learn; MPE** — MIDI learn is Edit ▸ MIDI Learn with the binding saved in the project and the cross-thread
  race fixed by binding on the GUI thread (`src/core/MidiLearn.cpp`, `src/gui/MidiLearnGui.cpp`; `MidiLearnTest`,
  `MidiLearnThreadTest`, `MidiLearnGuiTest`). MPE captures, stores and edits per-note expression, with pitch applied
  on playback and pressure/timbre stored but not applied (`src/core/midi/MpeExpression.cpp`,
  `include/MpeExpression.h`; `MpeExpressionTest`, `MpeInputPathTest`, `MpeNoteStorageTest`).
- **MIDI depth: note probability + velocity jitter** — seeded per note and saved, plus a note search-and-transform
  API; `probability()` serialises as `prob`. `src/core/NoteRandom.cpp`, `NoteTransform.cpp`, `include/NoteRandom.h`;
  `NoteRandomTest`, `NoteTransformTest`, `MidiProbabilityPersistenceTest`. No UI or command reaches it (§ PARTIAL).
- **Automation modes** — the Read/Touch/Latch/Write engine with the touch/latch state machine and trim offset.
  `include/AutomatableModel.h`, `src/core/AutomatableModel.cpp`, consumed by `src/core/Song.cpp`;
  `AutomationModesTest`. Nothing can select or persist a mode (§ PARTIAL).
- **Mixer concurrency fixes; recording / realtime-path fixes** — the audit's D1–D6 (mute dropout,
  handler-reallocation crash, channel-add use-after-free, unguarded effect reorder, sidechain cleanup omission), all
  inherited from upstream and fixed with a ThreadSanitizer before/after (`src/core/EffectChain.cpp`, `Mixer.cpp`;
  `MixerConcurrencyTest`, `AudioBusHandleTest`, `tests/run-mixer-concurrency-tsan.sh`); and the recorder clamps
  out-of-range samples on the writer thread with in-range audio bit-identical, while the capture path no longer
  takes the model lock (`src/core/audio/TrackRecorder.cpp`; `RecordingRealtimeTest`, `MultiTrackRecorderTest`,
  `RecordRingBufferTest`, `TwoTrackRecordingHarness`).
- **Save/load integrity** — a failed save is refused and reported with the destination left byte-identical; a
  project saved without the Session View feature no longer loses that block; a failed open no longer leaves
  autosave, undo and modified-tracking off until restart. `src/core/DataFile.cpp`, `Song.cpp`;
  `DataFileSaveIntegrityTest`, `ProjectOpenIntegrityTest`, `DataFileFormatTest`.
- **Reproducible render** — export renders single-threaded; live playback still uses the pool.
  `tools/render-determinism-probe.sh`; `RenderJobQueueTest`. 7 of the 9 projects the sweep covers are
  bit-reproducible; two are not, and the cause is inside their instruments.
- **Lua API stabilisation** — versioning plus a compatibility policy, a console and a package format.
  `src/core/ScriptApiVersion.cpp`, `ScriptConsole.cpp`, `ScriptPackage.cpp`, `ScriptCommandQueue.cpp`;
  `ScriptStabilisationTest`, `ScriptEngineTest`, `ScriptBindingsTest`. The package-format UI and the user package
  dir are not done (§ PARTIAL).
- **mmpz-git depth** — three-way merge, conflict presentation, a CI render recipe. `tools/mmpz-git/demo_check.py`,
  `render-recipe.sh`. Tooling only — not in the binary.
- **Test hygiene** — the teardown abort (one audio worker missing its single wake-up, then deleted as a live child)
  fixed; four test sources that could never run recovered; a gate now fails a test source registered nowhere.
  `src/core/AudioEngineWorkerThread.cpp`, `tests/src/core/AudioEngineTeardownTest.cpp`,
  `tests/unregistered-tests-gate.sh`.
- **Gate hardening, an honest coverage gate, release honesty, the version gate** — the Gate 9 scope manifest,
  skipped-gate-fails (`tests/run-all-gates.sh` exits 3), a real coverage entry floor (`tests/coverage-gate.sh`), the
  honesty manifest (`tests/advertised-features.tsv`, six compile-time rows bound to the binary and enforced by
  `tests/release-honesty-gate.sh`, measured 6-of-6 PASS on the release configuration) and
  `tests/release-version-gate.sh`, wired into all six package jobs. `docs/GATE-HYGIENE.md`,
  `docs/VERIFICATION-DEBT-FIXES.md`, `docs/COVERAGE-GATE-GREEN.md`.
- **Auto-mastering wave 1** — `zene master` renders once and measures five candidates against a named loudness
  target; it does not rank them. `src/core/MasteringChain.cpp`, `MasteringJob.cpp`; `MasteringTest`.
- **PDC and its scope registration** — `include/LatencyCompensation.h`, `src/core/LatencyCompensation.cpp`;
  `PdcMixerTest`. Both are in `tests/fork-sources.txt` and banked in `tests/coverage-baseline.tsv`; the v0.1.0-alpha
  `HAVE` verdict for PDC (2026-09-10) stands.
- **Already in the tree before the 0.2 line, and still there** — the multi-channel port model and unbounded mixer
  (`AudioBus`, `AudioPorts`); the two-track recording prototype (`MultiTrackRecorder`, `RecordRingBuffer`) with
  `Song::record()` still a stub; Lua 5.4 with an instruction budget; slide notes; HiDPI scaling; the inherited LMMS
  core (piano roll, song editor, mixer, 15+ synths, SF2, VST2/Vestige, LADSPA, LV2, MIDI I/O); RNNoise and NAM under
  AI DSP. **Part C migration** is in the tree, but its counts contradict across documents (92/93 vs 90/93 files
  differing from base; 44 vs 40 sample-exact renders), recorded in the planned-work master list and not resolved;
  its four remote-plugin families are proven at build + unit-test level only.

## PARTIAL — exists, not usable

- **VST3 instrument hosting** — *Exists:* `plugins/Vst3Instrument/` builds module `vst3instrument`, gated by
  `WANT_VST3` (declared AUTO; **all 7 release jobs pass `-DWANT_VST3=ON`**); `cmake/modules/PluginList.cmake`; the
  contract row `vst3-instrument-hosting WANT_VST3 ON vst3instrument`, enforced by `tests/release-honesty-gate.sh`
  (`[PASS] … ON matches ON`). *Missing:* CLAP instrument hosting (`ClapEffect` hosts effects only); a plugin editor
  (`IPlugView` unimplemented, so what opens is the host's generated knob grid); more than one instrument per track;
  multi-out, presets and instrument PDC; and its own in-tree suites (`Vst3InstrumentTest`,
  `Vst3InstrumentIntegrationTest`) sit behind `WANT_VST3_TEST_INSTRUMENT`, **default OFF**, so CI never runs them.
  Proven only against the MIT test instrument that ships in the source.
- **Automation modes** — *Exists:* the model and its test (`include/AutomatableModel.h`,
  `src/core/AutomatableModel.cpp`, `tests/src/core/AutomationModesTest.cpp`). *Missing:* any way for a user or an
  agent to select or persist a mode — `setAutomationMode` is called **only** inside that test, the mode is
  serialised nowhere, and `automation.mode_set` registers a full schema then **refuses every call**, citing "the
  shipped alpha has no automation modes" and a `docs/KNOWN-LIMITATIONS.md` line that is now false. The refusal is
  honest; its reason is stale. Ruling and method: the correction file, §8.
- **LUFS / stem export** — *Exists:* the offline meter and render/export report; the `exportstems` CLI. *Missing:* a
  live GUI meter (deliberately out of scope, `docs/LUFS-METER.md`) and any stem-export dialog control (`grep
  StemExport src/gui` returns nothing).
- **Racks / VCAs** — *Exists:* engine, persistence, tests (above). *Missing:* a UI and a scripting path — a rack or
  a group can only arrive by loading a project that already contains one. Macros and key/velocity zones are outside
  the rack slice, and chain switching is not crossfaded, so it can click.
- **Out-of-process plugin hosting** — *Exists:* VST2/Vestige is always separate-process; ZynAddSubFx has an opt-in
  per-plugin `separateprocess` toggle; `RemoteZynAddSubFx` builds unconditionally; `ZynSeparateProcessTest`,
  `RemotePluginClientE2ETest`. *Missing:* isolation for the new families — `docs/KNOWN-LIMITATIONS.md` records "no
  out-of-process hosting" for the VST3 instrument, and VST3/CLAP isolation is an unstarted follow-up in
  POST-ALPHA-PLAN.
- **Multicore scheduling** — *Exists:* inherited per-channel parallelism, on by default (`src/core/AudioEngine.cpp`
  workers; `src/core/Mixer.cpp` queues every ready channel). *Missing:* no scheduler over the `RoutingGraph` (chain
  nodes run in order), no thread-count option, and export is deliberately serialised
  (`src/core/ProjectRenderer.cpp`). No registered test asserts parallel scheduling; what exists is
  teardown/concurrency coverage.
- **Note probability** — *Exists:* engine, persistence and tests (above). *Missing:* any UI or command that reaches
  it.
- **Clip source-window trim / slip** — *Exists:* `include/SampleWindow.h` (a half-open `sourceIn`/`sourceOut` range
  with `clamped()`), `srcin`/`srcout` serialisation in `src/core/SampleClip.cpp` and its read-back path;
  `SampleClipWindowTest`, `ClipSerialisationTest`. *Missing:* an authoring gesture — nothing in the GUI or the
  plugins sets the source window (`grep setSampleWindow src/gui plugins` = 0), and there is no slip tool or command;
  `docs/KNOWN-LIMITATIONS.md` records "no trim, slip, fade, crossfade or clip-gain tools in the UI yet". **Note the
  two senses of "trim":** clip-*length* resize **is** `HAVE` (command `clip.resize` in
  `src/core/ControlCommandsClip.cpp`, plus the ClipView edge-drag); the source window is the `PARTIAL` one. Two
  audits graded these differently and both readings are recorded.
- **Scale awareness** — *Exists* (inherited, in the release): piano-roll scale/key selector, root and key
  highlighting, persistence, playback chord/scale stacking (`src/gui/editors/PianoRoll.cpp`). *Missing:* no
  registered ctest, no chord track, no chord detection, no generators (`docs/MIDI-DEPTH.md`: "No chord track, no
  groove pool"). `include/Scale.h` is Scala tuning for the Microtuner, not pitch-class scales.
- **Browser audition + drag-and-drop** — *Exists* (inherited, ungated): audition from the file browser
  (`src/gui/FileBrowser.cpp`, `previewFileItem()`), Space preview, favourites, `StringPairDrag`, and a waveform peak
  cache (`src/gui/SampleThumbnail.cpp`). *Missing:* no registered ctest covers `FileBrowser`, so it does not meet
  the `HAVE` bar; the boarded upgrade (tags/metadata search, ML similarity, browser waveform, key/BPM columns,
  hot-swap) never landed.
- **Quality gates / local CI reproduction** — *Exists:* the static gates (3, 4, 6, 7, 8, 9) run on push / PR /
  dispatch and the `static gates` job in `.github/workflows/quality-gates.yml` carries no `if:` guard (run
  `34725343778` succeeded on a push to main); `tools/local-ci.sh` and `docs/LOCAL-CI.md` are the inner loop with
  CI's own flags. *Missing:* the build-backed jobs (unit tests + coverage, and Gate 5) stay dispatch-only, and local
  reproduction is Linux only — no Windows or macOS toolchain here, so the 7-job matrix is not reproducible on this
  box.
- **Auto-mastering** — *Exists:* wave 1 (above). *Missing:* waves 2–3 — the reference-matching arm and the learned
  ranker (the ranker is conditional on pick-logs, i.e. on users).
- **The agent tooling ladder** — *Exists:* 74 commands in 19 groups, A16, `trk-<n>`. *Missing:* the per-wave groups
  (`session.*`, `warp.*`, `rack.*`, `comp.*`, `link.*`, `browser.*`) and the boarded-gaps commands
  (`clip.trim/slip/fade`, `record.punch_set`, `plugin.scan`, `mixer.lufs_read`, `render.freeze/bounce`,
  `automation.record_mode_set`, `groove.*`, `scale.*`, `note.probability_set`, `patcher.*`,
  `scheduler.multicore_set`, `telemetry.consent_set`) — each needs its commands the day its feature lands; and
  **stable-ID slice 2**, where five id families are still index-derived and only `trk-<n>` is persistent.
- **Coverage** — the old page's coverage bullet is stale, not merely low. The newest in-tree capture
  (`build-coverage/coverage/coverage-fork.info`, 2026-09-12) parses to **87.21%** — 13770/15790 lines over 165 files
  — and `tests/fork-sources.txt` holds **244** non-comment entries, not 97; both re-measured for this page. The
  76.07% figure survives only as a 2026-09-09 measurement and cannot be recomputed, because no 97-entry snapshot
  exists. `plugins/NeuralAmp` and `plugins/RnnoiseDenoiser` are still at 0% with no registered test file.

## In the tree, off in the release configuration

- **Session View data layer + launch scheduler** — `WANT_SESSION_VIEW` defaults **OFF** (`CMakeLists.txt:121`) and
  no release job passes it. In the tree: `include/SessionModel.h`, `src/core/SessionModel.cpp`,
  `SessionScheduler.cpp`, wired into `Song::processNextBuffer` under `#ifdef LMMS_HAVE_SESSION_VIEW`; its tests
  register only inside `IF(LMMS_HAVE_SESSION_VIEW)` (`tests/CMakeLists.txt`). Even a flag-ON build has **no clip
  launcher and no grid**.
- **Offline HTDemucs stem separation** — `WANT_STEM_SPLIT` defaults **OFF** (`CMakeLists.txt:120`), absent from
  every release build. This is **not** the `HAVE` stem *export* above.
- **WASM DSP sandbox** — `WANT_WASM` defaults ON but **degrades to OFF** when the wasmtime C API is absent
  (`CMakeLists.txt`), and CI provisions none, so it is compiled out in practice. The `advertised-features.tsv`
  "required OFF" row holds by dependency absence, not by choice; `docs/INDEPENDENT-NOTES-READ.md` B5 raises this and
  it is not closed.
- **VST3 instrument regression tests** — `WANT_VST3_TEST_INSTRUMENT` defaults **OFF** (`tests/CMakeLists.txt`); the
  host module ships, its own tests do not run in CI.
- **CLAP hosting; Qt6** — CLAP is in the release on Linux and macOS and **OFF on the three Windows jobs**
  (`ClapHost.cpp` needs `dlfcn.h`); the manifest records it per platform and the honesty guard asserts the absence
  on Windows. Qt6 is built only by the msvc job (`-DWANT_QT6=ON`); the other six use the default (Qt5).
- **The telemetry client is the opposite case, listed so nobody has to infer it** — `ZENE_TELEMETRY` defaults **ON**
  (`CMakeLists.txt:140`), so it **is** in the release; it compiles out only with `-DZENE_TELEMETRY=OFF` (74 → 72
  commands), and `--version` never reports it (lowercase `option()`, matching neither `^WANT` nor
  `LMMS_(HAVE|DEBUG)`).
- The honesty gate documents the first three as **absent**: `tests/advertised-features.tsv` carries `session-view`,
  `wasm-sandbox` and `stem-separation` as required-OFF rows, and `tests/release-honesty-gate.sh` fails a
  documented-absent feature the build reports ON.

## Absent

Genuinely no code, each verified by grep plus a named doc: fades, crossfades and clip gain; take lanes, comping
and punch in/out; arbitrary input count; input monitoring; CLAP instrument hosting; sample-accurate automation
(it resolves once per tick, `src/core/Song.cpp:404`); freeze; bounce-in-place; controller surfaces; groove pool;
Ableton Link; modern stock devices (`34`); factory content (`35`); design system (`36`); auto-mastering waves
2–3; and the patcher GUI (`grep Patcher src/gui` finds only unrelated files). Deliberately excluded rather than
merely unbuilt, per the roadmap-gap register (carried in the planned-work master list): notation/score,
surround/immersive, expression maps/articulation switching, and the ecosystem list (EuCon/HUI,
video/timecode/DNx, AAF/OMF/MXF, cloud collaboration, AAX, AU, Avid marketplaces). Decisions, not gaps.

## Claims that were false

The 2026-09-11 page against this tree. "**Already false when written**" means the same file's later sections
record the fix, so it contradicted itself from the start. Line numbers are that page's.

1. **"VST3 hosting — Effects only"** (:23-24, :196). `plugins/Vst3Instrument/` declares `Plugin::Type::Instrument`,
   the module builds, `PluginList.cmake:78` lists it, all seven release jobs pass `-DWANT_VST3=ON` and the contract
   row is enforced. False for VST3; stale for LV2; still true for CLAP (effects only).
2. **"clip trim, slip, fades, crossfades, clip gain" as a gap with no code** (:194, :93). Trim and slip have code,
   persistence and registered tests; fades, crossfades and clip gain remain absent — `PARTIAL`, not absent.
3. **"`RoutingGraph` present and unit-tested — but nothing in the app instantiates them"** (:21, :49, :74-75, :226).
   `Rack.cpp`, `EffectChain.cpp` and `Mixer.cpp` do, and `RoutingGraphLiveTest` is registered. Landed `3875183fa`.
4. **"Quality gates run `workflow_dispatch`-only"** (:213). The workflow triggers on push, PR and dispatch and the
   `static gates` job has no `if:` guard; run `34725343778` succeeded on a push to main. **Already false when
   written** — its lines 50-52 state the correct, narrower version.
5. **"Gate 2 has no coverage floor"** (:214). `tests/coverage-gate.sh` sets
   `ENTRY_FLOOR="${COVERAGE_ENTRY_FLOOR:-50.00}"` and ships `tests/coverage-entry-floor-exempt.txt`. **Already false
   when written** — :253 records the floor as fixed.
6. **Session View "Not on `main`; no UI"** (:35-38). "Not on main" was false the next day: PR #5 merged 2026-09-12
   (`f6364b7bd`, 1677 additions across 15 files). True in the page's 2026-09-11 frame; "no UI" is still true.
7. **"47 of the 97 in-scope files measured on 2026-09-09: 76.07%"** (:39-42). Stale: 87.21% (13770/15790 over 165
   files) and 244 entries in `tests/fork-sources.txt`; the 76.07% figure cannot be recomputed.
8. **`LatencyCompensation.{h,cpp}` "outside `tests/fork-sources.txt`"** (:43-45). Registered at `:208`/`:355` and
   banked in `coverage-baseline.tsv`. **Already false when written** — :245-247 and :269-270 restate the
   registration as done.
9. **"automation modes and sample-accurate automation" absent** (:95-96, :198). The modes half is contradicted
   (model implemented and tested, § PARTIAL); the sample-accurate half is **not**.
10. **"LUFS metering, freeze, bounce-in-place; stem export" have no code** (:96-97, :199). LUFS and stem export are
   real and wired; freeze and bounce remain absent — `PARTIAL`.
11. **"MIDI learn" a documented gap** (:100, :200). It landed, with three registered tests.
12. **"note probability" a documented gap** (:101, :200). False as an absence — model, engine, persistence and tests
   exist — but it is unreachable from any UI or command. `PARTIAL`.
13. **"autosave recovery; crash reporter" documented gaps** (:103, :202). Both exist, both fork-NEW, both with
   registered tests.
14. **"Any installable release (no GitHub releases exist)"** (:91). False: `v0.1.0-alpha` is a published prerelease
   with 7 assets. The real distinction is that **no published 0.2.x release object exists** — the `v0.2.0-alpha` and
   `v0.2.1-alpha` tags are not on the releases page, because packages upload only for tag or `workflow_dispatch`
   runs.

Also stale, outside this page: `docs/DISARMED-AUDIT.md` calls the crash reporter and the plugin scan cache
"NOT-ON-BRANCH" and both are now ancestors of HEAD; `docs/LUFS-METER.md`'s headline "Nothing calls it" was true
when written and is false now; and `tools/mcp-zene-control/zene_control/commands_snapshot.json` holds 70
commands against the registry's 74, with its README still saying "23 generated tools".

## The numbering problem

**The numbers this page's predecessor quotes have no enumeration anywhere.** The old page speaks of Bar-2 "items
1–45" and references numbers in prose (`13`, `18`, `21`, `34/35/36`, `38`, `39`, `40/41/42/44`, `608`), but no
literal 1–45 inventory exists — not in this file's history (checked to the last eight revisions) and not in the
one enumerated Bar-2 table (`ZENE-COMPLETION-ASSESSMENT.md` §4, which is unnumbered) — so it cannot be
recovered. Only `13`, `18`, `21`, `34`, `35`, `36`, `38`, `39`, `40`, `41`, `42`, `44` and `608` have meanings
any source establishes; every other number is unknown on purpose and is not guessed here.

**`BACKLOG.md`'s owner's-31 list reuses the same integers for different items.** Two collisions are established:
`18` is *third-party instrument hosting* here and *project collection/archive* in the owner's list; `21` is
*multicore scheduling* here and *folder tracks as routing/mix groups* there. The schemes are reconciled in
`projects/lmms-fl-research/ITEM-NUMBERING-CROSSWALK-2026-09-13.md` (program workspace, outside this repository),
which recommends keeping this page's numbering — `BACKLOG.md`, the change-plan intake and the ecosystem-gap
analysis all cite it — and prefixing the owner's list instead. The crosswalk is the resolver; this page does not
invent an enumeration to replace it. Until a 1–45 list is published, quote a number only with the meaning the
crosswalk gives it.

Different numbering, and not a problem: the 0.2.0-alpha release-gate items **are** enumerated, as Gate 1–12 in
`V0.2-ALPHA-PLAN.md` (carried in the planned-work master list). Those are citable; the Bar-2 integers are not.

## Where the documents are

- `README.md` — product-facing summary, scoped by its own caveats and by this file.
- `tests/QA-GATES.md` — gate definitions, measurements and the open gate defects.
- `docs/VERIFICATION-DEBT-FIXES.md` — **in this tree**: the three 2026-09-11 verification-debt fixes, their
  red/green fixture proofs and their exit codes.
- `docs/STEM-EXPORT.md` — **in this tree**, and the record for the `HAVE` stem export.
- `doc/STEM-SPLIT.md` — **in this tree too** (tracked, beside `docs/phase-f/`): historical program artifacts from
  the pre-product branch stack, kept for provenance, not product documentation.
- `docs/KNOWN-LIMITATIONS.md` — in this tree; there is **no** version-suffixed 0.2.1 limitations file.
- `DOCS-NAMING.md` — the naming decision and the remaining wave-R rename checklist.
- Program workspace, **outside this repository**: `STATUS-CORRECTION-2026-09-13.md`,
  `PLANNED-WORK-MASTER-LIST-2026-09-13.md`, `ITEM-NUMBERING-CROSSWALK-2026-09-13.md` and the sources they name
  (`V0.2-ALPHA-PLAN.md`, `POST-ALPHA-PLAN.md`, `ableton-gap/`, `verification/`, `ui-research/`). A reader of this
  repository alone cannot open those.

## Unverifiable here (carried, not silently dropped)

- The **live CI matrix state at this tip**: no build, ctest or GitHub Actions re-run was performed by the audits.
  `gh run list` showed the build workflow **FAILED** on the `v0.2.1-alpha` tag push (`34725347297`); per-job logs
  were not read. The old page's "6 of 7 green" is a 2026-09-11 snapshot.
- **Whether released artefacts contain the `vst3instrument` module**: the tip honesty log receives no artifacts
  ("module presence is not checked"), and the 0.2.0 release-prep log records a `[FAIL]` with artifacts. Needs CI
  artifact inspection. Likewise **whether the VST3 instrument suites pass** in a release configuration (option
  default OFF), and whether a `WANT_SESSION_VIEW=ON` or `WANT_STEM_SPLIT=ON` build compiles at all.
- The **`specs/` citations**: several docs cite `specs/SPEC-lua-api-v0.md` and `specs/SPEC-two-track-recording.md`,
  but no `specs/` directory is tracked — those paths are not here.
- **Runtime behaviour** of audition, drag-and-drop, MIDI learn, racks, warp and stem export: not exercised in a
  running binary. **Windows and macOS** behaviour is inferred from `build.yml` options and committed logs, never
  compiled on this box. "Reachable in release" is derived from CMake option declarations, the release jobs'
  configure arguments, ctest registration and the honesty artefacts.

This file is the status of record **for `5565b4b1b` / 0.2.1-alpha**. Update it when a claim above changes, and
date the change.
