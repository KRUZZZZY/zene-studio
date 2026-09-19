# Zene Studio 0.3.0 — capability specification (v1)

**What this document is.** The specification of what the 0.3.0-alpha build can do: one section per
capability domain, each naming the engine, the control-surface commands that drive it, its bounds, and
the artefact that proves the claim. It is a specification, not marketing — every claim is either
measured here, or carries a pointer to the registered proof that measures it, or is marked as not
verified from this worktree.

**Base of measurement.** This document was written in the worktree `030/capabilities` at commit
`b89d10a625342757dc7f01be413c4e63e2436cf7` (the 0.3.0-alpha release tip, version bump included) and
measured against the binary built from that same tip at `zene-030/build/zene`:

| artefact | measured value | how |
| --- | --- | --- |
| binary sha256 | `ab3f9b322bd2ef8ae4569223befb1ebcd1fdebd07d923d52d953cb5a8cb6fb92` | `sha256sum build/zene` |
| `zene --version` | `Zene Studio 0.2.1-alpha.626+b89d10a` | git-describe tag line for a non-base build (`cmake/modules/VersionInfo.cmake`); `CMakeLists.txt` declares `0.3.0-alpha` |
| registered tests | **214** | `ctest -N` in `zene-030/build/tests` |
| control surface | **340 command ids**, 53 id prefixes, in this build's configuration | SPEC A16 contract table (one row per command), ratcheted by `ReversibilityContractTest`; see §1 headliners and Appendix A |
| agent surface | opt-in, protocol v1, JSON-RPC line protocol over AF_UNIX / Windows named pipe | §3.11 |

**How to read it.** Bounds are stated with the claim, never left to be discovered — where a capability
has no interface, this document says so the way `docs/KNOWN-LIMITATIONS.md` does. `docs/RELEASE-NOTES-v0.3.0-alpha.md`
is the lane-by-lane record this document condenses; `docs/FEATURE-LIST-0.3.0.md` is the row list;
`docs/KNOWN-LIMITATIONS.md` is the honest bounds page, and §5 of this document condenses it with
pointers rather than repeating it.

**Three configurations, kept apart on purpose.** This program's numbers move with the build
configuration, so each claim below names which one it describes:

1. **The tree build** measured here (`zene-030/build/zene`): the wasmtime C API is provisioned on this
   box, so the WASM DSP sandbox is compiled **in** — 340 command ids.
2. **The release binaries** the CI matrix ships: no runner provisions the wasmtime C API, so
   `WANT_WASM` degrades to OFF and the `wasm-sandbox` row of `tests/advertised-features.tsv` holds by
   dependency absence — 332 command ids (the committed, ratchet-enforced bridge snapshot
   `tools/mcp-zene-control/zene_control/commands_snapshot.json` records exactly this count).
3. **The A16 contract table** publishes its histogram once, for the configuration with telemetry in,
   the wasmtime sandbox in, the Session View data layer in and the offline stem engine out — 340 rows;
   the registered test applies the documented per-option deltas (`wasm.*` +8, `session.*` +17,
   `telemetry.*` +2, `stem.*` +7) for a build that differs.

---

## 1. What 0.3.0 is

**0.3.0 is an engine-and-agent release, not a UI release.** The release's own promise, verbatim from
`docs/RELEASE-NOTES-v0.3.0-alpha.md` §The shape of 0.3.0 (and echoed in `docs/FEATURE-LIST-0.3.0.md`):

> **Everything is operable through `--control-socket` and the MCP bridge; almost nothing is operable
> from the interface.**

**The four-part scope contract** (charter `NEXT-0.3.0-AGENT-PROMPT.md` §3.1, quoted in
`docs/FEATURE-LIST-0.3.0.md`). A feature is in 0.3.0 only if all four hold:

1. the engine work is in the tree;
2. it has a control-surface command group — ids, argument and result schemas, A16 reversibility metadata;
3. it has a proof — a registered ctest, or a control-surface transcript committed as evidence;
4. its UI absence is written down (`docs/KNOWN-LIMITATIONS.md`).

If it cannot be driven or observed through the socket, it is not in this release. That contract is why
this document is organised around command groups: the groups are the product's actual interface in this
release.

**Headline measured numbers.**

- **Platforms.** Six CI job definitions cover **seven platform builds**, from `.github/workflows/build.yml`:
  linux-x86_64 (`ubuntu-22.04`), linux-arm64 (`ubuntu-24.04-arm`), macOS x86_64 (`macos-15-intel`),
  macOS arm64 (`macos-15`, the same job definition over a two-entry arch matrix), mingw64
  (`ubuntu-latest` cross-build), msvc-x64 (`windows-2022`), windows-arm64 (`windows-11-arm`, msys2) —
  plus a `release-gate` job that requires the matrix green. **Five of the seven run the registered
  tests** (both Linux jobs, both macOS arches, msvc-x64); **mingw64 and windows-arm64 build and package
  only** (mingw64's own step comment: “this job builds a Windows target and runs no tests”).
- **Registered tests: 214** — `ctest -N`, universe `zene-030/build/tests` (engine QTests, the
  control-surface transcript tests registered from `tests/*.py`, and the suite-level gates). Exact
  command in Appendix B.
- **Command surface: 340 command ids in 53 id prefixes** in this build's configuration — the SPEC A16
  contract table holds **one row per command** and its histogram is machine-checked against the live
  table by the registered `ReversibilityContractTest` (the block is in `docs/RELEASE-NOTES-v0.3.0-alpha.md`,
  re-measured by `bash tools/dawproject-proof.sh` part 2). The class split, for the configuration
  named there: **340 rows — 158 `true_inverse`, 32 `snapshot`, 13 `irreversible`, 137 `not_mutating`**.
  In the release binaries the eight `wasm.*` ids are compiled out: **332 ids**.
- **The A16 contract itself.** Every command ships with a reversibility class, and the registry
  anti-drift test checks both directions: a command without a row fails, a row without a command
  fails, a duplicate row fails by name. `control.undo` / `control.redo` and the transaction record
  (`control.transactions`) are the agent-facing half — see §3.11.
- **The registry sources at this commit:** 118 `src/core/ControlCommands*.cpp` translation units and
  52 declared `cmd.group` literals plus the two groups (`chord`, `modulator`) whose group name is set
  through their own constant. The authoritative per-id list is Appendix A (generated from a live
  instance).

**What is not in 0.3.0, in one line each** (detail in §5): no interface for almost any of it; no clip
launcher grid; no audio through a launched Session slot; the WASM sandbox absent from the release
binaries; the Windows named-pipe half proven by CI only; no hardware MIDI/audio device exercised on
this box.

## 2. How it runs

**2.1 Platforms and packaging.** Packages come only from the release page; a build job uploads a package
for a tag build or a manual dispatch, never for an ordinary push (`docs/KNOWN-LIMITATIONS.md`
§Before you download records the six conditional `upload-artifact` steps). The seven platform builds:
Linux x86_64 and aarch64 (AppImage), macOS Apple Silicon and Intel (`.dmg`), Windows x64 in two
toolchains (MinGW cross-build and native MSVC) and Windows Arm64 (`README.md` §Download). Each
published file's SHA-256 digest is generated from the release itself by the publish step
(`docs/RELEASING.md`). Options that differ per job, from the build-options dump and `build.yml`:
`-DWANT_VST3=ON -DWANT_CLAP=ON` on every platform job; `WANT_VST3_TEST_INSTRUMENT=ON` on
linux-x86_64 only; `-DWANT_QT6=ON` on msvc-x64 only (the other six use Qt5). `WANT_SESSION_VIEW` now
defaults ON (the 0.3.0 flip, enforced by the honesty row).

**2.2 The control socket.** `--control-socket <path>` is opt-in per instance; without it the socket
code is inert. The transport is AF_UNIX on Linux/macOS and a **Windows named pipe** on Windows
(`src/core/ControlServerWin32.cpp`, every line inside `#ifdef Q_OS_WIN`, created with
`PIPE_REJECT_REMOTE_CLIENTS` so a client on another machine cannot connect). The protocol is
line-delimited JSON-RPC, protocol version 1 (`control.version`), one request per line with
`{id, cmd, args, proto}`, one reply per request with `ok` true or a **typed** error
(`{kind, message}` — `invalid_args`, `not_found`, `irreversible`, …), and the reply is sent before a
`control.quit` shutdown runs. Socket-path safety: an arbitrary path is not unlinked before bind, and an
over-cap connection is drained (`src/core/ControlServerSocket.cpp`, `ControlSocketPathSafety`); the
Windows half of the contract is proven by `ControlNamedPipeSmoke`
(`tests/control-named-pipe-smoke.py`, registered) — CI-only evidence, because no Windows toolchain
runs on this box.

**2.3 Headless / offscreen operation.** A socket instance is meant to run unattended: the documented
start recipe is `QT_QPA_PLATFORM=offscreen`, a config whose `<audioengine audiodev="Dummy (no sound
output)"/>` matches the dummy device by character, `HOME`/`XDG_*` in a scratch directory, and the
working directory inside it (`tests/control_socket_harness.py`, the ONE harness every control-surface
test uses). **No modal may block a `--control-socket` instance** (`src/core/UnattendedRun.cpp`,
`include/UnattendedRun.h`; proven by `ControlHeadlessProjectOpen`, `ControlHeadlessWorkingDirectory`,
`ControlHeadlessNoAudioDevice`). A hang is a failure in every harness wait — a bound that expires
raises, never waits — and the crash-reporter path, the safe-start marker and the plugin scan cache are
all wired to keep that property.

**2.4 Per-instance isolation.** Each instance the harness starts gets its own temp directory: scratch
`HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, a working directory and a recovery file, all inside one
`tempfile.mkdtemp`; several instances can run side by side without sharing user state. Shutdown is
`control.quit` (the reply is sent first; unsaved changes are discarded unless `save=true`); the tests
reap by explicit PID, and the crash-recovery path (`record.*`, §3.3) is what an abnormal exit leaves
behind.
---

## 3. The capability domains

One subsection per domain. Each states what a user can do, the command groups and ids that do it (as
declared by the registry at this commit; the full per-id catalogue is Appendix A), and the bounds
recorded with the feature. The proof names are registered tests or committed transcripts.

### 3.1 Project and file format

**What you can do.** Open, save and save-as projects; the format is LMMS's `mmp` XML and its compressed
`mmpz`, still written as `<zene-project>` and still READ as `<lmms-project>` — user state is adopted,
not orphaned (`docs/STATUS.md` §Have; `ConfigMigrationTest`, `ProjectVersionTest`, `RelativePathsTest`,
`PluginPortsMigrationTest`). Sessions are recovered after a crash: autosave writes a recovery file and
the next start restores it, **gated on the file belonging to *this* project and being newer than it**
(`src/core/ProjectRecovery.cpp`; `ProjectRecoveryTest`). Every save writes a `<project>.bak` beside the
project first, and a failed save is refused with the destination left byte-identical
(`DataFileSaveIntegrityTest`, `ProjectOpenIntegrityTest`). A **revision timeline** keeps restorable
revisions of the project inside the session: `revisions.list`, `revisions.compare`, `revisions.restore`,
plus `project.restore_revision` (`RevisionTimelineTest`). Asset integrity is drivable:
`project.hash_assets`, `project.missing_assets`, `project.relink`, and `project.open` / `project.save` /
`project.get_state` are the file verbs. Safe-start mode after a crash is drivable and observable:
`safestart.get_state`, `safestart.acknowledge`, `safestart.clear`, `safestart.set_skip` — the crash
marker is written when a session dies, and `Plugin::instantiate()` consults the load-time predicate so a
suspect plugin can be skipped (`SafeStartTest`, `SafeStartLoadPathTest`, `src/core/SafeStart.cpp`).

**Bounds.** Recovery is gated (project identity + mtime), so a stale recovery file is ignored rather
than offered. Safe-start's skip predicate is consulted at instantiate time; nothing in the interface
shows either state (§5).

### 3.2 Composition and editing

**Tracks.** `track.add` (type `instrument`, `sample`, `automation`, …, or `folder`), `track.list`,
`track.get_state`, `track.rename`, `track.set_arm`, `track.set_mute`, `track.set_solo`, `track.move`,
`track.remove`, `track.get_state` — addressed by **stable `trk-<n>` ids** that are assigned at creation
and written into the project file, so they survive save/reload and reordering (`StableTrackIdsTest`).
**Folder tracks** are a real engine container with **two modes** — organisational (default) and routing,
where the folder acquires one mixer channel of its own — plus pinning and named visibility sets:
`track.set_folder`, `track.folder_get_state`, `track.folder_set_collapsed`, `track.set_pinned`,
`track.set_routing`, `track.visibility_set_{save,list,apply,remove}` (`TrackFolderTest`,
`ControlTrackFolderTranscript`).

**Clips.** `clip.add`, `clip.move`, `clip.resize`, `clip.split`, `clip.duplicate`, `clip.delete`,
`clip.select`; **edge edits**: `clip.trim` (moves the start edge, holding the audio at the same
position) and `clip.slip` (moves the audio inside a fixed clip rectangle, reporting the new `offset`);
**fades and clip gain**: `clip.set_fade`, `clip.set_gain`, `clip.crossfade`
(`ClipEditsTest`, `ClipFadesRenderTest` — the render proof asserts the equal-power identity and hashes
every render). **Linked / smart clips** are a persisted group id with a write-through mirror:
`clip.link_create`, `clip.link_get_state`, `clip.link_sync`, `clip.link_remove`
(`ClipLinkTest`, `ClipLinkPersistenceTest` — the relation survives save/reload).

**Notes.** `note.add`, `note.move`, `note.resize`, `note.remove`, `note.select`, `note.velocity_set`;
**probability and randomisation**: `note.probability_set` (chance in [0,1] that a note plays in a
take), `note.random_seed_get`/`note.random_seed_set`, `note.randomize` (seeded velocity jitter and
probability roll), `note.transpose`, `note.velocity_offset`, `note.velocity_scale`
(`NoteRandomTest`, `NoteTransformTest`, `MidiProbabilityPersistenceTest`); **slide notes**:
`note.slide_set`, `note.slide_clear` (`SlideNotesTest`); **per-note expression / MPE**:
`note.expression_set`, `note.expression_get`, `note.expression_clear`, and the input switch
`device.mpe_get_state`/`device.mpe_set` (`MpeExpressionTest`, `MpeInputPathTest`, `MpeNoteStorageTest`,
`MpePlaybackTest` — the last measures the expression against the same block without it, through the
in-tree test instrument).

**Warp and stretch.** `warp.list`, `warp.add`, `warp.move`, `warp.remove`, `warp.set` pin frames of a
clip's audio to timeline positions and choose the clip's warp tempo mode; `warp.stretch` selects
`resample` (default, changes pitch) or **`preserve_pitch`** (`AudioStretcher`; the proof measures the
440 Hz + 660 Hz two-tone input and reports the alignment search's cost; `SampleClipStretchTest`,
`WarpMarkersTest`, `ClipWarpPersistenceTest`).

**Groove, tempo and feel.** `groove.list`, `groove.extract`, `groove.set`, `groove.apply`,
`groove.quantize`, `groove.rename`, `groove.remove` (`GrooveTemplateTest`, `ControlGrooveCommands`);
tempo and time-signature events on the timeline: `transport.tempo_map_get`, `transport.tempo_map_add`,
`transport.tempo_map_set_active`, `transport.tempo_map_remove`, `transport.tempo_map_clear`
(`TempoMapTest`, `TempoMapPersistenceTest`).

**Chord track and scales.** `chord.get_state`, `chord.set`, `chord.clear`, `chord.remove`,
`chord.track_write`, `chord.detect` (detection from a clip's notes), `chord.detect_to_track`,
`chord.progression_generate` (seeded, repeatable), `chord.progression_list` (`ChordTrackTest`,
`ChordDetectTest`, `ChordProgressionTest`); the scale verbs `scale.list`, `scale.get_state`,
`scale.root_set`, `scale.set`, `scale.snap_notes` (`ControlNoteScaleVerbsTest`).

**Take lanes and comping.** `comp.lane_add`, `comp.lane_remove`, `comp.lane_list`, `comp.assign`,
`comp.select`, `comp.rebuild`, `comp.get_state` — a composite is a **view**: an ordered, gapless list
of (lane, source range) references that touches no take audio (`TakeLaneCompTest`, `TakeLaneTest`).

**Edit groups and structural undo.** VCA groups now carry a phase-locked **edit set**:
`vca.create`, `vca.remove`, `vca.list`, `vca.get_state`, `vca.rename`, `vca.set_gain`, `vca.set_mute`,
`vca.set_solo`, `vca.assign`, `vca.unassign`, `vca.set_phase_lock`, `vca.track_add`, `vca.track_remove`,
`vca.edit_move` (`ControlVcaCommandsTest`, `ControlVcaEditGroupsTest`). Structural edits are undoable
through the same journal Ctrl+Z unwinds: `track.add`, `track.remove`, `track.move`, `plugin.load`,
`plugin.unload` (`ProjectJournal::addJournalStructure()`; `ControlUndoStructuralTranscript`,
`ReversibilityUndoTest`).

**Bounds.** A comp does not sound different from the track's clips in this release (the composite is a
view with no separate playback path — `TakeLaneCompTest` proves the byte identity). The phase-locked
edit set propagates exactly one media edit kind (as the release notes' stated limit records). Rack
zones store key/velocity ranges but do not route yet (§3.5). Clip fades and clip gain apply to audio
only.

### 3.3 Recording and input

**An arbitrary input count, drivable end to end.** `record.input_set` writes the **channel** count (the
ALSA backend gained a capture path for this; `src/core/audio/AudioAlsa.cpp`), `record.input_get_state`
reads it, `track.set_arm` arms a real capture, and the recorder itself is drivable: `record.get_state`,
`record.arm_track`, `record.disarm_track`, `record.disarm_all`, plus routing verbs
`src/core/ControlCommandsRecordingRoutes.cpp` (`RecordingInputPathTest`, `MultiTrackRecorderTest`,
`RecordRingBufferTest`, `RecordingRealtimeTest`, `TwoTrackRecordingHarness`).

**Punch in/out.** `transport.punch_set` sets the tick range `[start, end)` the transport records
inside; `transport.punch_get_state`, `transport.punch_clear` (`ControlPunchTranscript`).

**Retrospective capture.** MIDI: `midi.retro_capture_arm`, `midi.retro_capture_status`,
`midi.retro_capture_to_clip` — a rolling window of what you just played, **8192 events, the most
recent, per open MIDI client** (not project state), recoverable after the fact (`ControlRetroCapture`,
`MidiRetroCaptureTest`). Audio: `record.retro_capture_arm`, `record.retro_capture_status`,
`record.retro_capture_to_take` (`RetroAudioCaptureTest`).

**Recording crash recovery.** A capture journals itself (`record.journal_begin`, `record.journal_update`,
`record.journal_finish`) and a later start can recover it (`record.recovery_get_state`,
`record.recovery_restore`, `record.recovery_discard`). **Bound, stated:** guaranteed recoverable is
`min(frames the journal recorded, frames actually written)` — a crash between the two loses the
difference (`ControlRecordingRecovery`, `docs/MIDI-RETRO-CAPTURE-BOUNDS.md`).

**Controller surfaces.** `controller.surface_state`, `controller.soft_takeover`, `controller.feedback`,
plus mapping templates `controller.template_list`, `controller.template_save`, `controller.template_apply`,
`controller.template_delete` — soft-takeover, LED/feedback output and reusable mappings
(`ControllerSurfaceTest`).

**MIDI auto-reconnection.** `midi.reconnect_arm`, `midi.reconnect_status`, `midi.reconnect_set`,
`midi.clients_list`, `midi.device_list`, `midi.learn_toggle` — assignments are remembered by identity
(client name + port name), not by index, and a replugged controller is re-bound (`MidiReconnectTest`,
`MidiLearnTest`, `MidiLearnThreadTest`, `MidiLearnGuiTest`).

**Clock and sync.** `clock.get_state`, `clock.master_set`, `clock.slave_set`: MIDI clock master (24
pulses per quarter note) and slave, with MTC, plus a bounded monitor (`MidiClockTest`). Session sync:
`link.get_state`, `link.set_enabled`, `link.set_quantum`, `link.set_session_tempo`,
`link.set_start_stop_sync` — two instances on one machine (or one network segment) agree on tempo and
beat; the shared beat is continuous across a tempo change (the timeline is re-anchored before the new
tempo takes effect); `ControlLinkCommandsTest`.

**Bounds.** `start_stop_sync` is announced and reported but never acted on; the play head is not
slaved. The record path's real-interface half is hardware-bound (no audio interface was exercised on
this box). The controller surface has **no hardware attached** in the proof, and OSC is out.

### 3.4 Session view

**What you can do.** The Session View is a real data layer plus a launch scheduler wired into
`Song::processNextBuffer`, drivable through **17 `session.*` ids**: `session.get_state`,
`session.set_grid`, `session.set_quantisation`, `session.set_scene`, `session.set_slot`,
`session.clear`, `session.clear_slot`, `session.launch_slot`, `session.launch_scene`, `session.stop_slot`,
`session.stop_all`, `session.follow_set`, `session.follow_get_state`, `session.arrangement_record_arm`,
`session.arrangement_record_land`, `session.arrangement_record_status`, `session.back_to_arrangement`.
**Follow Actions run as a chain** (evaluated where the launch state lives, delivered on the existing
lock-free queue) and **Arrangement Record** captures a performance's own events
(`SessionFollowTest`, `SessionArrangementRecordTest`, `SessionSchedulerTest`, `SessionModelTest`,
`tests/control-session-m1.py` — milestone M1: a saved project launches 4 clips across 2 scenes in sync).

**Bounds.** Says the release's own honesty row in `tests/advertised-features.tsv`: there is **no clip
launcher grid**, no scene launcher UI, and **a launched session slot does not render audio** — this tree
has no session-clip playback path. A user cannot see or hear the Session View; an agent can drive all
of it over the socket. The option defaults ON since 0.3.0 and the honesty gate requires it ON.

### 3.5 Mixing and routing

**Mixer.** `mixer.get_state`, `mixer.add_channel`, `mixer.remove_channel` (master `ch-0` is refused),
`mixer.set_volume` (0..2), `mixer.set_pan`, and the routing verbs `mixer.route_to`, `mixer.route_remove`,
`mixer.send_to`, `mixer.sidechain_to` — written at unity, sidechain included (`MixerConcurrencyTest`,
`MixerAbRegressionTest`, `PhaseDSidechainTest`). Channels are addressed by **stable `ch-<n>` ids**
persisted beside the positional `num` (slice 2, §3.12).

**Buses, ports, routing graph.** `bus.create`, `bus.list`, `bus.remove` (parallel buses with the
engine's own semantics); `port.get_state`, `port.set_pin` (a device's `AudioPortsModel` pin matrix);
`routing.get_state` (the chain graph as an inspector); `patcher.get_state`, `patcher.set_wiring` — the
patcher edits the routing graph's wiring while the node SET stays derived
(`RoutingGraphTest`, `RoutingGraphLiveTest`, `PatcherCommandsTest`).

**Latency compensation.** `pdc.report` answers the whole PDC picture in one call — per-path latency and
the compensation in force (`PdcMixerTest`, `PhaseFChannelScaleTest`). **Bound:** `pdc.report` publishes
0 for every latency unless a device reports one; plugin/instrument latency is not reported yet.

**Racks, macros, zones.** `rack.add_chain`, `rack.remove_chain`, `rack.get_state`, `rack.set_selected`,
`rack.macro_add`, `rack.macro_remove`, `rack.macro_set`, `rack.macro_target_add`,
`rack.macro_target_remove`, `rack.zone_add`, `rack.zone_remove`, `rack.zone_resolve` — a rack is
parallel chains plus a chain selector, macros are named persisted scalars (0..1) driving existing
parameters, zones store a key range and a velocity range (`RackTest`, `RackMacrosTest`, `RackZonesTest`).
**Bound:** the zone half does not route yet — the rack renders one stereo block, stated rather than
implied; no rack UI and no scripting path; chain switching is not crossfaded.

### 3.6 Automation and modulation

**Modes.** `automation.get_state` lists every automatable device parameter per track with its mode;
`automation.mode_set` sets off / read / touch / latch / write, and `automation.record_mode_set` sets the
record mode — both made drivable in 0.3.0 (`AutomationModesTest`, `ControlAutomationModesTest`,
`ControlAutomationScriptTest`). **Curves.** `automation.add_point`, `automation.remove_point`,
`automation.clear` (with the mode and the point list readable back). **Sample-accurate ramps.**
`automation.ramp_set` / `automation.ramp_get`: a per-block, per-sample ramp built from knots at tick
boundaries; opt-in per clip; realtime-safe (no allocation on the path; the ramp is the caller's own
fixed-capacity array) (`SampleAccurateAutomationTest` — real periods rendered through
`Song::processNextBuffer()`). **Modulators.** `modulator.create`, `modulator.remove`,
`modulator.get_state`, `modulator.rate_set`, `modulator.target_set`, `modulator.depth_set`,
`modulator.target_remove` — a timeline-locked LFO (shape, rate Hz, phase, depth as a fraction of the
target's own range) driving a set of parameters, applied once per audio block
(`ModulationLayerTest`, `ModulationLayerValueTest`, `ModulationLayerProjectRoundTripTest`).

**Bounds.** Modulation is applied **once per audio block** (~11 ms at the default block size), not per
sample. Sample-accurate automation cannot hold where a parameter's own resolution is coarser than a
sample (the limitation line is in `docs/KNOWN-LIMITATIONS.md` §Sample-accurate automation). Nothing in
the interface selects either — the socket is the only route.

### 3.7 Plugin hosting and DSP

**What you can load.** Built-in effects and instruments, plus vendor families: VST3 **effects and
instruments** (`plugins/Vst3Instrument/`, `Plugin::Type::Instrument`; one instrument per track,
MIDI in to audio out), CLAP **effects and instruments** (the CLAP instrument path added note ports,
input events and a generator's audio layout), LV2, LADSPA, VST2/Vestige, SF2 and the inherited LMMS
synth set (`plugin.list`, `plugin.load`, `plugin.unload`, `plugin.bypass`,
`plugin.param_get`/`plugin.param_set`, presets `plugin.preset_list`/`_load`/`_save`, state
`plugin.state_load`/`plugin.state_save`; `Vst3InstrumentTest`, `Vst3InstrumentIntegrationTest`,
`ClapEffectIntegrationTest`, `ClapLoaderErrorTest`, `ControlDeviceCatalogueTest`). Instances are
addressed by **stable `fx-<n>` ids** persisted in the project (slice 2, §3.12).

**Hosting contract.** Plugin hosts process **exactly the frames they are asked for, in chunks**
(`plugin.host_chunking`; `ControlChainPresetTest`, `HostChunking` rows, CODE-4), and the note path is
explicit (`plugin.host_notes`). Every CLAP load failure is **typed** — eleven distinct failure codes
from `plugins/ClapEffect/ClapLoader.h` (`ClapLoaderErrorTest` drives four modules built to fail in
exactly one way each).

**Scanning, cache, quarantine.** `plugin.rescan`, `plugin.scan_cache_get_state`, `plugin.scan_cache_list`,
`plugin.scan_cache_lookup`, `plugin.scan_cache_quarantine_add`, `plugin.scan_cache_quarantine_remove` —
a JSON cache with a quarantine list that no longer needs a hand-edited entry (`PluginScanCacheTest`,
`ControlPluginScanCommands`). **Out-of-process:** `oop.get_state`, `oop.list_families`, `oop.set_mode`,
`oop.restart`, `oop.reset_crashes` — a family table over this build's plugin families, a client-death
record, and a **typed refusal after 3 deaths in one session** (`OutOfProcessHostTest`).

**WASM DSP sandbox.** `wasm.list`, `wasm.get_state`, `wasm.load`, `wasm.unload`, `wasm.set_param`,
`wasm.process`, plus the shared pool and the deterministic offline render `wasm.pool`,
`wasm.render_offline` — present only in a build with the wasmtime C API (`#ifdef LMMS_HAVE_WASM`), which
**no CI job provisions**: the release binaries have `WANT_WASM=OFF` (the honesty row requires OFF).
**Bound:** the group drives the host's sandbox; it does not put a module into an effect's audio path.
**DSP readback and metering:** `dsp.get_state` (every device chain with its `fx-<n>` instances),
`meter.get_state` (live master: gated integrated LUFS, short-term max, true peak),
`meter.measure_file` (any rendered file), `meter.arm`, `export.set_loudness_report`
(`LufsMeterTest`, `LoudnessReportTest`, `MeterTapTest`, `ControlMeterCommands`). **Auto-mastering wave
1:** `mastering.run` renders once and measures five candidates against a named loudness target, with
`mastering.get_state`/`mastering.list_candidates` — it does **not rank** them (`MasteringTest`,
`ControlMasteringCommands`).

**Bounds.** One instrument per track; no plugin editor (the generated knob grid is what opens); no
multi-out, no instrument presets, no instrument PDC for the VST3 instrument; no out-of-process
isolation for the new families (VST3/CLAP isolation is an unstarted follow-up); no third-party CLAP
plugin has been loaded on the Windows side; the WASM sandbox is absent from the release binaries.

### 3.8 Rendering and export

**Render.** `render.render` renders the current session to a file and returns its hash — and it takes a
**time range**: `start_ticks` and `end_ticks`, both required together, with the range's validity
enforced (`RenderJobQueueTest`, `ControlRenderCommands`). `render.stems` exports every unmuted track to
its own file in an absolute directory, one stem per track (`StemExportTest`, `StemJobManagerTest`,
`StemModelStoreTest`, `StemSplitPipelineTest`, `ControlStemExportVerb`).

**Export settings.** `export.get_settings` exposes the render/export settings including
`loudness_report`; `export.set_dither` selects TPDF (triangular-PDF) dither, **off by default**;
`export.set_src_quality` selects the sample-rate converter's quality; `export.set_loudness_report`
toggles the loudness sidecar. **Presets:** `export.preset_add`, `export.preset_list`,
`export.preset_apply`, `export.preset_remove` — a preset carries the three settings
(dither, SRC quality, loudness report) (`ExportDitherTest`, `ExportWavDitherTest`, `ControlExportSettings`,
`ControlRenderPresets`).

**Freeze and bounce.** `bounce.in_place` renders a track's output to audio; `freeze.track` makes the
track play the render **instead of its clips**; `freeze.region` freezes one tick range; `freeze.unfreeze`
restores. The frozen state is project state and survives save/load; a frozen track's own `length()`
accounts for its take (`ControlFreezeCommandsTranscript`).

**Determinism and the golden programme.** Export renders single-threaded (live playback still uses the
pool); 7 of the 9 projects of the render-determinism sweep are bit-reproducible, and the two that are
not have the cause inside their instruments (`tools/render-determinism-probe.sh`,
`RenderJobQueueTest`). The **golden-audio integration programme** (board task #679) measured **0 LSB
over 10 pairs** for all three headline paths of a socket-built fixture, and 13 275 LSB (40.5 % of full
scale) with 96.7 % of frames differing on the bundled project; its sensitivity bound — measured by
sweeping the fader — is a −0.001 dB change (`docs/GOLDEN-AUDIO.md`, `tests/control-golden-audio.py`,
`tests/golden_audio_lib.py`).

**Bounds.** A preset carries three settings and no path or format. The determinism claim is "for the
sweep's projects", not for arbitrary plugin sets. The golden programme is a test, not a surface: it
registers no command group.

### 3.9 Interchange

**DAWproject in and out.** `dawproject.export`, `dawproject.import`, `dawproject.read`,
`dawproject.convention` — the session leaves as a DAWproject container another DAW reads, and comes
back; the document's ids are the model's own ids, and the mixer model is written as separate strips
(`DawProjectInterchangeRoundTripTest` — the round trip is the centrepiece). **Bound:** eleven losses are
recorded and counted, each named in the release notes (audio clips and their media, automation and
more).

**Standard MIDI File conductor interchange.** `interchange.smf_export`, `interchange.smf_import`,
`interchange.smf_read`, `interchange.smf_convention` — the tempo map leaves as an SMF and returns; the
tick-0 rule and the PPQ relationship are published by `interchange.smf_convention`; a foreign division
(96, 960, 1000 ppq …) is scaled onto LMMS ticks on read (`SmfInterchangeTest`,
`SmfInterchangeRoundTripTest`). **Bound:** events are **steps** — no ramps.

**Import detection.** `detect.analyze` reads one audio file through the engine's own decoder
(`SampleDecoder`) and reports tempo, first transient and key; `detect.apply` writes them into the
project's own fields (one tempo-only event at tick 0, the key field); `detect.get_state` reads them
back. Method names are published: tempo `spectral-flux-autocorrelation`, key via the pre-existing scale
vocabulary (`ImportDetectionTest`, `tests/control-detect-commands.py`, `tools/import-detection-proof.cpp`).
**Bound, stated:** what is measured is synthesised input, not a corpus of real recordings
(`docs/IMPORT-DETECTION.md` §4.1).

**Version control.** mmpz-git: `project.merge`, `project.diff`, `project.conflicts`,
`project.audible_diff` — three-way merge of concurrent track edits with a musical conflict
presentation (`track "Bass" > pattern "I" > note F#1 at bar 1 beat 1`), large assets summarised by hash
with a `.mmpz-git-conflicts.json` sidecar beside the project, and an audible-diff CLI
(`MmpzGitDepthTest`, `tools/mmpz-git/depth-demo.sh`). **Bound:** tooling, not in the binary's GUI; the
merge driver is a git integration, not a command.

### 3.10 Browser and content

`browser.query` searches files by what they ARE (tags/metadata), not only by where they sit;
`browser.tags`, `browser.tag.add`, `browser.tag.remove` read and edit the tag store — written to the
user's config directory, **not** into the project file; `browser.peaks` answers a file's min/max
waveform peaks from a cache (the waveform peak cache); `browser.roots` lists what is scanned
(`BrowserCatalogTest`, `tests/src/core/BrowserCatalogTest.cpp`). **Bound:** tag edits are a real
inverse in A16 terms, but the store is per user, not per project, so tags do not travel with a project.

### 3.11 Scripting (Lua)

`script.list` (the scripts this build ships under `data/scripts`), `script.run` (run a Lua script in
the running instance; `docs/specs/SPEC-lua-api-v0.md`), `script.set_memory_budget`. The DAW-control
half of the API is real: `zene.mixer()` with `Mixer`/`MixerChannel`/`EffectChain`/`Effect` classes —
one implementation, two surfaces, so a script addresses the same channel the control surface does; a
gain/mute/solo write queues the channel model's own journal checkpoint (so it is undoable through the
same stack); writes are queued and never applied on the script's thread
(`ScriptDawBindingTest`, `ScriptBindingsTest`, `ScriptEngineTest`, `ScriptStabilisationTest`,
`ScriptMemoryBudgetTest`). The API version is **0.2.0** and the compatibility policy is **enforced,
not described**: a script that declares `--! zene-api 0.2` cannot run on a build that predates the
additive change; every 0.1 script keeps running (`ScriptApiVersion.cpp`,
`docs/LUA-COMPATIBILITY-POLICY.md`). **Bounds:** no console pane, no script editor and no GUI control
that runs a script — the console is an output path only; channel pan, and the other deliberately
withheld bindings, are listed one line each in `docs/LUA-API-STABILISATION.md` §5.

### 3.12 The agent surface

**The socket.** Opt-in `--control-socket <path>` (§2.2) speaks line-delimited JSON-RPC, proto 1. The
registry is introspectable from the surface itself: `control.commands_list` (every id with its group,
description, `requires` declaration, `mutating` flag, and its **argument and result JSON schemas**),
`control.surface_report` (the live menu/toolbar surface — every user-visible action and whether a
command id reaches it, SPEC A15), `control.id_contract` (the stable-id rules and counts),
`control.ping`, `control.version`, `app.version`. Errors are typed and carry a message; a refusal names
its fallback where one exists.

**The A16 reversibility contract.** Every command ships with a reversibility class and reason as data in
`src/core/ControlReversibilityTable*.cpp`; the classes are `true_inverse`, `snapshot`, `irreversible`,
`not_mutating` (§1 headliners for the counts). `control.undo` undoes the last agent command — by
dispatching the recorded inverse when the inverse is a command, else by unwinding the engine's own
`ProjectJournal` (the same stack Ctrl+Z drives) — and **refuses, typed** when the last command has no
inverse, naming the command, its class and the documented fallback, rather than silently undoing an
older command. `control.redo` redoes the last undone checkpoint. `control.transactions` reports the
recorded transactions for mutating commands — each with its class and serialised size — plus the bounds
they are kept within (`cap_records`, `cap_bytes`, `retained_bytes`, `evicted`, `capped`).

**Bounded, coalescing undo.** `control.undo_depth`, `control.set_undo_depth`,
`control.set_undo_coalescing` — a step cap (100 by default, settable up to 10 000), a byte budget over
serialised checkpoints (16 MiB by default, settable up to 512 MiB), eviction oldest-first with the
newest step never dropped and every eviction counted and reported, and a **400 ms coalescing window**
(a drag is one undo step, not one per call; `window_ms=0` restores one-step-per-call). `control.undo`
refuses, typed, when a bound has evicted the step it would unwind (`UndoBoundsTest`,
`ReversibilityContractTest`, `docs/UNDO-BOUNDS.md`).

**Stable ids.** Document objects carry ids that persist through save/open and survive insert, delete,
split, reorder and undo: `trk-<n>` (slice 1), and `clip-<n>`, `note-<n>`, `ch-<n>`, `fx-<n>` (slice 2)
written as an `id` attribute on the object's own element; `dev-<n>` is a catalogue selector, not a
project object, and is deliberately not written into the project. A copy is a new object with a new id
(`tests/control-stable-ids-slice2.py`, `StableTrackIdsTest`, `control.id_contract`).

**The MCP bridge.** `tools/mcp-zene-control` generates **one MCP tool per command id** it reads from a
live instance — so a new command group is automatically an MCP tool — and serves the offline copies
next to the bridge when no instance answers; the committed snapshot is the offline list, and the
registered `ControlCommandsSnapshot` check compares the snapshot against the binary's live registry in
both directions, asserts the bridge exposes every id (live, offline, and with a stale cache planted),
and makes an offline copy's staleness **detectable instead of silent** (`tests/control-commands-snapshot.py`,
`ControlMcpGroupCoverage`). **Bound:** with no instance running the tool list is a copy, and the bridge
can only say so; re-pointing it is a deployment act.

**The Windows transport.** On Windows the same contract listens on a named pipe
(`PIPE_REJECT_REMOTE_CLIENTS`, local-only by construction, one thread per connection so a blocking pipe
call never stalls the accept loop) — `src/core/ControlServerWin32.cpp`; proof `ControlNamedPipeSmoke`,
registered and CI-only (§2.2).
---

## 4. Verification — how the claims above are proven

**The rule.** Every capability in §3 is in this release only because all four parts of the scope
contract hold it there (§1). The four-part contract is also the verification rule: an engine with no
command group, a command group with no proof, or a proof that is not registered, is not a claim this
document may make.

**4.1 The registered proofs.** The suite this build registers holds **214 tests** (`ctest -N`, universe
`zene-030/build/tests`). Three classes do the proving:

- **Engine QTests** under `tests/src/**`, registered in `tests/CMakeLists.txt` — the arithmetic and the
  persistence (`TempoMapTest`, `TakeLaneCompTest`, `NoteRandomTest`, `WarpMarkersTest`,
  `SampleAccurateAutomationTest`, …). A test source that is registered nowhere fails
  `tests/unregistered-tests-gate.sh`, so "no test" cannot hide as "test not run".
- **Control-surface transcript tests** under `tests/control-*.py`, registered as ctests and run against
  the **real binary** through `tests/control_socket_harness.py`: one instance, `--control-socket`, offscreen,
  bounded waits (a hang is a failure, never a wait), `control.quit` shutdown, explicit-PID reaping.
  These are the proofs for every "drivable through the socket" claim in §3 — e.g. `ControlPunchTranscript`,
  `ControlRecordingRecovery`, `ControlUndoStructuralTranscript`, `ControlMeterCommands`,
  `ControlRetroCapture`, `ControlNamedPipeSmoke`, `ControlCommandsSnapshot`.
- **Suite-level checks** registered beside them: the A16 contract, the honesty guard, the agent-surface
  gate, the drift ratchets (below).

**4.2 The seven-platform CI.** `.github/workflows/build.yml` — six job definitions, seven platform
builds, a `release-gate` job that requires the matrix green and that "this commit's own push runs" are
green. **Five platform builds run the registered tests** (linux-x86_64, linux-arm64, macos-x86_64,
macos-arm64, msvc-x64); **mingw64 and windows-arm64 build and package only**, which is itself stated in
the workflow (`mingw64`'s step comment: “this job builds a Windows target and runs no tests”). Every
package job runs the release-honesty guard and the release version guard before packaging. Runs with
recorded verdicts in this tree: **#6** (four reds fixed: namespace allowlist + Gate 13, a Qt5 include
leak, an MSVC empty-boundary skip, a telemetry timing bound), **#7** (whose MSVC red produced the
root-cause fix: POSIX `shlex` was eating Windows object paths), and the macOS evidence runs
`35126160372` and `35212797698`. **Run #9 — the current tip's run — was in flight while this document
was written**; its verdict is deliberately not part of this document, and the two sibling lanes working
this release (`030/wvbump` reading the run, `030/wdocs`) are separate from the measurement here.

**4.3 The A16 contract ratchet.** The contract table is data (`src/core/ControlReversibilityTable*.cpp`),
and the registered `ReversibilityContractTest` **reads the histogram block in
`docs/RELEASE-NOTES-v0.3.0-alpha.md`** and compares it against a histogram computed from the live table
on every run — so a row added, removed or re-classified anywhere fails the ctest until the published
figure is re-taken with `bash tools/dawproject-proof.sh` (part 2 is the probe; its own output line is
the figure: `MEASURED rows=340 true_inverse=158 snapshot=32 irreversible=13 not_mutating=137`, with
`DECLARED rows=340 entries=340 duplicates=0` on the same run). Two properties come with the derivation:
**a command declared twice fails by name** (the four raw literal blocks are compared against the keyed
map — it found and retired 24 cross-file duplicates in `ControlReversibilityTableLive.cpp`), and
**every row is in exactly one class** (the four counts must sum to the row total).

**4.4 Honesty and documentation gates.** A published claim must be true of the binary the release
ships, and that is mechanised:

- `tests/release-honesty-gate.sh` reads `tests/advertised-features.tsv` — six rows (VST3 effects, VST3
  instrument, CLAP effects, Session View, the WASM sandbox, stem separation), each bound to a `WANT_*`
  option the binary reports in its own build-options dump and, where applicable, a plugin module in the
  build tree — and fails a build whose dump contradicts the documented claim, in either direction
  (a documented-absent feature turned ON fails too).
- `tests/release-version-gate.sh` checks the version, the documents and the tag agree; it runs in every
  package job. The version-bump lane added a self-test that derives its injection point (card 690).
- The **agent-surface gate** (SPEC A15) compares every user-visible menu/toolbar action against the
  command ids and ratchets the gap through `tests/agent-surface-baseline.txt` (one way only: a line is
  deleted when its action gets an id). The tree's own build records the latest run in
  `build/tests/agent-surface-report.json` (written 2026-09-17): **340 commands swept, 48 actions
  reflected, 42 grandfathered baseline entries, 0 stale, 0 problems.**
- **Drift ratchets** on the fork sources (`tests/{complexity,file-length,duplication}-gate.sh` with
  their per-scope baselines and `--reanchor` requiring a written reason), a real coverage entry floor
  (`tests/coverage-gate.sh`, 50 % for new files, with a named exemption home), and **Gate 9**
  (`tests/fork-sources-gate.sh`), which requires every tracked source to be in a scope manifest AND runs
  the manifests' own regeneration recipes (`tests/all-sources-reproduce.sh`) so a manifest cannot drift
  from its own recipe. This lane's changes pass all of the above; exit codes in Appendix B.

**4.5 The crash-testing programme's first tooling.** The crash surface is drivable and observable:
`crash.list_reports`, `crash.enable`/`crash.disable`, `crash.acknowledge_report`,
`crash.discard_report`, `crash.upload_report`, over the offline local crash reporter
(`src/core/CrashReporter.cpp`, `CrashReporterWindows.cpp`) with `ControlCrashReporter` and
`CrashReporterArmTest` as its registered proofs. The socket harness itself carries the frozen-instance
diagnosis that makes a hang a *named* failure — liveness, the kernel's wait channel, a debugger
backtrace (`tests/control_instance_diagnosis.py`) — and the safety-net harness records the arm/pair
matrix. Two further programmes are in this tree and registered: the **golden-audio integration
programme** (`tests/control-golden-audio.py`, `tests/golden_audio_lib.py`; headline: 0 LSB over 10
pairs) and the **RT-safety sweep** (`tests/rt-safety-sweep.py`). **Bound:** there is **no measured
crash-free rate** — the reporter is new, so the number does not exist yet, and that is the recorded
statement rather than a placeholder (`docs/KNOWN-LIMITATIONS.md` §Where the quality bars are not met yet).

---

## 5. Limits and exclusions

This section condenses `docs/KNOWN-LIMITATIONS.md` — the honest bounds page, which remains the
authority — and `docs/FEATURE-LIST-0.3.0.md` §Out of scope. Nothing here contradicts that page; where a
detail matters, follow the pointer.

**5.1 Before you install.** This is an alpha; parts are unfinished and crashes are possible; keep
backups and expect that a file saved here may not open in a later build, an older build, or in LMMS.
The builds are **unsigned** (SmartScreen / Gatekeeper will warn). Packages come from the release page
and nowhere else, and only for platforms whose build job is green. Every save writes a `.bak` beside
your project.

**5.2 The interface is deliberately minimal — the release's own promise.** Everything in §3 that is not
listed here has **no interface**: the Session View grid and clip launcher, racks and their macros/zones,
VCA/edit groups, folder tracks and visibility sets, clip fade/crossfade/gain gestures, take lanes and
comping, the modulation layer, the modulation editor, the undo-history panel and its caps, the plugin
scan-cache/quarantine UI (no user-facing scanning interface worth the name), automation modes, the chord
track, scale-aware root highlighting (deferred to the interface phase), the revision timeline, stable-id
inspection, the WASM sandbox, stem export controls, and the browser upgrade. The socket and the MCP
bridge are the product surface in this release.

**5.3 Whole features that are absent (deliberate exclusions, not gaps).** Notation/score,
surround/immersive, expression maps/articulation switching, EuCon/HUI, video/timecode/DNx,
AAF/OMF/MXF, cloud collaboration, AAX and AU, Avid marketplaces; modern stock devices, factory content
and a design system; auto-mastering waves 2–3 (the reference-matching arm and the learned ranker); the
patcher GUI. Absent for this release but not excluded: input monitoring; a measured crash-free rate;
sample accuracy outside the opt-in per-clip ramps.

**5.4 Where the build is real but narrower than it sounds.**
- **VST3 / CLAP instruments:** one instrument per track, MIDI in to audio out; the plugin's own editor
  does not open (a generated knob grid is what opens); no multi-out, no presets, no instrument latency
  in PDC; no out-of-process isolation for the new families; CLAP instruments are new and have no
  third-party witness.
- **Session view:** no clip launcher grid, and **a launched session slot does not render audio** — a
  user cannot see or hear it; an agent can drive all of it (`tests/advertised-features.tsv`,
  `session-view` row's own text).
- **WASM sandbox shipping status:** the release binaries have `WANT_WASM=OFF` because no CI job
  provisions the wasmtime C API; the `wasm.*` group exists and is drivable only in a build that has it
  (this document's tree build does). The honesty row requires the absence and stays true until the C API
  is provisioned in CI.
- **Windows:** mingw64 and windows-arm64 are **build-only** (no ctest), so the Windows halves of CLAP
  loading and the named-pipe control transport are **CI-only evidence** — no MinGW or MSVC toolchain
  exists on the box this document was measured on, and no Windows binary was run here.
- **Racks/zones:** the zone half stores key/velocity ranges but does not route; chain switching is not
  crossfaded. **Comping:** the composite is a view with no separate playback path. **VCA edit groups:**
  exactly one media edit kind is propagated by the phase lock.
- **Undo bounds** (count cap, byte budget) are session state, not project state; neither survives a
  restart. **Coalescing** covers the five declared commands only.
- **Automation** outside the opt-in ramps is evaluated once per tick; **modulation** is applied once per
  audio block (~11 ms at the default block size).

**5.5 Quality bars not met yet (recorded, not exempted).** Renders are reproducible for 7 of the 9
projects of the determinism sweep; `Root84` and `StrictProduction` are not, with the cause inside their
instruments. Measured test coverage at the last capture: **87.21 %** of the instrumented lines
(13 770/15 790 over the 165 fork-scope files that produced a record), and the coverage gate **fails** on
that build with **15 files below its 50 % entry floor** — recorded in the limitations page with the
plan, deliberately not exempted at the tag. No hardware was attached for the controller, MIDI
hotplug, clock, audio-interface or stem-separation proofs; those halves are measured in software or
not at all, as each section of the limitations page records.

**5.6 Telemetry and privacy.** Telemetry is opt-in, off by default, previewed byte-for-byte before
consent, built from a **closed allowlist of 24 fields** and cannot carry a project name, path, plugin
name, email, IP address or installation ID; `ZENE_TELEMETRY=OFF` compiles the client out and drops the
registry by exactly `telemetry.consent` and `telemetry.status` (the A16 histogram's telemetry delta of 2
rows). `docs/TELEMETRY-V1.md`, `docs/KNOWN-LIMITATIONS.md` §Telemetry and privacy.

---

## Appendix A — the full command catalogue

> **Status: not generated in this draft.** The catalogue is produced by
> `scripts/capabilities-dump.py` (committed on this branch, registered in `tests/fork-sources.txt`),
> which boots ONE instance of the built binary through `tests/control_socket_harness.py` and prints, for
> every command id in the live registry, its group, whether it mutates and its one-line description —
> plus a provenance block (binary, sha256, `control.version`, the `control.transactions` caps). The
> generation run was **denied by the approval gate in the writing session**, so this section is a
> labelled gap rather than a copy: regenerate it with the command in Appendix B, §B.4, and replace this
> block with the script's output. Nothing else in this document depends on the missing block: §3's ids
> were read from the registry sources at this commit and the headline counts from the ratcheted A16
> table.

## Appendix B — measured matrix, and how to re-run every measurement

### B.1 The platform and test matrix (measured from `.github/workflows/build.yml` + `ctest -N`)

| job (workflow) | runner | platform builds | runs the 214 registered tests | package |
| --- | --- | --- | --- | --- |
| `linux-x86_64` | ubuntu-22.04 | linux x86_64 | **yes** (`ctest -j2`) | AppImage |
| `linux-arm64` | ubuntu-24.04-arm | linux aarch64 | **yes** (`ctest -j2`) | AppImage |
| `macos` (2-arch matrix) | macos-15-intel, macos-15 | macOS x86_64 + arm64 | **yes** (`ctest -j3`) | `.dmg` |
| `mingw64` | ubuntu-latest | windows x64 (cross) | no (build-only) | `.exe` (NSIS) |
| `msvc-x64` | windows-2022 | windows x64 (native) | **yes** | `.exe` (NSIS) |
| `windows-arm64` | windows-11-arm | windows arm64 | no (build-only) | `.exe` (NSIS) |
| `release-gate` | ubuntu-latest | — | requires the matrix green + its own push runs green | — |

Per-job options of record: `-DWANT_VST3=ON -DWANT_CLAP=ON` on every platform job;
`WANT_VST3_TEST_INSTRUMENT=ON` on linux-x86_64 only; `-DWANT_QT6=ON` on msvc-x64 only;
`WANT_SESSION_VIEW` defaults ON; no job provisions the wasmtime C API (`WANT_WASM` degrades to OFF in
CI), no job passes `WANT_STEM_SPLIT`.

### B.2 The numbers quoted in this document, and the command that measures each

| claim | value | command (run from the worktree root unless stated) |
| --- | --- | --- |
| binary under test | sha256 `ab3f9b32…cb6fb92` | `sha256sum ../build/zene` |
| its version string | `0.2.1-alpha.626+b89d10a` | `LD_LIBRARY_PATH=../third_party/wasmtime/lib ../build/zene --version` |
| registered tests | 214 | `ctest -N` in `../build/tests` |
| A16 histogram (340 / 158 / 32 / 13 / 137) | ratcheted by ctest | `bash tools/dawproject-proof.sh` (part 2 prints `MEASURED rows=…`) |
| command ids / groups | 340 ids, 53 id prefixes (this config); 332 in release builds | A16 table + Appendix A's live dump |
| A16 rows by id prefix | 54 declared prefixes (53 with `stem.*` compiled out, 52 in release builds) | `grep -rhoE '(RC\|R)\("[^"]+"' src/core/ControlReversibilityTable*.cpp \| sed 's/.*("//; s/"$//' \| cut -d. -f1 \| sort -u` |
| agent-surface gate | 340 swept, 48 reflected, 42 baselined, 0 problems | `python3 tests/agent-surface-gate.py ../build/zene`; latest report `../build/tests/agent-surface-report.json` |
| fork-sources recipe | REPRODUCES | run the `# Verify it` block in `tests/fork-sources.txt` (exit 0) |
| scope manifests | 663 fork-NEW, 1104 whole-tree, 40 tooling, 0 stale | `bash tests/fork-sources-gate.sh` (exit 0) |
| upstream-divergence ledger | 422 changed paths declared | `bash tests/no-upstream-regression-gate.sh` (exit 0) |
| whole-tree manifest reproduces | REPRODUCES | `bash tests/all-sources-reproduce.sh` (exit 0) |
| file-length ratchet (fork) | PASS | `bash tests/file-length-gate.sh --scope fork` (exit 0) |
| complexity ratchet (fork) | PASS — 6166 functions, 72 over CCN 10, all grandfathered | `bash tests/complexity-gate.sh --scope fork` (exit 0) |
| duplication ratchet | PASS — 0.52 % (budget 5 %) | `bash tests/duplication-gate.sh` (exit 0) |

Note on the two ratchet runs: each refreshes its own baseline file when the tree has improved; this
branch left those refreshes out of its commits deliberately (they are maintenance, not this lane's
change), so re-running them here leaves `tests/{complexity,file-length}-baseline.tsv` modified in the
working tree.

### B.3 The measurement hygiene this document obeyed

One instance at a time, ≤2 in total; the instance is started through `tests/control_socket_harness.py`
(offscreen, scratch `HOME`/`XDG_*`), closed with `control.quit`, and reaped by explicit PID — never a
pattern kill; no other lane's worktree was touched.

### B.4 Appendix A: regenerate the catalogue

```bash
# one instance, through the tree's harness; prints the full markdown catalogue to stdout
python3 scripts/capabilities-dump.py ../build/zene > /tmp/capabilities-appendix-a.md
# the documented environment the dump uses (the harness derives the same path itself):
export LD_LIBRARY_PATH=$PWD/../third_party/wasmtime/lib
```

The script's provenance block records the binary path, its sha256, `control.version`, the surface count
and the `control.transactions` caps; its body is one section per group with a
`| command | mutating | description |` table per group — generated, never hand-typed, so Appendix A
cannot drift from the binary the release ships.
