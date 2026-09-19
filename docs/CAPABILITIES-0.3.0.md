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
pointers rather than repeating it. **Independently verified 2026-09-19** — an independent pass re-measured
every headline number against the same binary (all match) and found six errata, each applied here; the A16
one-row residual is settled (`chord.set`). Record: `docs/CAPABILITIES-0.3.0-VERIFICATION.txt`. Appendix A was captured live
from the merged tip's binary (`d408e35`, `0.2.1-alpha.643+d408e35`, surface 340 ids / 53 groups) on
2026-09-19; its own provenance block is inside the catalogue.

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
  51 declared `cmd.group` literals in the command TUs — the 52nd, `wasm`, is declared in
  `src/core/ControlWasmSupport.cpp` — plus the two groups (`chord`, `modulator`) whose group name is set
  through their own constant (54 declared prefixes in all). The authoritative per-id list is Appendix A (generated from a live
  instance).

**What is not in 0.3.0, in one line each** (detail in §5): no interface for almost any of it; no clip
launcher grid; no audio through a launched Session slot; the WASM sandbox absent from the release
binaries; the Windows named-pipe half proven by CI only; no hardware MIDI/audio device exercised on
this box.

## 2. How it runs

**2.1 Platforms and packaging.** Packages come only from the release page; a build job uploads a package
for a tag build only — an ordinary push and a manual dispatch both run the jobs but upload no package (`docs/KNOWN-LIMITATIONS.md`
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
explicit (`plugin.host_notes`). Every CLAP load failure is **typed** — ten distinct failure codes (an eleven-valued enum whose zero value
means *no failure*)
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
enforced (`RenderJobQueueTest`, `ControlRenderPresets`). `render.stems` exports every unmuted track to
its own file in an absolute directory, one stem per track (`StemExportTest`, `ControlStemExportVerb`; the
stem-*separation engine's* own tests compile only under `LMMS_HAVE_STEM_SPLIT`, which is OFF in this build
and in every release build, and they test the engine, not this verb).

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
withheld bindings, are listed one line each in `docs/LUA-API-STABILISATION.md` §8 (`Withheld, one line each`).

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
  `ControlRetroCapture`, `ControlCommandsSnapshot` — plus, on Windows only, `ControlNamedPipeSmoke`
  (registered under `IF(WIN32 AND PYTHON3_EXECUTABLE)`, so it is CI-only evidence and not among this
  build's 214).
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
  `build/tests/agent-surface-report.json` (written 2026-09-17): **340 commands, 339 swept, 1
  allowlisted (`telemetry.consent`), 48 actions reflected, 42 grandfathered baseline entries, 0 stale, 0
  problems.**
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
  restart. **Coalescing** covers the six declared commands only (`chord.set`, `clip.move`, `clip.resize`,
  `mixer.set_volume`, `plugin.param_set`, `rack.macro_set` — the live `control.undo_depth` list).
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

The catalogue below is **generated, never hand-edited**: it is the output of `scripts/capabilities-dump.py`
(the generator committed on this branch), captured from a live instance of the merged tip's own binary.
Re-run it with the command in Appendix B, §B.4, and replace this block wholesale.

<!-- Generated by scripts/capabilities-dump.py - do not edit by hand. Re-run it and replace this block, never hand-edit a row. -->

| provenance | value |
| --- | --- |
| binary | `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/build/zene` |
| binary sha256 | `87a65f52ac69c9bb9019e34f57ed3b237fcf67e25d905bcc7a758ed50b244e9d` |
| `control.version` | `0.2.1-alpha.643+d408e35` (proto 1) |
| surface | 340 command ids in 53 groups (205 mutating) |
| `control.transactions` caps | records cap 100, bytes cap 262144, retained 0 bytes |
| generated (UTC) | 2026-09-19 13:54:03Z |

### app (1 ids)

| command | mutating | description |
| --- | --- | --- |
| `app.version` | no | The product name, the version string, the control protocol version and the build options the binary was compiled with. |

### arrangement (1 ids)

| command | mutating | description |
| --- | --- | --- |
| `arrangement.get_state` | no | Every track with its clips, addressed by the stable trk-<n> and clip-<n> ids. The trk-<n> number is assigned at creation and persists in the project file. Addressing is scoped to the SONG container: a track inside a nested container (the <trackcontainer> a pattern track carries) is not reachable by id, exactly as it is not addressable by index. |

### audio (2 ids)

| command | mutating | description |
| --- | --- | --- |
| `audio.device_list` | no | The audio backends this build knows, with the one that is running and the one the config file selects. Requires no audio device: the running name is whatever the engine opened, the dummy device when nothing else would open. |
| `audio.device_set` | yes | Choose the audio backend for the next start: the device name is written to the config file's audioengine/audiodev, the same key the settings dialog writes. The running device is NOT switched - the engine constructs its device in AudioEngine::initDevices() during startup and the product's own settings dialog also only stores the preference and warns that a restart is needed. The result says so explicitly with applied="next_start". |

### automation (8 ids)

| command | mutating | description |
| --- | --- | --- |
| `automation.add_point` | yes | Add or replace one automation point (ticks, value) on a track's device parameter. The value is the model's own unit and is range-checked against the model's own min..max. Creates the automation clip when the parameter has none yet, and says so: that first call is the one control.undo cannot fully reverse (the new AutomationTrack has no journal checkpoint). Later calls are reversible. |
| `automation.clear` | yes | Remove every automation point from a parameter's clip, leaving the clip bound to the model (an empty clip no longer drives the parameter). Reversible through the ProjectJournal. |
| `automation.get_state` | no | Per track, every automatable device parameter with its stable '<plugin>/<index>' id, its current automation 'mode' (what automation.mode_set sets) and, for each automated one, its clip's points and record flag. A point's 'value' is the model's own unit (what plugin.param_get reports); 'raw_value' is what the clip stores. 'automated_only' trims the inventory. |
| `automation.mode_set` | yes | Set a parameter's automation mode: off (ignore the curve - the manual value stands and nothing is written), read (follow the curve, never write), touch (write while the control is held, then return to reading), latch (write from the first touch until the transport run ends), or write (overwrite the pass while the transport runs). The mode is runtime state: not persisted and not journalled, so it has no undo. automation.get_state reports each parameter's mode, so the change can be observed and not only issued. |
| `automation.ramp_get` | no | Per automated parameter: the sample-accuracy mode its clip asked for, and the ramp the audio thread built for it in the last rendered block - knots, frames, whether the value MOVES inside the block, and how many knots the fixed capacity had to refuse. Entries are read from the clips themselves, on every track the engine plays automation from (including the hidden global automation track), so 'track' is the target the parameter is addressed by ('ch-<n>'/'trk-<n>', the one ramp_set takes) and 'parameter' its '<plugin>/<index>' id: the pair addresses the parameter back. 'track' filters on that target, and 'include_block_mode' false trims it to the parameters asking for sample accuracy. 'unaddressable_object_count' counts the clip objects the surface has no parameter id for (the song's tempo, a pattern-internal control) rather than dropping them silently. |
| `automation.ramp_set` | yes | Render one automation clip at SAMPLE precision inside each audio block ('sample'), or leave it block-quantised ('block', the behaviour every project has always had). In 'sample' mode the parameter's per-sample buffer - what the mixer, the fx chain and the tracks multiply their samples with - carries the curve at every frame instead of one value smeared over the whole block. Reversible through the ProjectJournal. |
| `automation.record_mode_set` | yes | Set a parameter's automation clip record flag: 'on' to write the control's manual value into the clip at every tick the transport runs, 'off' to stop. The clip is a JournallingObject, so the inverse is a live checkpoint. |
| `automation.remove_point` | yes | Remove the automation point at an exact tick. The node list is a map keyed by tick, so the tick must match one. Reversible through the ProjectJournal. |

### bounce (1 ids)

| command | mutating | description |
| --- | --- | --- |
| `bounce.in_place` | no | Render one track's own output - its devices, fader, pan and sends, which is what a stem is - to a WAV file and return the file, its frame count and its sha256. With 'start' and 'end' (ticks) the file covers that region instead of the whole track. The session is NOT modified: the render runs in a child process against a serialised copy with every other track muted, so nothing in this instance changes and nothing is journalled. There is no interface for this (docs/KNOWN-LIMITATIONS.md). |

### browser (6 ids)

| command | mutating | description |
| --- | --- | --- |
| `browser.peaks` | no | The waveform peaks of an audio file: 'buckets' min/max pairs over the interleaved samples, the frames each bucket covers, the file's rate, channels and length, and whether the answer came out of the cache ('cached') or off the disk. The cache is bounded (capacity entries, base_buckets per file) and its hit and miss counts are reported. Read-only: it writes no project state and nothing at all on disk. |
| `browser.query` | no | Find files in the browser by name, by tag, or - with 'probe' - by what the audio file itself says it is (sample rate, channels, length, and the embedded title/artist/album/comment/genre tags). Every 'tags' entry must be present (AND). Sorted by path, so paging with offset is stable. The walk is bounded by scan_limit entries and the probe by probe_limit files, and a result that hit either bound says truncated: true. Read-only. |
| `browser.roots` | no | The directories the file browser reads - the same tabs the sidebar has: the user's samples, the factory samples, the user and factory presets and projects - with a stable id per root, whether each exists on this machine, and the path of the tag store. Read-only. |
| `browser.tag.add` | yes | Tag a file in the browser's library. The tag is filed against the file's canonical path in a JSON store in the user's config directory, and the write is reported: a tag that cannot be persisted is a refusal, not a silent success. Refused when the file already carries the tag. Reversible: the recorded inverse is browser.tag.remove, which control.undo dispatches. |
| `browser.tag.remove` | yes | Remove a tag from a file in the browser's library, persisting the store. Not-found when the file does not carry the tag: a removal that removes nothing is a refusal. Reversible: the recorded inverse is browser.tag.add, which control.undo dispatches. |
| `browser.tags` | no | The tag vocabulary of the library: every distinct tag with the number of files carrying it, how many files carry at least one tag, the store's own path, and whether the store could be read. Read-only. |

### bus (3 ids)

| command | mutating | description |
| --- | --- | --- |
| `bus.create` | yes | Create a parallel bus channel (Mixer::createBusChannel) and return its new ch-<n> id. A bus never receives instrument output; sends INTO it default to pre-fader. Reversible: one undo step deletes the bus this created. |
| `bus.list` | no | Every parallel bus channel with its stable ch-<n> id, fader, incoming sends and PDC numbers. Read-only. |
| `bus.remove` | yes | Delete a bus channel. Refused for a channel that is not a bus. NOT reversible: the command records the bus's full state but nothing recreates a channel with state, so control.undo refuses, typed, and names the fallback. |

### chain (6 ids)

| command | mutating | description |
| --- | --- | --- |
| `chain.apply` | yes | Apply a stored chain preset to a target: the target's chain becomes the preset's devices, IN THE PRESET'S ORDER, each one restored from its own state document, so the parameters are the values the chain was captured with. The target's existing devices are removed (the plugin.unload lifetime). A preset naming a device this build cannot load is refused and the target's chain is left untouched. Reversible: one control.undo puts the previous chain back, devices and settings. |
| `chain.get_state` | no | One chain preset in full: its name, its file, its device count and every device in order with the plugin's own identity (the LADSPA file and label, the LV2 URI, or the built-in name), the format, and the size and SHA-256 of the device's state document. The state documents themselves are not returned - they are what chain.apply writes back, and chain.list's hashes are what a caller compares. |
| `chain.list` | no | Every chain preset in the store: its name, its file, its device count and one summary per device (the plugin, the format and the size and hash of the device's own state document). Pass "name" for one preset in full. The store lives OUTSIDE the project - the user preset tree, chainpresets/ - so the presets are the same ones whichever project is open, which is the point of a preset. Writes nothing. |
| `chain.remove` | yes | Delete a chain preset from the store. The preset's own bytes are captured first, so the removal is reversible: one control.undo writes the same file, byte for byte, back to its own path. |
| `chain.rename` | yes | Rename a stored chain preset. The name is the key, so a rename onto a name the store already holds is refused, typed, and changes nothing. Reversible through a recorded action checkpoint that renames the file back. |
| `chain.save` | yes | Capture a target's effect chain as a named preset: every device in the chain's own order, each with the state document plugin.state_save writes for it (enabled, wet, autoquit and every parameter). Writes ONE file in the store (chainpresets/<name>.zcp), outside the project, so the preset can be applied to a track in another project, and so project.save/project.open cannot lose it. An existing preset of that name is refused unless "overwrite":true. 'target' is a trk-<n> or ch-<n> id. Reversible through a recorded action checkpoint that puts the file - or the revision it replaced - back. |

### chord (9 ids)

| command | mutating | description |
| --- | --- | --- |
| `chord.clear` | yes | Empty the project's chord track, reporting how many chords went. An already-empty track is REFUSED, typed: a clear that would change nothing must not leave an undo step behind. Reversible: one recorded action checkpoint holding the whole <chord-track> element as it was. |
| `chord.detect` | no | What chords a MIDI clip's notes spell. The notes are grouped into slices - by default notes that start at the SAME tick; 'window_ticks' widens that for a strummed or humanised take, measured from each slice's first note so a run of sixteenths cannot chain into one huge slice - and each slice is named from this engine's chord vocabulary. Every slice reports the nearest entry with 'missing' (chord tones the slice does not sound) and 'extra' (tones the chord does not contain), so 'exact' false is visible rather than rounded to a name, and the whole clip's key is reported too. Read-only: nothing is written to the clip or the track (write a detection with chord.detect_to_track). |
| `chord.detect_to_track` | yes | Detect the chords a MIDI clip's notes spell (the same detection chord.detect reports) and write them onto the project's chord track. By default the track is REPLACED; 'append' adds to what is there. A slice with no name in this engine's vocabulary cannot be a chord-track event and is reported as `skipped` - and if nothing at all could be written the track is left EXACTLY as it was and the call is refused, typed, rather than half-written. Reversible: one recorded action checkpoint. |
| `chord.get_state` | no | The project's CHORD TRACK: every chord with its position, length, root pitch class, the octave the root sounds in, the root's absolute MIDI key, the chord's name and the key (scale) it was written in, plus the track's own bounds. Read-only. The track is project state saved inside <song> as one <chord-track> element (written only when it holds a chord, so a project that never used one is byte-identical to before), and it is what chord.track_write turns into notes. Chord and scale names come from the piano roll's own vocabulary - see chord.progression_list. |
| `chord.progression_generate` | yes | GENERATE a named progression into a MIDI clip as notes. The walk and the key are the pre-existing vocabulary's: each step of the progression is the chord on that SCALE DEGREE, built by stacking the scale's own tones in thirds and named by this engine's chord table (a stack the table cannot name comes back with an empty 'chord' and is still written). 'pattern' is block, arpeggio_up, arpeggio_down or broken; 'step_ticks' is how long each chord lasts (default one 4/4 bar); 'steps' defaults to one pass of the walk. SEEDED AND REPEATABLE: 'variation' (0..1, default 0) opens the draws - each chord's voicing, its position and its velocity - and every draw is a pure function of 'seed' and that chord's own identity, so the same request reproduces the same take note for note and a different seed gives a different one. With 'variation' 0 nothing is drawn and the seed decides nothing. Reversible through the ProjectJournal (MidiClip checkpoint). |
| `chord.progression_list` | no | What this engine can be asked for: the named PROGRESSIONS the generator walks (each a name and the scale degrees it steps through), the SCALES a key may name, the CHORDS a chord-track event may name, the patterns a generation can lay a chord out with, and the group's bounds. Every one of those vocabularies is the piano roll's own (InstrumentFunctionNoteStacking::ChordTable, the table behind its chord and scale selectors) - this group adds no names of its own. Read-only; with 'name' one progression is reported on its own. |
| `chord.remove` | yes | Remove the chord at an absolute tick from the project's chord track, reporting the event that went. A tick that holds no chord is a typed not_found, never a silent success. Reversible: one recorded action checkpoint, and the recorded inverse names chord.set with the removed event's own arguments. |
| `chord.set` | yes | Write ONE chord into the project's chord track at an absolute tick: the chord's name (a chord of this engine's vocabulary - chord.progression_list lists them), its root pitch class, the octave the root sounds in, and optionally its length. The POSITION is the key: a set at a tick that already holds a chord REPLACES it (the command reports `replaced`), so a track cannot hold two chords at once. 'length' 0 (the default) means HOLD - the chord sounds until the next one, which is what a chord track means. Reversible: one recorded action checkpoint (the track is project state the Song's journal checkpoint does not carry), so one control.undo takes the write back. |
| `chord.track_write` | yes | Write the CHORD TRACK's own chords into a MIDI clip as notes: every event's chord is voiced from its root and laid out under 'pattern' (block, arpeggio_up, arpeggio_down, broken), each event starting where the track says and sounding for as long as the track says (the distance to the next chord, or 'length' for the last one). 'replace' (default true) clears the clip's notes first and reports how many went; false adds to them. Reversible through the ProjectJournal (MidiClip checkpoint). An empty chord track is refused, typed: there is nothing to write. |

### clip (16 ids)

| command | mutating | description |
| --- | --- | --- |
| `clip.add` | yes | Create a clip on a track at a tick position and return its stable clip-<n> id. Reversible through the ProjectJournal (Track checkpoint). |
| `clip.crossfade` | yes | Ramp two overlapping clips on one track into each other: the outgoing clip gets a fade-out and the incoming clip a fade-in, both exactly as long as their overlap, with the given shape (default equal_power, which sums the pair to unity POWER). Reversible through the ProjectJournal (Track checkpoint). |
| `clip.delete` | yes | Delete a clip from its track. dry_run previews it. Reversible through the ProjectJournal (Track checkpoint). |
| `clip.duplicate` | yes | Copy a clip on its own track (notes included) and return the copy's id. Reversible through the ProjectJournal (Track checkpoint). |
| `clip.link_create` | yes | Link clips to a clip so they share its content: an edit to the note list of any member is seen by every member (linked / smart clips). 'clip' is the content source and the group's anchor - if it is already linked, the named clips join ITS group; otherwise a new group is created and 'clip' joins it. The new members ADOPT 'clip'"s notes (the group starts in sync), while their own position, length, source offset, fades, gain and take lane stay their own: a link shares content, not placement. Reversible through the ProjectJournal (one Clip checkpoint per member written, merged into one undo step). See docs/LINKED-CLIPS.md. |
| `clip.link_get_state` | no | Read the link groups: every group with its members, the content channel it shares ('notes'), the note count, which member is the reference (the group's first member in arrangement order) and - per member - whether its content still matches the reference (`in_sync`) or has drifted (`divergent`). With 'clip', the report is that clip's group only; without it, every group in the song. Read-only. |
| `clip.link_remove` | yes | Unlink a clip: it leaves its link group and keeps the content it has, but stops following - and stops being followed by - the other members. This is the operation that detaches (there is no detach-on-edit: an edit to a member propagates). When the group is left with a single member that member's link is dissolved too, so no group of one outlives its last pair; a clip that is not linked is refused, typed. Reversible through the ProjectJournal (Clip checkpoint). |
| `clip.link_sync` | yes | Force the group to agree: every other member of 'clip'"s link group is given this clip's content, and the members that had to be rewritten are named in `written`. An edit through the note.* verbs already propagates on its own - this is the REPAIR verb for the edit kinds that do not (a piano-roll gesture the engine's note entry points do not see, a hand-edited project file), and it is what makes a disagreement fixable instead of permanent. Reversible through the ProjectJournal (one Clip checkpoint per member written). |
| `clip.move` | yes | Move a clip to an absolute tick position. Reversible through the ProjectJournal (Clip checkpoint). |
| `clip.resize` | yes | Set a clip's length in ticks (the clip becomes manually resized). Reversible through the ProjectJournal (Clip checkpoint). |
| `clip.select` | no | Select (or clear with an empty id) a clip for roll.get_state and the get_states' selection flags. Control-surface state, not project state. |
| `clip.set_fade` | yes | Set a clip's fade-in and/or fade-out in ticks, with a shape each (linear, exponential, equal_power). An omitted argument keeps its current value; fade_in + fade_out must fit inside the clip. Reversible through the ProjectJournal (Clip checkpoint). |
| `clip.set_gain` | yes | Set a clip's gain in dB (-60..+24, 0 is unity). The gain multiplies every frame the clip renders, on top of its fades; a clip's own gain is independent of any other clip's. Reversible through the ProjectJournal (Clip checkpoint). |
| `clip.slip` | yes | Slip a clip's content inside its own rectangle: the clip's position and its length do not move, and the part of the source that plays at the clip's start becomes 'offset' ticks into it. Slip is the only verb that moves the audio without moving the clip. Reversible through the ProjectJournal (Clip checkpoint). |
| `clip.split` | yes | Split a clip at an absolute tick position, leaving the left part in place and returning both ids. Reversible through the ProjectJournal (Track checkpoint). |
| `clip.trim` | yes | Move a clip's start edge to an absolute tick, holding the audio the clip already carried at the same song position: the start, the length and the source offset move together, which is what the song editor's own left-edge drag does. An optional 'end' trims the tail in the same step. Reversible through the ProjectJournal (Clip checkpoint). |

### clock (3 ids)

| command | mutating | description |
| --- | --- | --- |
| `clock.get_state` | no | What this engine's MIDI clock is doing: the master's enabled flag, the port it sends on and whether that port is subscribed to a client destination, the message counters it has emitted and a bounded monitor of the last messages in order; and the slave's enabled flag, its source port, whether it follows the measured tempo, whether it is LOCKED, the measured tempo, the drift of the last interval from the window's mean, the drift bound, the tempo error the pulse jitter can produce, the window it measures over, the pulse count and the last song position and time-code values received. Read-only. `mtc` reports "absent": this release generates no MIDI time code (see docs/KNOWN-LIMITATIONS.md). |
| `clock.master_set` | yes | Enable or disable the DAW as a MIDI clock MASTER: 24 clock pulses to the quarter note, START/STOP/CONTINUE and a Song Position Pointer on the transport's own edges, emitted from the audio thread through this engine's MIDI output. `port` names the writable client destination to subscribe to (see midi.device_list for the names); an empty one keeps the current subscription. Enabling when the engine has no MIDI client to send through is refused, typed, rather than reported as a success that emits nothing. Reversible: the enabled flag AND the port subscription are restored by control.undo. |
| `clock.slave_set` | yes | Enable or disable the DAW as a MIDI clock SLAVE: follow an incoming clock, measure its tempo over one quarter note of pulses and, when follow_tempo is on, write that tempo to the Song once it has left the dead band. A slave that is enabled with NO clock arriving never locks, writes nothing and does not move the transport - clock.get_state reports locked:false. `source_port` names the readable client port to follow (informational: the engine's MIDI readers deliver a clock from wherever it arrives). `drift_bound_ms` is how far one pulse interval may stray from the window's mean and still count as locked. Reversible for the CONFIGURATION: the tempo a following slave wrote is a trajectory of writes, not one state, and control.undo does not restore it (the transaction names the value). Receiving START/STOP/CONTINUE/SONG POSITION is counted and reported; moving the transport FROM the incoming clock is not in this release (see docs/KNOWN-LIMITATIONS.md). |

### comp (7 ids)

| command | mutating | description |
| --- | --- | --- |
| `comp.assign` | yes | Assign an audio clip to a take lane of its own track: the lane tag is a field on the clip, so the take is the clip and its audio is never copied or moved. A lane the clip's track does not have is refused, as is a MIDI clip (take lanes carry audio takes in this release; docs/COMPING.md). Reversible through the ProjectJournal (Clip checkpoint). |
| `comp.get_state` | no | One track's comping state: its take lanes (with the ids of their takes), the composite's ordered segments, and what each segment resolves to - the take clip and the source frame its first tick reads. A segment whose lane has no take covering it is reported as 'unresolved', never guessed at. Reads only. |
| `comp.lane_add` | yes | Add a take lane to a track and return its index. The index is the lowest one the track does not use; a lane is never renumbered by a removal (docs/COMPING.md). The lane holds no audio of its own: the takes are the clips tagged with that index, assigned with comp.assign. Reversible through the ProjectJournal (Track checkpoint). |
| `comp.lane_list` | no | List take lanes: the named track's, or every track's in song order when no track is given. Each lane reports its index, its name and the ids of the takes assigned to it (comp.assign). Reads only. |
| `comp.lane_remove` | yes | Remove a take lane from a track. Any composite segment that named the lane falls back to the track's base lane, because a composite stays gapless over its span; removing the last lane clears the composite. A lane the track does not have is refused, typed, naming the lanes it does have. Reversible through the ProjectJournal (Track checkpoint). |
| `comp.rebuild` | yes | Rebuild the composite: sort it, merge neighbouring segments that are the same lane at a continuous offset, and - when a span is given - clamp the composite to exactly [begin, end) and fill every gap in it with the track's base lane. With no span it normalises what is already there (idempotent on a well-formed composite). The take audio is not touched. Reversible through the ProjectJournal (Track checkpoint). |
| `comp.select` | yes | Choose which take lane supplies the composite over [begin, end) ticks, slipped 'srcpos' ticks into that lane's take (default 0 = the take's own start). A selection paints over what was there: the range is cut out of every existing segment and the new choice is inserted, so the composite stays ordered and gapless over its span. The take audio is not touched, and nothing is copied. Reversible through the ProjectJournal (Track checkpoint). |

### control (12 ids)

| command | mutating | description |
| --- | --- | --- |
| `control.commands_list` | no | Every registered command with its schemas and requires declaration. |
| `control.id_contract` | no | The stable-id contract: every id family, its persistence, its form, the document element it lives on, and the current count. |
| `control.ping` | no | Liveness probe; also reports whether the engine is addressable yet, and why not when it is not. |
| `control.quit` | no | Ask the instance to run its normal shutdown (the reply is sent first). Unsaved changes are discarded unless save is true. |
| `control.redo` | no | Redo the last undone journal checkpoint. A structural step whose inverse was a one-way action has nothing to redo, and says so: the redo stack is emptied at that point rather than replaying an older step. |
| `control.set_undo_coalescing` | no | Set the window (milliseconds) inside which a run of the same command on the same target is ONE undo step, so a 200-call drag costs one Ctrl+Z. 0 disables grouping entirely, which is the pre-0.3.0 behaviour. Changing the window ends the gesture in flight, so it can never group two gestures retroactively. Does not touch the project. |
| `control.set_undo_depth` | yes | Set the undo stack's two caps: `steps` (how many undo steps are kept) and/or `bytes` (the serialised size they may occupy). Lowering a cap EVICTS the oldest steps immediately and the result reports how many (`dropped`); the caps themselves are reversible (one control.undo restores them), the evicted steps are not. Refuses a value outside the declared ceilings rather than clamping it. |
| `control.surface_report` | no | Reflect the live menu/toolbar surface: every user-visible action and the command id it declares (SPEC A15). |
| `control.transactions` | no | The transactions recorded for mutating commands (SPEC A16 hook), each with the contract table's class and the serialised size of the record, plus the bounds they are kept within (cap_records/cap_bytes, retained_bytes, evicted, capped). |
| `control.undo` | no | Undo the last agent command: the engine's own ProjectJournal step it recorded (the same stack the GUI's Ctrl+Z unwinds), or the recorded inverse command when the change is file-level. If the last recorded command has no inverse, this FAILS with the typed 'irreversible' error and names the documented fallback instead of undoing an older command. |
| `control.undo_depth` | no | Read the engine's own undo stack: its depth and redo depth, the TWO bounds it is kept within (a count cap and a byte budget), the serialised bytes it retains, how many steps a bound has evicted, and the coalescing rule in force (the window, and which commands it groups). Writes nothing. |
| `control.version` | no | The product version string and the control protocol version. |

### controller (7 ids)

| command | mutating | description |
| --- | --- | --- |
| `controller.feedback` | yes | Turn LED/feedback output on or off for one MIDI-bound control. With feedback on, every control-change that moves the model is written back to the controller as a control-change on the channel the hardware transmits on, so a motorised fader or a lit ring follows the value the project holds - including after a project load, a MIDI-learn binding or an automation step. Enabling it also moves the control's MIDI port to Duplex mode, because a port that is not output-enabled cannot carry the write; that port mode is serialized in the project, which is why this command records a transaction. 'written' reports whether THIS call's own feedback write was handed to the port's output path, and 'output_events_written' is the port's own count of control-changes that reached the MIDI client - 'written': false with a port that is not output-enabled is the honest answer, not an error. Without a real controller attached nothing consumes the bytes; the count is a write to the client, not a proof that a lamp lit. |
| `controller.soft_takeover` | yes | Turn soft-takeover on or off for one MIDI-bound control, and/or set the value it waits for. While soft-takeover is on, a hardware control that is bound to a control whose stored value differs from the knob's position is IGNORED: the model does not jump to wherever the knob happened to be. The hardware takes the control over the moment a control-change crosses the stored value (or lands within one MIDI step of it), and only then do that control-change and the ones after it move the model. Without 'target' the value the model holds now is used, which is the reading a user expects: the surface was in some position when the project was saved. 'control' names the model's fullDisplayName() (controller.surface_state lists the names); an empty or absent 'control' means the project's only bound MIDI control. 'captured' reports whether the hardware has already taken over - it is reset by the first movement after enabling, which is what makes a fresh binding wait for the knob. |
| `controller.surface_state` | no | Read-only: every control in this project that a MIDI controller drives, addressed by the model's fullDisplayName(). Each entry carries the MIDI channel and controller number, the soft-takeover flag and whether the hardware has already taken the control over ('captured'), the value the takeover waits for ('takeover_target'), the LED/feedback flag and the port's two output counters. 'output_events_written' is the count of control-changes this control's port actually handed to the MIDI client's output - the measurement the LED half is proved by, taken at the client boundary in MidiPort (include/MidiPort.h) and not self-reported by the command. This is how an agent reads a surface without a controller in hand. |
| `controller.template_apply` | yes | Apply a saved mapping template to this project: for every binding in it whose target model resolves, create the MIDI controller and the connection that drives that model, and restore the binding's soft-takeover and feedback flags. 'bound' counts the controls the call bound, 'skipped' counts the bindings whose target is not in this project and 'missing' names them - a template saved against another song applies the part that resolves and REPORTS the rest, rather than failing as a whole. A binding whose target is already driven by a MIDI controller is re-bound, which is what makes applying a template a way to move a surface onto a different set of addresses. This creates project state (the model's <connection> element), so the project is marked modified. controller.surface_state reports the resulting surface. |
| `controller.template_delete` | yes | Remove one saved mapping template from this machine. 'removed' is false when no template of that name existed, which is reported rather than treated as an error (the file is the state, and the state asked for - no such file - already holds). No project state is touched: the project's own bindings are unaffected. |
| `controller.template_list` | no | Read-only: the mapping templates saved on this machine, by name, sorted, with the directory they live in and - per template - how many bindings it holds, how many of them resolve in the CURRENT project, and the names that do not. A template whose targets do not resolve still lists: it is a saved mapping set, not a claim about the project that is open. |
| `controller.template_save` | yes | Save the project's current MIDI bindings as a named mapping template - one JSON file in the user preset tree, listed by controller.template_list and re-applied by controller.template_apply. Each binding records the MIDI channel, the controller number, the model's fullDisplayName() and the binding's soft-takeover and feedback flags, so applying a template restores the whole surface and not just the addresses. A template is not a project: it survives closing the project and is what a user with two controllers swaps between. An existing template of the same name is overwritten. 'name' must be one file name - a path separator, a ".." or a leading dot is refused. |

### crash (6 ids)

| command | mutating | description |
| --- | --- | --- |
| `crash.acknowledge_report` | yes | Acknowledge the pending crash report: write the reporter's `offered` sentinel so the report is not offered again on the next start, and KEEP the file so it can still be attached. Refuses when there is no report. Not reversible - no function in this engine removes the sentinel; control.undo names the sentinel to delete by hand. |
| `crash.disable` | yes | Disarm the crash reporter's signal handlers (crashreporter::uninstall): the default disposition is restored for exactly the signals install() claimed, so a crash is no longer reported and the process dies as it would without the reporter. Deletes NOTHING - the report, its directory and the session marker survive, and crash.list_reports still names them. Refuses when it is not armed. Reversible through control.undo, which dispatches crash.enable with the directory captured before the call. |
| `crash.discard_report` | yes | Clear the crash reporter's report: delete the report file and the offered sentinel beside it (crashreporter::discardPendingReport). Refuses when there is neither, so a call that would change nothing writes nothing. Not reversible - the report's bytes are gone and nothing writes a report from a caller's bytes; control.undo names the fallback. |
| `crash.enable` | yes | Arm the crash reporter's signal handlers (crashreporter::install). With no arguments it re-arms to the report directory the reporter remembers; an explicit `directory` arms that one instead. The result reports `armed` from the kernel's own signal dispositions and `installed` from the module's flag, plus the signal set and the report path. Refuses when it is already armed and when the directory does not exist (install() never creates it). Reversible through control.undo, which dispatches crash.disable. |
| `crash.list_reports` | no | The crash reporter's state: whether it is installed, where its report directory is, every report it holds (path, existence, size, last-written time), whether a report is still pending an offer, whether the offered sentinel is present, whether a session marker says the previous run exited uncleanly, its two hard bounds, and the upload policy. Read-only, and it answers in every configuration. |
| `crash.upload_report` | yes | Send a crash report somewhere. Refused, always: this build has no upload and no network code of any kind in the reporter (include/CrashReporter.h states it as a design property), so no send is faked. The refusal names the file to attach by hand. |

### dawproject (4 ids)

| command | mutating | description |
| --- | --- | --- |
| `dawproject.convention` | no | The DAWproject version and convention this build implements, as data: the format's name and version (README.md: "The format is version 1.0 and is stable"; Project.xsd declares version="1.0"), the container's two entry names and its text encoding, the time unit every time value is written in, LMMS' own ticks per quarter note and the exact rule between a tick and a beat, the ZIP method used, the contentType vocabulary and how LMMS' nine track types map onto it, and the STATED LOSSES - everything this module does not carry. A file declaring another MAJOR version is refused rather than read with this build's assumptions. |
| `dawproject.export` | no | Write the session as a .dawproject container: a ZIP whose project.xml carries the tracks, their clips and notes, the tempo map and each track's mixer strip, in the published format's own vocabulary at version 1.0. The file is written with STORE entries so it needs no compression dependency and every ZIP reader opens it. `loss` in the reply reports EXACTLY what the format could not carry from this session. Refuses an existing file unless `overwrite` is true, and refuses a relative path. Writes a file; changes nothing in the session. |
| `dawproject.import` | yes | Apply a .dawproject container's model to the session: REPLACE the tracks, write the global tempo and time signature, replace the tempo map with the file's two automation timelines and set the mixer strips it carries. One command, one undo: the whole session captured before the import is put back through the project loader's own paths when the stack unwinds. `loss` in the reply reports what the file carried that this build does not apply. Refused, typed, when the file cannot be read, when a tempo or metre is outside the engine's own bounds, or when the file needs more tempo-map events than the map holds - a refusal writes nothing. |
| `dawproject.read` | no | Read a .dawproject container's model WITHOUT touching the session: the format version the file declares, the application that wrote it, every track with its clips and notes, the tempo and time-signature timelines, the mixer strips, the entries the container carries that this module does not use, and the model's printable digest - what makes a round trip checkable against the FILE and the MODEL rather than a hash. A file that is not a ZIP, has no project.xml entry, declares another major format version, or carries a value the engine will not accept is a typed refusal. |

### detect (3 ids)

| command | mutating | description |
| --- | --- | --- |
| `detect.analyze` | no | Analyse an audio file the importer can read and report what it implies: the tempo (BPM) over the declared 40..240 BPM band, the FIRST transient (in seconds and frames), and the key as a tonic pitch class plus a scale name from the pre-existing scale vocabulary - each with the detector's own score and the method that produced it. Writes NOTHING: this is the suggestion BACKLOG.md item 10 asks for, and `detect.apply` is the acceptance. Reads at most `max_seconds` (default 60) from the START of the file. |
| `detect.apply` | yes | Analyse an audio file and WRITE the result into the project's own fields: the detected tempo becomes a tempo-map event at tick 0 (the map is switched on, and the whole BPM is the map's integer - the exact estimate is reported back beside it), and the detected key is stored in the project's `<detected-key>` field under a scale name the pre-existing vocabulary knows. `tempo` and `key` select the halves (both default to true); a half the analysis found nothing for is REFUSED and NOTHING is written. Both writes are ONE undoable step: control.undo takes the whole detection off. |
| `detect.get_state` | no | Read back what the project holds: the detected key (the `<detected-key>` field, when a detection has been applied), the tempo map's own state and what it answers at tick 0, the scale vocabulary a key name can come from, the two detection method names, the bounds - and the accuracy sentence (`method.accuracy_note`), which says plainly that real-world accuracy is unverified and that the confidence numbers are scores, not probabilities. |

### device (2 ids)

| command | mutating | description |
| --- | --- | --- |
| `device.mpe_get_state` | no | Whether MIDI Polyphonic Expression input is on, and what that means in this engine: the flag itself, which axes reach PLAYBACK and how (all three do - pitch as a frequency ratio the engine applies to the note itself, and pressure and timbre as MIDI channel pressure and CC74 sent to the instrument on the channel the note's own note-on took, which for a note captured from an MPE controller is its member channel; the two MIDI axes need an instrument that consumes them - a hosted instrument or a MIDI output port - because the built-in synthesisers are driven by frequency and volume and never see MIDI), how a note stores its capture (the optional mpepitch / mpepressure / mpetimbre attributes), the channel count and master channel the model assumes, the default bend range and the MPE+ cap on notes per channel. Read-only. 'per_stream_settings_reachable' is false and the result says why: the master channel and bend range live on a MIDI input stream's own MpeExpression, which no object the control surface holds owns. |
| `device.mpe_set` | yes | Turn MIDI Polyphonic Expression INPUT on or off. While it is on, a bend / channel pressure / CC74 arriving on a note's own MPE member channel is that note's expression and is consumed instead of bending the whole instrument; while it is off the input path is exactly what it was before MPE existed (the engine's own flag is deliberately NOT serialized, so a project never changes meaning because of it). All three axes reach playback: pitch as a frequency ratio, pressure and timbre as MIDI channel pressure / CC74 on the note's own channel - which a hosted instrument or a MIDI output port consumes, and no built-in synthesiser does. Switching it off does NOT clear what is already stored on the notes: note.expression_get still reads it, and note.expression_clear is the verb that removes it. Reversible: a recorded action step restores the previous flag, so control.undo and Ctrl+Z are one history. |

### dsp (1 ids)

| command | mutating | description |
| --- | --- | --- |
| `dsp.get_state` | no | Every device chain with its fx-<n> instances, each one's parameter values and, on an instrument track, its 'inst' entry with the instrument's parameters. With 'target' it reads exactly that target, an empty chain included; without it, every track and mixer channel that carries at least one device. |

### export (8 ids)

| command | mutating | description |
| --- | --- | --- |
| `export.get_settings` | no | Read the render settings that outlive one OutputSettings: whether the next export dithers, which sample-rate-conversion quality it resamples with, and whether it writes an EBU R128 loudness report beside its output. All three are OFF/DEFAULT unless something asked for otherwise - dither is off by default because this release's render-reproducibility claim depends on it, 'linear' is the converter every render the engine has produced so far used, and the loudness report is opt-in because it is a measurement, not a render choice. Read-only: no transaction is recorded. There is no interface for the first two; drive them through export.set_* (docs/KNOWN-LIMITATIONS.md). The loudness report has an interface - the export dialog's checkbox - and export.set_loudness_report drives the same value from here. |
| `export.preset_add` | yes | Save a named render/export preset: the sample rate, the bit depth and the stereo mode a render is launched with. Writes ONE document in the store (renderpresets/<name>.zrp), outside the project, so the preset can be applied to another project and project.save/project.open cannot lose it. sample_rate must be inside the window the render path accepts (44100 to 192000 Hz - the shipped CLI's own check), bit_depth is one of 16, 24 or 32 and stereo_mode one of mono, stereo or jointstereo; a value nothing could honour is refused here rather than discovered by a failed render. An existing preset of that name is refused unless "overwrite":true. Reversible through a recorded action checkpoint that removes the document - or writes back the revision it replaced. |
| `export.preset_apply` | yes | Put the render path on a saved preset: the next render (render.render) is started with that preset's sample rate, bit depth and stereo mode. Passing no "name" - or an empty one - returns the render path to its OWN defaults (44100 Hz, 16-bit, joint stereo), which is what a render with no apply uses. The selection is process-wide and NOT project state: it is not saved with the project and project.open does not change it, exactly like export.set_dither beside it. Reversible: one control.undo restores the selection this call replaced. A render already performed is not undone by that: its file stays where it was written, so the fallback for a render made under the wrong preset is to apply the right one and render again. |
| `export.preset_list` | no | Every saved render/export preset in the store and the settings the NEXT render will be started with. A preset is a name plus the three OutputSettings fields a render can be told - sample_rate, bit_depth, stereo_mode - and passing "name" returns one of them in full. The store lives OUTSIDE the project (the user preset tree's renderpresets/, one JSON document per preset), so the presets are the same ones whichever project is open, which is what makes them presets. Reports "active" (the applied preset, null when none is) and the settings in force. Writes nothing. |
| `export.preset_remove` | yes | Delete one render/export preset from the store. The document's bytes are captured before the removal, so one control.undo writes the preset back byte for byte. Removing a preset that is currently APPLIED leaves the render path on the settings it already has - the applied selection is a copy, and what goes is the store entry, not the render settings (use export.preset_apply with no name to go back to the defaults). A name the store does not hold is a typed not_found and changes nothing. |
| `export.set_dither` | yes | Turn TPDF dither for the integer export formats on or off. The dither is applied by the WAV encoder immediately before quantisation, at the depth actually written (16- and 24-bit; 32-bit float has no quantisation step and is left alone). It is DETERMINISTIC - seeded from a constant - so a dithered render is still reproducible and two runs produce identical files. Off by default; turning it on changes the bytes of every subsequent render, which is the point. |
| `export.set_loudness_report` | yes | Turn the render path's EBU R128 loudness report on or off for the NEXT render (feature row 24 of docs/FEATURE-LIST-0.3.0.md, "LUFS / loudness metering"). With it on, the render measures every block it writes with the engine's BS.1770-4 meter (the same LufsMeter `meter.get_state` and `meter.measure_file` report through) and writes the report beside the output as <output>.loudness.txt - integrated LUFS, the loudest 3 s window, true peak in dBTP and the EBU R128 verdict against -23.0 LUFS-I +/- 0.5 LU and -1.0 dBTP. MEASURE-ONLY: the rendered audio is byte-identical whether the report is on or off (the tap reads the frames immediately before the file device writes them), which is why the default is off rather than on. The same value is the export dialog's "Loudness report (EBU R128)" checkbox and the CLI's --loudness-report flag; this verb is how an agent sets it. Reversible: control.undo restores the previous selection. |
| `export.set_src_quality` | yes | Choose the sample-rate-conversion converter the render resampler uses: 'linear' (the default, and the converter this engine has always used), 'sinc_fastest', 'sinc_medium' or 'sinc_best' (libsamplerate's sinc converters, in increasing quality and cost). The choice reaches the resampler through Sample::play, so it governs every mismatch-rate source in the render, not just the export file. It takes effect at the next render; a render already in flight keeps the converter it started with. |

### freeze (3 ids)

| command | mutating | description |
| --- | --- | --- |
| `freeze.region` | yes | Freeze one tick range of a track: the region is rendered and the take plays inside it, while the source keeps playing outside it. The clips that START inside the region are muted (and recorded, so freeze.unfreeze unmutes exactly those and nothing else); a clip that starts before the region and runs into it is left alone and named in 'overlapping_clips', because muting it would silence audio outside the region - that clip sounds twice inside the region (docs/KNOWN-LIMITATIONS.md). Reversible through the ProjectJournal (Track checkpoint). |
| `freeze.track` | yes | Bounce a track and make it PLAY THE RENDER instead of its clips: the engine queues the take for every pass inside its window and schedules none of the track's own playback, so the source is disabled for as long as this holds. The take carries the track's devices, fader, pan and sends, so it is summed at the mix level and not through the track's chain a second time, and the track's volume and effects controls are inert until freeze.unfreeze. The state is saved with the project and one control.undo takes it off (Track checkpoint). Reversible through the ProjectJournal. |
| `freeze.unfreeze` | yes | Drop a track's frozen take: the track plays its own clips again, and the clips the freeze muted (freeze.region) are unmuted - exactly those, so a clip the user had muted themselves stays muted. The rendered file is left on disk: this is not a delete. Reversible through the ProjectJournal (Track checkpoint). |

### groove (7 ids)

| command | mutating | description |
| --- | --- | --- |
| `groove.apply` | yes | Apply a named groove to a MIDI clip's notes: each note is snapped to the slot it is nearest to and given that slot's tick and that slot's velocity. 'strength' (0..1, default 1) is how far each note travels toward both, so 0.5 is half the feel and 1 is the groove exactly - at which point a second apply has nothing left to do. Reversible through the ProjectJournal (MidiClip checkpoint): one control.undo restores every position and velocity. |
| `groove.extract` | yes | Capture the feel of a MIDI clip's notes into a NAMED groove: each slot's step becomes the mean timing deviation of the notes that fell in it and their mean velocity. The name is the key - extracting over an existing name replaces that groove. One undoable step (a recorded action checkpoint: the pool is project state the Song's journal checkpoint does not carry). |
| `groove.list` | no | The project's groove pool: every named groove with its cycle length, slot width and slot count, and - with 'name' - one groove's own steps (the tick and the velocity each slot applies, both absolute). Read-only. A groove is the timing and velocity feel of a note pattern, captured from a clip by groove.extract and written back by groove.apply. |
| `groove.quantize` | yes | Quantise a MIDI clip's notes onto a grid of 'grid' ticks, with 'strength' (0..1, default 1) for how far each note travels and a 'humanise_ticks' / 'humanise_velocity' amount added afterwards. 'mode' is 'nearest' (default), 'floor' or 'ceil'. The jitter is a pure function of 'seed' (default 0) and each note's own identity, so the SAME call on the same notes reproduces the same take; and it is a jitter, so applying it again on top of itself rolls again rather than being a no-op. Reversible through the ProjectJournal (MidiClip checkpoint). |
| `groove.remove` | yes | Delete a named groove from the project's pool, leaving the rest in place. Removing the last groove leaves the pool empty, which is the state a project that never captured one saves in (no <groove-pool> element at all). Reversible through a recorded action checkpoint. |
| `groove.rename` | yes | Rename a groove, keeping its steps and its position in the pool. A rename onto another groove's name is refused, typed, and changes nothing - the name is the key, so it would destroy that groove. Reversible through a recorded action checkpoint. |
| `groove.set` | yes | Write a groove VERBATIM: its name, its cycle length and slot width in ticks, and (optionally) each slot's timing offset and velocity - so a groove can be authored by hand as well as captured by groove.extract, and so an extract that replaced a groove has a real inverse to record. The name is the key: an existing groove of that name is replaced. One undoable step (a recorded action checkpoint that writes the captured pool back). |

### interchange (4 ids)

| command | mutating | description |
| --- | --- | --- |
| `interchange.smf_convention` | no | The tick/PPQ and time-signature convention this product's Standard MIDI File interchange uses, as data: the division it writes (ticks per quarter note), LMMS' own ticks per quarter note and the exact ratio between them, the unit the tempo meta event is expressed in, the byte layout of the time-signature meta event, the file shape (format and track layout), the tick-0 rule and the stated limits (steps only, conductor track only). |
| `interchange.smf_export` | no | Write the tempo map as a Standard MIDI File another DAW can read: format 1, one conductor track carrying the tempo and time-signature events, at division 480 ticks per quarter note (LMMS' own 48 ticks per quarter mapped in exactly). The file always names the step in force at tick 0, so a map whose first event is later still exports the session it is part of; `seed_events` reports how many halves that rule had to add. Notes, clips and automation are NOT in this file - it is a conductor track (the note export is File > Export MIDI, a different, pre-existing path). Refuses an existing file unless `overwrite` is true, and refuses a relative path. Writes a file; changes nothing in the session. |
| `interchange.smf_import` | yes | Replace the tempo map with the conductor events of a Standard MIDI File: every track's tempo and time-signature meta events, merged by tick, in LMMS' own tick domain at the file's own division. The imported map is ACTIVE - a conductor track IS a tempo map, so a map switched off would import tempo changes the timeline does not obey. One command, one undo: the map captured before the import is written back through TempoMapPublisher::edit when the stack unwinds. Refused, typed, when the file is not readable as a Standard MIDI File, when an event is outside the engine's own bounds (tempo 10..999, a denominator that is a power of two up to 32), or when the file needs more than the map's 128 events - a refusal writes nothing. |
| `interchange.smf_read` | no | Read a Standard MIDI File's conductor events back and report them in LMMS' own tick domain, WITHOUT touching the session: every track's tempo and time-signature meta events, merged by tick (the first the file names at a tick wins, per half), with the file's own division, how many events had to be rounded onto LMMS' 48-ticks-per-quarter grid, and whether the map could hold them all. This is what makes a round trip checkable against the file rather than against its hash: read the file, compare the map. A file that is not a Standard MIDI File, or whose division is an SMPTE rate, is a typed invalid_args refusal. |

### link (5 ids)

| command | mutating | description |
| --- | --- | --- |
| `link.get_state` | no | The session-sync state this instance sees: whether sync is on, how many peers are in the session and who they are, the session tempo and who declared it, the shared beat and its phase inside the quantum, this engine's own tempo and phase and the error between the two, and whether announcements can travel at all (transport.available / transport.reason). The model is 'zene-link-style' - Ableton Link's semantics (a shared tempo, a shared beat phase, a quantum, a peer set) without Ableton Link's protocol - and the `interop` block says so plainly rather than leaving a reader to infer it. Read-only: no transaction is recorded. There is no interface for any of it; drive it through link.set_* (docs/KNOWN-LIMITATIONS.md). |
| `link.set_enabled` | yes | Join or leave the session: on starts the transport, joins the multicast group and begins announcing this instance's tempo and beat every 100 ms; off stops both and forgets every peer. While sync is on, a tempo change made here - by any means, including transport.set_tempo - is announced to the session, and a peer's newer announcement sets this engine's tempo, so two instances stay on one tempo without anyone relaying between them. Joining also declares this instance's current tempo, so a session that was already running is adopted from its own newest announcement. The quantum and the kernel's tempo bounds are reported by link.get_state. Transport is UDP multicast on 224.76.78.75:20808, the group Ableton Link itself uses for discovery. |
| `link.set_quantum` | yes | Set the quantum, in beats: the length of the cycle the shared beat phase is measured against and the grid a phase-aligned action would be placed on. 4 (one 4/4 bar) is the default; 1 to 64 is accepted. The quantum is THIS instance's own setting and is not part of what peers agree on - a peer that reports a different one is reported with it (link.get_state peers[].quantum) and is not corrected. |
| `link.set_session_tempo` | yes | Declare a session tempo, in BPM, to every peer: the timeline is re-anchored at the declaration so the shared beat does not jump, the revision goes to one above the highest this instance has seen, and the announcement that carries it is newer than every declaration already in the session - so peers follow this one rather than the other way round. This is the explicit form of a tempo change; a plain transport.set_tempo reaches the session too, on the next announcement, because the model watches the engine's own tempo for a change it did not make itself. A session tempo is a double on the wire and an integer in this engine, so what is applied is the rounded value and the engine's own bounds win. |
| `link.set_start_stop_sync` | yes | Turn transport-start/stop sharing on or off: the flag that says a session's members share not only the tempo and the beat phase but the decision to be playing. It is announced to peers (peers report their own through link.get_state) and it is reported back, but this release does NOT act on it: nothing here starts or stops another instance's transport, because doing so would write the audio thread's play state from a network announcement. The flag is the contract's declaration half; the acting half is a stated limitation (docs/LINK-SYNC.md section 5). |

### mastering (3 ids)

| command | mutating | description |
| --- | --- | --- |
| `mastering.get_state` | no | The last auto-mastering run this instance performed: the report the run itself produced (one measured row per candidate - LUFS-I, the loudest 3 s window, measured dBTP, crest, the residual against that candidate's target and the two verdicts - plus the source render's own readings and the counted number of project renders), and the live state of the files it wrote, hashed now. `has_run` is false and `last_run` is null before the first run. `session_empty` is the fact mastering.run refuses on. Read-only. |
| `mastering.list_candidates` | no | The mastering candidate set wave 1 generates: each candidate's name, the delivery target it is graded against (its integrated loudness, tolerance and true-peak ceiling, plus the published document the numbers come from), the dynamics stage's five numbers and the chain settings they imply. The set is the engine's own (MasteringJob::defaultCandidates) - it varies target loudness, ceiling and dynamics so the candidates are objectively distinguishable. This command writes nothing and starts no render: mastering.run is what masters and measures. |
| `mastering.run` | yes | Auto-master the session, wave 1: render the mix ONCE (counted by the render machinery, returned as render_count), branch every candidate of MasteringJob::defaultCandidates() off that one render, write one wav per candidate into out_dir and measure each with the BS.1770-4 meter - integrated loudness, the loudest 3 s window, measured true peak and crest factor, plus the residual against that candidate's target and its loudness and true-peak verdicts. `out_dir` is required and absolute. The session is not modified (the render runs in a child process on a serialised copy). REVERSIBLE through a recorded action checkpoint: the files this run created are removed and the revisions the directory already held are written back, byte for byte (bounded at 64 MiB of pre-existing wav files - beyond that the run is refused rather than performed without an inverse). Names no best candidate: there is no validated preference scorer, so the choice is the user's. |

### meter (3 ids)

| command | mutating | description |
| --- | --- | --- |
| `meter.arm` | yes | Arm or disarm the PASSIVE loudness tap on the master mix. ARMING STARTS A MEASUREMENT, EVERY TIME - including a re-arm of an already-armed tap: the accumulated integrated loudness, short-term maximum and true peak are dropped and everything measured from now on is the new reading, so 'arm, play the section, read meter.get_state' always reports THAT section and two measured sections can never be silently averaged into one number. A caller that wants the running measurement to continue simply does not re-arm it. DISARMING keeps the last reading readable, so a caller can stop measuring and still see the result. Safe to send while the transport is running: the tap allocates nothing, locks nothing and only reads the master mix, so the audio is bit-for-bit unchanged. Reversible: control.undo restores the armed flag (the readings themselves are surface memory and are not part of the inverse). |
| `meter.get_state` | no | The LIVE loudness of the master mix: gated integrated loudness (LUFS-I), momentary (LUFS-M, last 400 ms), short-term (LUFS-S, last 3 s), the loudest short-term window since the tap was armed, and true peak (dBTP) - the ITU-R BS.1770-4 / EBU R128 measures - measured by the engine's own meter from the periods it is rendering, plus whether the tap is armed and how many periods it has measured. Reads only; the tap is PASSIVE, so it never changes a sample. A reading is null (never a plausible number) while nothing measurable has been fed - which is also the state before the tap is armed: see meter.arm. |
| `meter.measure_file` | no | Measure a RENDERED FILE's loudness and true peak, now, from the file's own bytes: gated integrated loudness (LUFS-I), momentary, short-term and the loudest short-term window, true peak in dBTP, the EBU R128 verdict, and the file's own facts (sample rate, channels, frames, duration, size and sha256). The same BS.1770-4 meter the render path uses measures it, in bounded chunks, reading the file and writing nothing - so a caller can hash the file before and after and see it unchanged. 1 to 6 channels (mono and stereo exactly; 5.1 with BS.1770-4's channel weights); a file with more, or one with no frames, is refused typed. A silent file reports null readings, measured false and verdict NOT MEASURED. |

### midi (9 ids)

| command | mutating | description |
| --- | --- | --- |
| `midi.clients_list` | no | Read-only: every MIDI client and port the running client can see right now, gathered by the client's NAME, with each port's identity (the name without its address), its address, its direction, and whether an engine port holds a binding to it. This is the list a re-connection is resolved against: the device on a controller that vanished and came back appears under the SAME client name with a DIFFERENT address, and that is the pair of readings midi.reconnect_status reports the engine reacting to. 'engine_port' names the object that holds the binding (a trk-<n> id for a track's MIDI port), so a caller can see which track a device is bound to. With no MIDI backend open the client is the dummy one and the list is empty, which is reported as such rather than as an error. |
| `midi.device_list` | no | The MIDI client that is running, the client the config file selects, and every readable/writable port it exposes. With no MIDI backend open the client is the dummy one and both port lists are empty, which is reported as such rather than as an error. |
| `midi.learn_toggle` | no | Arm or disarm global MIDI learn - the mode the Edit > MIDI Learn menu item drives (SPEC A11: the menu action declares this command id and its slot invokes this command, so both the user and an agent arm the mode through one implementation). While the mode is armed, the next hardware control movement binds itself to the control the user last touched, and the mode then disarms itself. The armed flag is GUI/engine mode state, not project state, so no transaction is recorded and control.undo has nothing to reverse. |
| `midi.reconnect_arm` | no | Arm or disarm automatic MIDI controller re-connection. With no 'enabled' argument the mode is ENABLED (the default); 'enabled': false is its disarm. While enabled the engine re-establishes every remembered controller assignment whose identity comes back at a new address - the device that was unplugged and plugged in again, or whose controlling process exited and restarted. While disarmed the loss is still recorded and reported by midi.reconnect_status, but nothing is re-attached automatically; re-enabling takes effect at the next port-list poll (the ALSA-sequencer client's one-second inventory read). The mode is engine state, not project state: no transaction is recorded and control.undo has nothing to reverse. It is persisted to the config file's midi/reconnect key (not the project file), so it survives a restart, and 'persisted' reports what was stored. |
| `midi.reconnect_set` | yes | Bind the MIDI port of the track named by 'port' (a trk-<n> id) to a live MIDI controller port, named by 'name' (the exact full name midi.clients_list reports) or by 'identity' (the name half alone, "<client name>:<port name>", which is what survives the device being unplugged); 'direction' is "read" (the default, a controller feeding the track) or "write". 'detach': true removes the binding instead. This is the manual half of auto-reconnection: it establishes the binding the engine then remembers and re-establishes by itself, which midi.reconnect_status reports as an assignment. A binding is a property of the engine port and is serialized into the project as the <midiport> element's inports/outports attribute, so this command writes PROJECT state: one journal step is recorded and ONE control.undo puts the binding set back, through the same MidiPort::subscribeReadablePort call the write uses, so the live subscription and the serialized attribute are restored together. Refused typed when 'name' is not a port the running client lists, when 'identity' matches no port or more than one, when the track named by 'port' is not an instrument track, and when 'detach' names a binding the port does not hold. |
| `midi.reconnect_status` | no | Read-only: what the engine remembers about MIDI controller assignments and what happened to them. A controller is remembered by IDENTITY - the ALSA-sequencer client's NAME and the port's NAME, "<client name>:<port name>" - and never by the address in front of them ("<client>:<port>"), because that number is handed out when a client opens the sequencer and is different after a replug. So a device that is unplugged and plugged back in is re-attached without user action, and this command is where that is visible: 'assignments' lists every remembered binding with its identity, the full name it currently holds, whether that identity is live, whether it was LOST (the device went away and has not been seen since) and how many times the engine has re-established it. 'reconnects' and 'lost' are the session's totals. 'notice' says whether the RUNNING client class publishes port-list changes at all - "polled" for the ALSA-sequencer client, whose one-second inventory poll is what a re-connection is driven by, and "none" for every client class this build does not consume changes from, in which case the memory still records the loss but nothing can re-attach automatically. 'enabled' is the mode (midi.reconnect_arm) and 'persisted' is what the config file's midi/reconnect key holds. |
| `midi.retro_capture_arm` | no | Arm or disarm retrospective MIDI capture - the mode the Edit > Arm MIDI Capture menu item drives (SPEC A11: the menu action declares this command id and its slot invokes this command, so the user and an agent arm the mode through one implementation). With no 'armed' argument the mode is ARMED; 'armed': false is its disarm. While armed, the MIDI input threads keep the most recent events in one bounded ring per open client, and midi.retro_capture_to_clip writes that window into a clip; nothing is recorded while disarmed. Mode/engine state, not project state, so no transaction is recorded and control.undo has nothing to reverse. The state is persisted to the config file's midi/retrocapture key (not the project file) and re-applied by the GUI startup path, not by the capture object's constructor, which runs before main(); 'persisted' reports what was stored. |
| `midi.retro_capture_status` | no | Read-only: whether retrospective MIDI capture is armed, which MIDI client the instance is running, how many events its ring retains out of its capacity, how many events fell out of the window (overwritten) and how long the window is in ticks and in seconds (derived from the tick span and the song tempo). 'refused_snapshots' and 'paused_dropped' are the ring's own loss counters - both 0 in practice - and 'quiesced' says whether the MIDI thread acknowledged this read. This is how an agent tells whether a capture is armed without a GUI. |
| `midi.retro_capture_to_clip` | yes | Write the events retrospective MIDI capture has retained into a NEW MIDI clip on 'track', and return its clip-<n> id. Without 'track', the track of the clip selected in the control surface is used, and with nothing selected the song's first instrument track. The clip starts at the window's first tick and is 'length' ticks long, or - without it - the window rounded up to whole bars. The note matcher closes an unmatched note-on at the window's end and reports it in 'unmatched_ons': the capture never truncates silently. One journal checkpoint (the Track's, like clip.add) over the new clip, so ONE control.undo removes the whole capture; the inverse is clip.delete. Reversible through the ProjectJournal (Track checkpoint). |

### mixer (9 ids)

| command | mutating | description |
| --- | --- | --- |
| `mixer.add_channel` | yes | Append a mixer channel and return its new ch-<n> id. |
| `mixer.get_state` | no | Every mixer channel with its stable ch-<n> id, gain and routing. |
| `mixer.remove_channel` | yes | Delete a channel (the master channel is refused). |
| `mixer.route_remove` | yes | Remove the send from one channel to another - a regular routing with sidechain false (the default), or a sidechain send with sidechain true. Reversible: one undo step re-creates it with the amount and tap it had. |
| `mixer.route_to` | yes | Make a channel's output go to another channel's input (the channel's routing), at unity. Validated against the mixer's own feedback rule before anything is written. Reversible: one undo step. |
| `mixer.send_to` | yes | Create or adjust an auxiliary send from one channel to another, with an amount. A send INTO a bus is pre-fader by default, which is the engine's own rule. Reversible: one undo step. |
| `mixer.set_pan` | yes | Set a channel pan. Refused: this tree has no pan on a mixer channel. |
| `mixer.set_volume` | yes | Set a channel fader (0..2). Reversible through the ProjectJournal. |
| `mixer.sidechain_to` | yes | Create or adjust a SIDECHAIN send: the sender's signal is tapped at the given point and delivered to the receiver's sidechain input instead of being mixed into its output. tap_point is one of post_fader, pre_fx, pre_fader, post_fader_no_gain. Reversible: one undo step. |

### modulator (7 ids)

| command | mutating | description |
| --- | --- | --- |
| `modulator.create` | yes | Create a modulator: a timeline-locked LFO. Returns its modulator-<n> id. It drives nothing until modulator.target_set binds a parameter to it. Reversible through the ProjectJournal (an action checkpoint removes the modulator this command created). |
| `modulator.depth_set` | yes | Set one route's depth: the modulation amount as a FRACTION of the target's own range (-1..1). 0 leaves the route bound but driving nothing. The base value the parameter is modulated around is kept, so changing a depth does not re-read the parameter. Reversible through the ProjectJournal (the recorded undo step writes the previous depth back). |
| `modulator.get_state` | no | The modulation layer: every modulator with its source (shape, rate in Hz, phase, polarity, active) and every route it drives, with the route's address, its depth and whether it still resolves. 'driving' is the number of routes the audio thread will actually write - a route whose device is gone is reported, not hidden. |
| `modulator.rate_set` | yes | Set a modulator's LFO. 'rate' is in Hz and 'phase' is 0..1 where in its cycle the modulator starts; 'shape' is one of sine/triangle/square/saw and 'unipolar' makes the output 0..1 instead of -1..1. An argument the call omits keeps the value it had. Switching 'active' off hands every target back to its own value. Reversible through the ProjectJournal (the recorded undo step puts the previous source back). |
| `modulator.remove` | yes | Drop a modulator and every route it drives. Its targets are handed back to the values they had before it modulated them. Reversible through the ProjectJournal (the recorded undo step re-inserts the captured modulator at its own index, target list included). |
| `modulator.target_remove` | yes | Unbind one parameter from a modulator, by its target index as modulator.get_state reports it. The parameter is handed back to the value it had before it was modulated. Reversible through the ProjectJournal (the recorded undo step re-inserts the captured route at its own index). |
| `modulator.target_set` | yes | Bind a parameter to a modulator. 'channel', 'chain' and 'effect' address a device in a mixer channel's rack (fx-<n> order) and 'parameter' is its display name, exactly as rack.macro_target_add names one; 'depth' is the modulation amount as a FRACTION of that parameter's own range (-1..1), added to the value the parameter already has. Refused when the parameter does not resolve or is already driven, so a modulator never carries a route that can only fail. Reversible through the ProjectJournal. |

### note (18 ids)

| command | mutating | description |
| --- | --- | --- |
| `note.add` | yes | Add a note to a clip (key 0..127, ticks, velocity 0..200) and return its stable note-<n> id. Reversible through the ProjectJournal (MidiClip checkpoint). |
| `note.expression_clear` | yes | Drop a note's per-note MPE expression entirely, and the optional mpepitch/mpepressure/mpetimbre attributes with it - so the note serializes exactly as one that never carried expression. A no-op on a note that carries none is a typed refusal rather than a silent write. Reversible through the ProjectJournal (a MidiClip checkpoint). |
| `note.expression_get` | no | One note's per-note MPE expression, or - when 'note' is omitted - every note in the clip that carries one, with the count. 'has_expression' is the presence flag, so a captured-but-neutral note (every axis 0) is distinguishable from a note with no expression at all. |
| `note.expression_set` | yes | Set a note's per-note MPE expression (task #601): 'pitch' is a bend offset in 1/100 semitone (+-4800), 'pressure' is channel pressure 0..127 and 'timbre' is CC74 0..127. An axis the call omits keeps the value it had, and the note is marked as carrying expression either way. Reversible through the ProjectJournal (a MidiClip checkpoint). |
| `note.move` | yes | Move a note to an absolute tick position inside its clip. The move re-sorts the clip's note list, so the result reports the note's new id. |
| `note.probability_set` | yes | Set the chance, in [0, 1], that a note is played at all in a take (1 is the default and means "always"; 0 means never). The value is per note, so one clip can hold some 100% notes and some 50% notes, and it is rolled against the project's own MIDI seed once per note trigger. Reversible through the ProjectJournal (MidiClip checkpoint). |
| `note.random_seed_get` | no | The project's MIDI seed: the number every seeded roll of this project draws from, persisted in the project header as the "midiseed" attribute and written only when it is not 0. Read-only. A seed of 0 is the default of every project saved before MIDI depth existed and of every project that never touched it, so 0 means "this project rolls from the default", not "unset". |
| `note.random_seed_set` | yes | Set the project's MIDI seed: the PERSISTED half of the seed pair. Every later note.randomize that names no seed rolls from this value, and the save path writes it into the project header, so a take you liked is reproducible after a save and a re-open. The seed travels as a whole number in 0..2147483647 (the engine's is a 32-bit unsigned; a larger one cannot be spelled in this schema subset and is refused rather than truncated). Reversible: a recorded action step on the engine's own undo stack restores the previous seed, so Ctrl+Z and control.undo are one history. |
| `note.randomize` | yes | Roll a clip's notes with the engine's SEEDED randomisation: 'velocity_jitter' (0..1) multiplies each note's velocity by a factor in [1-j, 1+j] and 'position_jitter' (ticks, optional) moves each note by up to that many ticks, never before tick 0. Both draws are pure functions of the seed and the note's own identity (key, position, length at entry), so the SAME seed on the same starting notes reproduces the same take and a DIFFERENT seed produces a different one. The seed defaults to the project's persisted one; naming a 'seed' argument rolls from it without writing it to the project (note.random_seed_set is the verb that persists one). 'scope' is 'clip' (default) or 'selection'. The roll is multiplicative and drawn at entry, so a second call rolls on top of the first - the inverse is the clip's checkpoint, not another call. Reversible through the ProjectJournal (MidiClip checkpoint). |
| `note.remove` | yes | Delete a note from a clip. Reversible through the ProjectJournal (MidiClip checkpoint). |
| `note.resize` | yes | Set a note's length in ticks. Reversible through the ProjectJournal (MidiClip checkpoint). |
| `note.select` | no | Select notes in a clip (an empty list clears the selection). Control-surface state, not project state. |
| `note.slide_clear` | yes | Clear every slide flag in a clip (or in the current selection) as ONE edit and ONE undo step, and report how many were cleared and how many slide notes remain OUTSIDE the scope. Clearing is not deleting: the notes stay exactly where they are and become regular notes. Reversible through the ProjectJournal (MidiClip checkpoint). |
| `note.slide_set` | yes | Mark one note as a SLIDE note (FL-style portamento: its pitch glides from the previous note's key to its own over its length) or clear the flag. The flag is serialized per note as the optional "slide" attribute, so it survives save/load and a project that has none stays byte-identical to how it serialized before slide notes existed. What the flag does at play time is the engine's (NotePlayHandle::hasSlideGlide / slidePitchOffset); this verb only edits the note. Reversible through the ProjectJournal (MidiClip checkpoint), INCLUDING a first edit back to a regular note. |
| `note.transpose` | yes | Move every note of a clip (or of the current selection) by 'semitones'. A note whose target is outside the engine's 0..127 MIDI range is clamped there and is not counted as changed, which is NoteTransform::transpose's own documented rule; a 'semitones' value beyond +/-127 is refused rather than clamped to a no-op. 'scope' is 'clip' (default) or 'selection' - and a 'selection' scope with nothing selected is refused, because "I edited nothing" and "there was nothing to edit" are different answers. Reversible through the ProjectJournal (MidiClip checkpoint). |
| `note.velocity_offset` | yes | Add 'delta' to every note's velocity in scope, clamped to the engine's own 0..200 volume range (a note already at a bound does not move and is not counted as changed). The note the delta is added to is the CURRENT velocity, so applying it twice adds twice - there is no stored pre-edit velocity, and the inverse is the clip's checkpoint. 'scope' is 'clip' (default) or 'selection'. Reversible through the ProjectJournal (MidiClip checkpoint). |
| `note.velocity_scale` | yes | Multiply every note's velocity in scope by 'factor' (0..8), rounded and clamped to the engine's own 0..200 volume range. A factor above 1 is a crescendo, below 1 a diminuendo, and 0 silences the notes without deleting them; a note already at a bound is not counted as changed. It is multiplicative on the CURRENT velocity, so applying it twice multiplies twice. 'scope' is 'clip' (default) or 'selection'. Reversible through the ProjectJournal (MidiClip checkpoint). |
| `note.velocity_set` | yes | Set a note's velocity (0..200, the engine's note volume scale; the piano roll's own velocity dialog uses the same range). |

### oop (5 ids)

| command | mutating | description |
| --- | --- | --- |
| `oop.get_state` | no | Every device chain in the song with each device's out-of-process hosting resolved: the state (in-process / separate-process / client-exited / refused-no-client / refused-crash-loop), the client executable and its live pid, what the device plugin reports itself, whether it can be driven (mode choice, restart), and this session's record for that client (starts, exits, crashes, restarts, last exit code). Also the build's whole family table and every client executable seen. Read-only. |
| `oop.list_families` | no | The build's out-of-process family table: for every plugin family this build classifies, its availability (client-available / always-separate / no-client), the client executable when it has one, and the one-sentence reason when it does not - so a family that cannot be hosted out of process is refused by NAME rather than silently run in the wrong place. Read-only. |
| `oop.reset_crashes` | yes | Clear this session's crash count for one client executable ('client'), or for every one this session has started (no argument). This is the only thing that lifts a crash-loop refusal; it does not undo a crash, and the exits and the last exit code stay in the record. The count is per client executable, not per plugin instance. |
| `oop.restart` | yes | Re-host ONE device: a fresh client process through the plugin's own reload path, which is what a slot that lost its client needs. Reports the state and the pid before and after. Refused when the family ships no client executable, when the plugin implements no reload, and when that client executable is in a crash loop (the count is in the refusal) - a restart never feeds a crash loop. |
| `oop.set_mode` | yes | Choose the hosting mode of ONE device: 'in-process' (the plugin's own code in this process) or 'separate-process' (a client process, so a crash in it cannot take this one down). 'target' is trk-<n>/ch-<n> and 'plugin' is fx-<n> or 'inst'. Refuses with a typed reason when the family ships no client executable in this build, when the family has no in-process path to fall back to, when the plugin does not implement setHostingMode, or when that client executable is in a crash loop. The choice is stored, so it survives save and reload. |

### patcher (2 ids)

| command | mutating | description |
| --- | --- | --- |
| `patcher.get_state` | no | The patcher node graph of a target's effect chain: every node with the role a patch addresses it by ("input", "effect:<index>"), its type, its ports, its own parameters and whether it is prepared; the edges in node ids and in roles; the cached topological order the audio thread walks; the output node; and whether the wiring is the DERIVED one (linear, from the effect list) or an AUTHORED patch. Read-only. `editable` reports whether an edit can land at all, with the reason when it cannot. |
| `patcher.set_wiring` | yes | Re-wire a target's effect chain: 'edges' is the whole wiring ({from, to, from_port, to_port} objects whose ends are "input" or "effect:<index>"), 'output' is the node the host block leaves through (the last effect by default), and an empty or absent edge list restores the DERIVED wiring. The new graph is built off the audio thread and published under the audio engine's model-change guard, so the edit is never concurrent with the render (include/RoutingGraph.h's threading contract) and it survives the next plugin.load - the derived rebuild re-applies it. Refused, typed, with nothing written, when the chain does not render through its graph (see patcher.get_state's `editable`), when an edge would make a cycle, or when the output cannot be reached from the input. Reversible: the previous wiring, derived or authored, is restored by control.undo. |

### pdc (1 ids)

| command | mutating | description |
| --- | --- | --- |
| `pdc.report` | no | Plugin delay compensation as the mixer publishes it: the total latency to the master output, every channel's alignment point and the latency its own chain adds, the compensation applied at every send, and whether sidechain routing exists. Read-only. |

### plugin (19 ids)

| command | mutating | description |
| --- | --- | --- |
| `plugin.bypass` | yes | Switch a device instance off or on. This drives the same enabled control the rack's On/Off LED drives, so a bypassed device reports processing=false in dsp.get_state. Reversible through the ProjectJournal. |
| `plugin.host_chunking` | no | The chunking contract both plugin host paths keep, and what this instance's audio path has actually been asked for: per host, the process() calls that reached a loaded plug-in, the frames they asked for, the plug-in calls they were turned into, the requests that needed more than one chunk and the frames beyond the prepared block they carried, the largest request and the block size the last one was prepared with. 'chunks' > 'requests' with a non-zero 'frames_beyond_prepared_block' is a request that was chunked instead of truncated or over-run. Counters are process-wide and never reset; read-only. |
| `plugin.host_notes` | no | The note path the CLAP host keeps, and what this instance has actually carried: the note input ports (clap.note-ports) and the audio layout of the CLAP plug-in loaded last, plus the note events the MIDI route pushed, the ones the bounded queue (or a missing note port) refused, the ones put into a plug-in's input event list and the note-ONs among them. A plug-in with no note input port refuses every note and counts it here rather than hiding it. Read-only. |
| `plugin.list` | no | Every device this build can load: built-in effect and instrument modules plus the devices of the shipped hosting formats (LADSPA, LV2, VST3 and CLAP), each with a dev-<n> id that is deterministic for the binary. dev-<n> is a catalogue index, not a persisted project id, and 'loadable' says whether plugin.load accepts the entry. Format order is built-in, then LADSPA, then LV2, then VST3, then CLAP, so adding a host does not renumber the ids of the formats that were already there. |
| `plugin.load` | yes | Load a device by its dev-<n> id: an effect onto a track's device chain or a mixer channel's insert chain, an instrument onto an instrument track. Returns the new instance id (fx-<n>, or inst for an instrument). Headless-safe: it declares no 'requires' at all, because no editor is created here - neither a built-in plugin view nor an LV2 UI. An LV2 device whose bundle ships a GUI loads and is fully parametrisable through plugin.param_get / plugin.param_set on a display-less instance. |
| `plugin.param_get` | no | Read one device parameter by 'name' or by 'index'. 'plugin' is an fx-<n> instance id or 'inst' for the target track's instrument. The range reported is the engine's own model range. |
| `plugin.param_set` | yes | Set one device parameter by 'name' or by 'index' with the engine's own range enforcement: a value outside the model's min..max is refused with invalid_args rather than silently clamped. Reversible through the ProjectJournal. |
| `plugin.preset_list` | no | The preset files (*.xpf) available to a device: the user preset directory and the factory preset directory, the same two the product's browser reads. An effect's directory is the device's own name (its LADSPA label when hosted); an instrument's is its product preset folder. |
| `plugin.preset_load` | yes | Load a preset (*.xpf) into a device: the user preset directory is searched first, then the factory one. A document written for a different device is refused. |
| `plugin.preset_save` | yes | Save the current device state as a preset (*.xpf) in the product's user preset directory. An instrument writes the instrument-track preset document the browser loads; an effect writes the device's own state document, because this build has no separate effect-preset format. An existing preset of the same name is refused unless "overwrite":true. |
| `plugin.rescan` | yes | Run the plugin scan the factory already has (PluginFactory::discoverPlugins, the slot the GUI never re-invokes): re-read the cache, drop quarantined files, serve what the cache can still be trusted for, load what changed, and rewrite the cache. This is what APPLIES a quarantine edit. Reports the scan's own numbers before and after. Not reversible: the previous fingerprint record of a file the scan replaced is not restorable by any command. |
| `plugin.scan_cache_get_state` | no | The plugin scan cache and its quarantine list: the cache file and whether it is persistent and dirty, how many records it holds, the quarantine entries with their reasons and whether the files are still there, and the last scan's own report (files found, quarantined, served from cache, known-bad skipped, scanned, descriptors). Read-only. |
| `plugin.scan_cache_list` | no | Every record the scan cache remembers, sorted by path: the file's fingerprint (path, size, mtime), its status (has-descriptor / not-a-plugin / load-failed) and, for a plugin, the descriptor metadata the scan resolved. Read-only; this is the cache's contents, where scan_cache_get_state only reports how many there are. |
| `plugin.scan_cache_lookup` | no | What the scan cache remembers about one plugin file: whether a record exists for it, whether that record would still be SERVED (the file's size and mtime must both still match - `cached`), whether it is stale (a record exists and no longer matches - `stale`), and whether the quarantine list hides it. Read-only. |
| `plugin.scan_cache_quarantine_add` | yes | Quarantine a plugin file: add it to the scan cache's skip list, with a reason, and write the cache file. A quarantined file is not loaded and not offered to the device catalogue on the next scan (plugin.rescan applies it). ONE entry per path: adding a path that is already listed is refused rather than silently ignored. Reversible - control.undo dispatches the recorded plugin.scan_cache_quarantine_remove. |
| `plugin.scan_cache_quarantine_remove` | yes | Un-quarantine a plugin file: drop it from the scan cache's skip list, write the cache file, and report the reason the entry carried. The next scan loads the file again. Reversible - control.undo dispatches the recorded plugin.scan_cache_quarantine_add WITH the reason captured before the removal (a path-only re-add would lose it). |
| `plugin.state_load` | yes | Restore a device's state from a file written by plugin.state_save. A document written for a different device is refused, so a state file cannot be pushed into the wrong plugin. |
| `plugin.state_save` | yes | Write a device's state to a file path. An effect writes this fork's zenepluginstate document; an instrument ('inst') writes the product's own instrument-track preset document, the one the browser loads. An existing file is refused unless "overwrite":true, so a save cannot silently destroy a state file. |
| `plugin.unload` | yes | Remove a device instance (fx-<n>) from a track's chain or a mixer channel. REVERSIBLE: the device's own state document is captured before the removal and one control.undo re-instantiates the same plugin at the same index in the chain with its settings restored. The transaction's 'before' snapshot still carries the state XML, so a client that prefers to rebuild by hand can; a device whose state exceeds the capture cap reports itself as snapshot-only instead. |

### port (2 ids)

| command | mutating | description |
| --- | --- | --- |
| `port.get_state` | no | A device's audio-ports model: the input and output pin matrices, their channel counts and names, and the engine's own used-channel caches. Read-only. |
| `port.set_pin` | yes | Set one pin of a device's audio-ports matrix (the pin connector's own write, AudioPortsModel::Matrix::setPin). Reversible: control.undo re-dispatches this command with the value the pin held before the write. |

### project (11 ids)

| command | mutating | description |
| --- | --- | --- |
| `project.audible_diff` | no | Render two project files (through the built binary, as child processes) and report which bars of which track differ, with the RMS and peak difference per run of bars. This is the audible answer to `diff`: it names a bar and a track, not a note. Long-running: it renders both projects, track by track. differ=false means the renders matched bar for bar. |
| `project.conflicts` | no | Re-present the musical conflicts a project file carries: the marker comments the mmpz-git merge driver left behind, read back without re-running the merge. Each conflict names the track, the pattern, the note and the bar, and what each side did. Reads one file and writes nothing; conflict_count 0 means the file is not conflicted. |
| `project.diff` | no | Semantic, XML-aware diff of two LMMS project files: which elements were added, removed, changed or moved, named by their musical path. Reads both files and writes nothing. This is the diff the repository configures for .mmpz, so `git diff` and this verb show the same operation list. |
| `project.get_state` | no | Project file, modified flag, tempo and track count. |
| `project.hash_assets` | no | The same reference list as project.missing_assets, plus a sha256 and a size per reference that is on disk - the identity of the media, so 'the same sample, moved' is distinguishable from 'a different sample with the same name' before anything is rewritten. Also returns one digest over the whole reference set, so two projects that reference byte-identical media compare equal whatever the files are named. Reads only; a reference over the per-file cap (1 GiB) is reported unhashed rather than quietly skipped. |
| `project.merge` | no | Three-way (base, ours, theirs) merge of LMMS project files through the mmpz-git merge driver. Operates on FILES, not the running session: the merged document is written to the 'ours' path (which is what git expects of a merge driver). 'conflicts' is 0 for a clean merge and the number of musical conflicts otherwise; a conflicted file is marked in place with comments a human resolves and `project.conflicts` re-prints. Exit 2 from the driver (a merge it refuses to write) is an error here, never a silent success. |
| `project.missing_assets` | no | Every file a project file references - sample clips, AudioFileProcessor and SF2 instruments, session-view audio slots - and which of those references have nothing on disk at them. Reads the project FILE, so it needs no session and answers for a project that cannot be loaded; nothing is written. The list is empty for an intact project. Sample paths are resolved the way the engine resolves them, with one stated difference: 'local:' and an unresolvable legacy relative path resolve against the project file's own directory rather than the open project's. Copying the media into a portable bundle is NOT this verb (Bar 3). |
| `project.open` | yes | Load a project file into this running instance. |
| `project.relink` | yes | Point the references of a project file whose value is 'from' - as stored, or resolved to a path - at the file 'to', so the project finds its media again. 'from' is a value project.missing_assets reported; pass 'expect_sha256' to refuse unless the file really is that media (the hash project.hash_assets reported). dry_run previews the change and writes nothing. The document is re-serialised with the product's own serialiser and no other attribute, element or the root's name is touched. Reversible: one recorded action checkpoint holds the previous bytes of the project file, so one control.undo restores it byte for byte. The media is not copied anywhere - the portable-bundle half is Bar 3 and is not this verb. |
| `project.restore_revision` | yes | Restore a retained revision of a project file OVER the live file (policy 'keep-3': revision 0 is the revision the last save replaced). The live file is rotated in first, so the restore is itself recoverable. The session in memory is NOT reloaded: call project.open afterwards to work on the restored bytes. |
| `project.save` | yes | Save the session. With no path, saves over the project's own file. Before the write, the file being replaced is rotated into the named 'keep-3' revision set (<file>.rev0..rev2, each capped at 8 MiB), so the previous revision is recoverable through project.restore_revision - and control.undo dispatches exactly that for the save it recorded. |

### rack (12 ids)

| command | mutating | description |
| --- | --- | --- |
| `rack.add_chain` | yes | Append an empty parallel chain to a channel's rack and return its index (chain-<n>). Reversible through the ProjectJournal (the recorded undo step removes the chain this command created). |
| `rack.get_state` | no | One mixer channel's rack: every chain with its device count, the chain selector, and every macro and key/velocity zone with its targets. 'channel' is a ch-<n> id. |
| `rack.macro_add` | yes | Add a macro to a channel's rack: a named scalar (0..1) that drives parameters bound with rack.macro_target_add. Returns its macro-<n> id. Reversible through the ProjectJournal (an action checkpoint removes the macro this command created). |
| `rack.macro_remove` | yes | Drop a macro from a channel's rack, targets included. Reversible through the ProjectJournal (the recorded undo step re-inserts the captured macro at its index). |
| `rack.macro_set` | yes | Set a macro's value (0..1) and drive every bound parameter through its own window in one step. Parameters whose device or name no longer exists are skipped and counted ('skipped'), never guessed at. Reversible through the ProjectJournal (one action checkpoint restores the macro and every parameter it wrote). |
| `rack.macro_target_add` | yes | Bind an existing parameter to a macro: 'chain' and 'effect' address a device in the rack's chains (fx-<n> order), 'parameter' is its display name as plugin.param_get reports it, and 'low'/'high' are the window as a fraction (0..1) of the parameter's own range. Refused when the parameter does not resolve, so a macro never carries a target that can only fail. Reversible through the ProjectJournal. |
| `rack.macro_target_remove` | yes | Unbind one parameter from a macro, by its target index as rack.get_state reports it. Reversible through the ProjectJournal (the recorded undo step re-inserts the captured target at its index). |
| `rack.remove_chain` | yes | Drop a parallel chain from a channel's rack. Chain 0 is the channel's own effect chain and is refused. The removed chain's effects and their state are captured in the transaction's before-state. |
| `rack.set_selected` | yes | Choose the chain a channel's rack routes to: a chain index, or -1 for parallel (every chain runs and the outputs sum). Reversible through the ProjectJournal (an action checkpoint restores the previous selection). |
| `rack.zone_add` | yes | Add a key/velocity zone to a channel's rack: an inclusive key range (0..127) and an inclusive velocity range (0..200) mapped to one of the rack's chains, with an optional sample reference. Returns its zone-<n> id. Reversible through the ProjectJournal (an action checkpoint removes the zone this command created). |
| `rack.zone_remove` | yes | Drop a zone from a channel's rack. Reversible through the ProjectJournal (the recorded undo step re-inserts the captured zone at its index). |
| `rack.zone_resolve` | no | Ask which of a channel's rack zones a note falls into: the first zone, in the order it was added, whose key range and velocity range both contain (key, velocity). Read-only. NOTE: nothing in this build consults a zone while a note plays - see docs/KNOWN-LIMITATIONS.md. |

### record (15 ids)

| command | mutating | description |
| --- | --- | --- |
| `record.arm_track` | yes | Arm one record route: start writing the interleaved input channel it selects into a 24-bit WAV, and journal the take beside it (the take journal is what makes a crashed capture recoverable - record.journal_begin's side file, written here by the recorder's own arm()). 'route' is the route index record.get_state reports; 'input_channel' defaults to the route's own index and must be inside [0, input_channel_capacity) - the FRAMES the route records come from the engine's input path, so with no capture device open and no frames staged the take is silent however the route is armed (record.input_get_state says which of those is true). 'file' must be absolute and defaults to zene-take-route<N>.wav beside this instance's recovery file. Reversible: the recorded inverse is record.disarm_track, which stops the capture and retires the journal; the take file itself is left on disk, like any recording you keep. |
| `record.disarm_all` | no | Stop every armed record route in one call and report the whole recorder afterwards. Each route's take is flushed and closed and its journal retired, exactly as record.disarm_track does for one. Not reversible: every route's take is a file that stays on disk, and re-arming starts a new one. |
| `record.disarm_track` | no | Stop one record route: the disk-writer is drained and joined, the WAV is closed, and the take journal is RETIRED - a clean stop leaves no journal, which is what makes 'there is a journal' and 'the capture died' the same fact (include/RecordingJournal.h). The take file stays on disk. This is record.arm_track's recorded inverse, so control.undo after an arm dispatches it. Not reversible itself: re-arming writes a NEW take rather than restoring this one, and there is nothing the engine could hand back. |
| `record.get_state` | no | Every record route the engine prepared, and the input path that feeds them. A route is one capture stream: an arm flag, the interleaved input channel it reads, the take file it writes, the take JOURNAL beside it, and the frame counters (pushed / recorded / journalled / overflow / write errors). The engine prepares 16 routes, and each may select ANY of the configured input channels - that pair of numbers (route_count and input_channel_capacity) is the 'arbitrary input count / multiple simultaneous inputs' feature row 64 asks for. `input` is record.input_get_state's own report, including `input_frames_staged` and `wide_frames`: both are 0 under a backend with no capture path, which is what 'a record route takes no inputs' measured. Read-only. |
| `record.input_get_state` | no | The engine's capture-IN path, asked directly: what the configuration asks for (device, channel COUNT, which pair of captured channels rides the stereo bus), what this backend actually did with it (capture_capable, capture_open, capture_reason, the channels and rate the device granted), and what the two input stages hold now (bus_frames on the stereo bus, wide_frames/wide_channels on the N-channel stage, plus the staged and dropped counters). 'capture_capable: false' means this instance's backend has no capture path at all; 'capture_capable: true, capture_open: false' means it has one and the device refused - capture_reason carries the device's own message. Read-only, and the honest place to look before blaming a silent take. |
| `record.input_set` | yes | Set the capture input path: 'device' (empty = the playback device), 'channels' (1..32 - the ARBITRARY input count of feature row 64), and 'left'/'right' (which two captured channels the stereo engine bus carries, so an interface's third and fourth input can be what the rest of the engine hears). Fields not given keep their current value. The four keys are written to the config file under `audioinput`, exactly as the settings dialog writes its own, and 'restart_required' is true: the backend opens the device at start and the recorder's routes are built when the engine is, so the next start is when this takes effect - there is no live device swap to pretend about. Reversible: the recorded inverse is this command with the previous plan, and control.undo dispatches it (the settings.set precedent, which records a command inverse for the same kind of config write). |
| `record.journal_begin` | yes | Start a journalled capture: write the take journal (<take>.rec-journal) that makes a recording recoverable if this process dies before the capture stops cleanly. `take` must be absolute (a relative path names a different file in the next process). Write the journal, then arm the recorder against the same path, then keep it current with record.journal_update. Reversible: the recorded inverse is the paired command (record.recovery_discard when no journal existed, record.journal_begin with the previous contents when one did), which control.undo dispatches. |
| `record.journal_finish` | yes | End a journalled capture CLEANLY: remove the take journal so the next start does not offer this take as a crashed recording. This is what the recorder's own disarm() does - a clean stop leaves no journal, which is why 'a journal is there' and 'the capture died' are the same fact. Reversible: the recorded inverse is record.journal_begin with the journal that was removed. |
| `record.journal_update` | yes | Record how much of the take had reached disk at the last flush ('frames_on_disk'), which is the count the recovery offer reports as guaranteed. It must not move backwards: the journal only ever records that MORE of the take reached disk. The recorder's own disk-writer does this once per second of audio (RecordingJournal::UpdateIntervalFrames), which is the whole of the lag bound. Reversible (paired command). |
| `record.recovery_discard` | yes | Refuse an interrupted capture's offer: remove its take journal so the next start stops offering it. THE TAKE'S WAV IS NEVER DELETED - it stays on disk at the take path the transaction's before-state names, which is the documented fallback because this command has no inverse (control.undo fails, typed, naming that path). |
| `record.recovery_get_state` | no | Every capture an abnormal exit left behind in `dir` (default: this instance's working directory): one entry per in-progress take journal whose WAV still exists. Each entry reports frames_journalled (what the journal recorded at its last update), frames_in_file (measured from the take's RIFF header, falling back to the file's real length when a crashed header was never updated) and frames_recoverable (the SMALLER of the two - the guaranteed count). The bound travels with every entry: the journal lags by up to one second of audio, and up to 65536 frames still in the recorder's ring at the crash are gone for good. Writes nothing. |
| `record.recovery_restore` | yes | Take an interrupted capture's offer: report the material the journal recovered and mark the journal `restored`, so the next start stops offering it. The take's WAV is NOT moved, rewritten or deleted - the material stays exactly where the crash left it, and the result says so (`audio_untouched`). WHAT 0.3.0 DOES NOT DO: bring the take into the session as a clip. No command in this release imports an audio file onto a track, so that half is deferred and named in `next_step` (docs/KNOWN-LIMITATIONS.md). Reversible: the recorded inverse is record.journal_begin with the journal that was restored. |
| `record.retro_capture_arm` | no | Arm or disarm retrospective AUDIO capture: while armed, the engine keeps the most recent frames of its INPUT BUS in one bounded ring, so the take you did not press record for can still be written with record.retro_capture_to_take. With no 'armed' argument the mode is ARMED; 'armed': false is its disarm. Nothing is recorded while disarmed, and the cost then is one relaxed atomic load per rendered period. The ring is allocated once, when the engine is built ('capacity_frames' in the reply is that allocation, 1048576 frames); a frame that falls out of it is counted in 'overwritten_frames' and cannot be recovered. Mode/engine state, not project state, so no transaction is recorded and control.undo has nothing to reverse (the midi.retro_capture_arm precedent, verb for verb). |
| `record.retro_capture_status` | no | What retrospective AUDIO capture is holding: whether it is armed, the ring's capacity in frames and in SECONDS, how many frames are retained right now and how many seconds that is, how many frames have already been overwritten by later audio (the window's bound, as a measurement), frames dropped because a snapshot was in flight, and the count of copies that could not obtain a consistent window. Read-only. |
| `record.retro_capture_to_take` | no | Write the retained window to a 24-bit STEREO WAV - the take that was already played when this command ran. The window is copied under the ring's publication handshake, so the copy is never torn even with the engine running, and the file is written off the audio thread. Refused, typed, when the window holds no frames: a take with no audio in it is not a take. 'file' must be absolute and defaults to zene-retro-take.wav beside this instance's recovery file. NOT reversible and NOT inserted into the session: the file it writes is yours to keep, and turning it into a clip is the project's own load path, not this command's (docs/KNOWN-LIMITATIONS.md). |

### render (2 ids)

| command | mutating | description |
| --- | --- | --- |
| `render.render` | no | Render the current session to a file and return its hash (headless). Whole project by default; pass start_ticks and end_ticks TOGETHER to render only that span of the song (a selection), in which case the span is rendered exactly - no tail bar and no loop repetition - and the reply reports the range and the frame count. The render is started with the export settings in force: the preset applied through export.preset_apply (sample rate, bit depth, stereo mode), or the render path's own defaults when none is applied. Runs in a CHILD process, so the declared bound applies: this surface does not answer - not even control.ping - until it finishes (docs/KNOWN-LIMITATIONS.md). |
| `render.stems` | no | Export every unmuted track to its own file in an absolute directory: one stem per track, post-fader and post-effects, including that track's own sends and their tails, each rendered to the project's length plus 'tail_bars' bars (default 1, the whole-project render's own convention). Returns the new file names. NOTE the declared bound: the render runs in a child process and this surface does not answer - not even control.ping - until it finishes (docs/KNOWN-LIMITATIONS.md). |

### revisions (3 ids)

| command | mutating | description |
| --- | --- | --- |
| `revisions.compare` | no | Compare two revisions of a project, or one revision against the file as it is on disk now (the id 'live', which only this command accepts - revisions.list does not report the working file as a revision). The result reports each side's id, source, timestamp, size and sha256, whether the two are byte-identical, and a STRUCTURAL comparison of the two documents: how many elements of each tag each side holds, the element totals and the tags that differ. That is a summary, not a semantic diff - the musical diff of two project documents is tools/mmpz-git's (`mmpz-git diff`), outside this process. Read-only. |
| `revisions.list` | no | The project's revision timeline: every revision this machine still holds, newest first, each with the SOURCE it came from ('rotation' - the keep-3 revision set project.save rotates, '<file>.rev0..rev2'; 'backup' - the '<file>.bak' a save from the interface writes; 'autosave' - the periodic recover.mmp and the recover.mmp.bak it replaced; 'git' - the commits that touched the file where the project lives in a git repository), its timestamp (UTC; an autosave reports its sidecar's recorded savedUTC), its size and its sha256. Read-only: nothing is written and no new store is created. The `id` each entry carries is what revisions.compare and revisions.restore take. The `git` half is bounded and optional: 'include_git' false, no git on the machine, or no repository, and the `git` object says which - without failing the list. |
| `revisions.restore` | yes | Restore one revision of the project OVER the file on disk, by the `id` revisions.list reports (rev0..rev2, 'backup', 'autosave', 'autosave_prev' or 'git:<sha>'). The live file is rotated into the keep-3 revision set BEFORE it is replaced, so the restore is itself recoverable - control.undo puts the replaced file back, or, when there was no file on disk yet, removes the one this call created. Refused, typed and without writing anything, when the id names no revision, when the source exceeds the policy's 8 MiB per-revision cap, or when the live file does (its rotation would have to be bounded by the same cap, and a truncated project is a corrupt revision). The session in memory is NOT reloaded: call project.open afterwards to work on the restored bytes. |

### roll (1 ids)

| command | mutating | description |
| --- | --- | --- |
| `roll.get_state` | no | The piano-roll view of a clip: the clip and every note with its position, length, key and velocity. Without 'clip', the selected clip is used. |

### routing (1 ids)

| command | mutating | description |
| --- | --- | --- |
| `routing.get_state` | no | The routing graph a target's signal is processed through: the nodes and their type names, the connections, the cached topological order the audio thread walks, the output node, and - for a mixer channel - its rack's graph. Read-only. |

### safestart (4 ids)

| command | mutating | description |
| --- | --- | --- |
| `safestart.acknowledge` | yes | Accept the offer of a normal start: write the acknowledgement the NEXT launch consumes, so it loads with third-party plugins enabled. Keeps the marker and does not change this session (instances already skipped were never created). Refuses when there is no marker. Not reversible - no function in this engine removes the acknowledgement except safestart.clear, which removes the marker with it. |
| `safestart.clear` | yes | Clear the crash marker: delete the marker and the acknowledgement, so the next launch is a normal one, and leave safe-start mode in this session. The verb for 'the cause is known and fixed'. Refuses when there is neither file, so a call that would change nothing writes nothing. Not reversible - the marker's own record of the crashed session is gone; control.undo names the fallback. |
| `safestart.get_state` | no | Safe-start mode's state: whether the module is installed, the crash marker and the acknowledgement as files (path, existence, size, time), whether a marker says the previous run did not exit cleanly, whether THIS session started safe, how many sessions in a row have, the session-scoped skip switch, every plugin instance the mode skipped and why, the directories the third-party classification treats as this build's own, the previous session's own record and the offer of a normal start. Read-only, and it answers in every configuration. |
| `safestart.set_skip` | yes | Turn the load-time skip of third-party plugin INSTANCES on or off for THIS session - the session-scoped half of the predicate Plugin::instantiate consults. Does not touch the marker: with it off, a project loaded now loads its third-party plugins when it is opened (already-skipped instances are not created by this call). Re-armed by the next session. Not reversible - process-scoped mode state, no journal checkpoint, and nothing puts an already-skipped instance back. |

### scale (5 ids)

| command | mutating | description |
| --- | --- | --- |
| `scale.get_state` | no | What this group is resolving against: the context's root, scale name, degrees, pitch classes and mask. Naming 'root' and/or 'scale' answers for those instead and says so ('resolved_from_arguments'), without changing the context. A 'clip' argument adds the analysis an arranging caller wants - how many of its notes are in the scale and how many are out, counted with the engine's own predicate (NoteTransform::matches) - so "this part is in key" is a measurement. A context with no scale set yet reports an empty 'scale', an empty mask and scale_set:false. Read-only. |
| `scale.list` | no | Every scale this engine knows and the twelve keys, with each scale's degrees (semitone offsets from the root), its pitch classes, its twelve-character membership mask (index 0 = C, so "101011010101" is the major scale: C D E F G A B, ChordTable's own scale row) and its size. The vocabulary is the scales are the ones with more than six degrees; 'include_chords' adds the chord entries too. 'root' (a key index 0..11 or a name such as "C#"/"Db") shifts every pitch class and every mask; absent, the group's own context root answers. Read-only. |
| `scale.root_set` | yes | Set the CONTEXT's root - the key every later scale.snap_notes and every scale.get_state resolves against when it names no root. Accepts a key index 0..11 or a name ("C", "c", "C#", "Db", "D♭", or the piano roll's "C# / Db" spelling; an octave suffix is ignored, because a root here is a pitch class). An unknown name is refused rather than defaulted to C. Reversible: a recorded action step restores the previous root. The context is this group's own process state and is deliberately not serialized - it is NOT the piano roll's key selector (docs/KNOWN-LIMITATIONS.md). |
| `scale.set` | yes | Set the CONTEXT's scale by NAME - a scale this engine knows, from scale.list (e.g. "Major", "Minor", "Major pentatonic"). An empty string CLEARS the context's scale, after which scale.snap_notes refuses instead of guessing. A name ChordTable does not carry is refused typed (NotFound), not resolved to an empty scale. Reversible: a recorded action step restores the previous scale. The context is this group's own process state and is deliberately not serialized - it is NOT the piano roll's scale selector (docs/KNOWN-LIMITATIONS.md). |
| `scale.snap_notes` | yes | Move every note of a clip (or of the current selection) whose pitch class is OUTSIDE the scale to the nearest in-scale pitch, clamped to the MIDI range, with a tie resolving downward; notes already in the scale are left alone (NoteTransform::snapToScale, the engine's own rule). The scale is the CONTEXT's unless this call names 'root' and/or 'scale', and a call with no scale anywhere is REFUSED rather than snapped to a guessed key. The report says how many notes moved and how many are still out of scale afterwards, so "it is in key now" is a measurement. Reversible through the ProjectJournal (MidiClip checkpoint) - and NOT by re-running the snap, which is why the checkpoint is the inverse. 'scope' is 'clip' (default) or 'selection'. |

### script (3 ids)

| command | mutating | description |
| --- | --- | --- |
| `script.list` | no | The Lua scripts this build ships (data/scripts, resolved through the app's own 'data:' search path) with their sizes and hashes. Reads the filesystem, so it answers before the engine is up. |
| `script.run` | yes | Run a Lua script (docs/specs/SPEC-lua-api-v0.md) in this running instance - the same ScriptEngine the run-and-exit `--run-script` CLI flag drives, on its own worker thread with the engine apply side pumped on the UI thread. Returns the Lua log lines, or a typed error carrying them. 'budget' overrides the per-invocation instruction budget (default from the engine) for this call only. |
| `script.set_memory_budget` | yes | Set the block of memory a script run may hold, in bytes (CODE-6): the budget beside the instruction budget. The Lua state is opened with an allocator that refuses any allocation past this cap, so a runaway script fails with 'memory budget exceeded' and its run is aborted - it cannot grow until the machine kills the process. Refuses a value outside [min_memory_budget, max_memory_budget]; there is no 'unlimited' through this surface. Reversible: one control.undo restores the previous cap (script.run reports the budget and what the last run measured). |

### session (17 ids)

| command | mutating | description |
| --- | --- | --- |
| `session.arrangement_record_arm` | no | Arm (or disarm) Arrangement Record: while it is armed the audio thread pushes one event per session launch and per stop into the engine's ring, at the tick the transition fired on. Disarming KEEPS what the ring already carries - the performance is landed, not dropped - and a reset (session.stop_all, session.back_to_arrangement) records the stop of every slot it ends, so a stopped performance has no launches left open. Mode state, not project state: no transaction is recorded. |
| `session.arrangement_record_land` | yes | Land the recorded performance: pair every recorded launch with the stop that ended it and create ONE arrangement clip per pair on that column's song track, at the recorded ticks. REFUSED - consuming nothing - while any recorded start is still open, because a half-landed performance would lose starts or invent their ends; stop the session first (session.stop_all, or session.back_to_arrangement). Reversible: one Track journal checkpoint per touched track, so one control.undo takes the whole pass back. The clip's CONTENT is not copied from the session slot (there is no session-clip playback path in 0.3.0): the reply names the slot's pattern reference per clip. |
| `session.arrangement_record_status` | no | Read Arrangement Record back: whether the tap is armed, how many events it has recorded, how many it dropped because the ring was full, how many are waiting, how many clips a land pass would write, and how many launches are still playing (which is what a land pass refuses on). 'pending' is a bounded estimate - the two ring indices are read separately, so a push landing between them is not counted. Read-only. |
| `session.back_to_arrangement` | no | The Back-to-Arrangement switch: end the session playback and hand every track back to its arrangement content (SPEC A1 mutual exclusivity - a track plays its session content or its arrangement content, never both). It is one atomic request, the same engine operation session.stop_all makes, and it exists as its own id because it is the ARRANGEMENT's verb: the reply reports the recorded performance, which this does NOT drop - the ring is landed afterwards by session.arrangement_record_land. Not a project edit: no transaction is recorded. |
| `session.clear` | yes | Empty the whole Session View: no grid, no slot, no scene override, default quantisation. A project that never used the session then re-saves byte-identically (the <session> block is dropped). Reversible through the ProjectJournal. |
| `session.clear_slot` | yes | Empty one grid cell, whatever it held. Reversible through the ProjectJournal (action checkpoint on the <session> block). |
| `session.follow_get_state` | no | Read the Follow Action engine back: which cells are armed (the count and the bit(track * 8 + scene) mask), how many actions have fired, what the newest fire did (its outcome, the chain entry that produced it, the scene it addressed and the tick it was scheduled for), and - when track and scene are given - that cell's own state and the chain's action time. The mask is a decimal string because a 64-bit mask does not survive a JSON number. Read-only. |
| `session.follow_set` | no | Arm (or clear, with enabled: false) one cell's Follow Action chain: the engine stores the chain and evaluates it while the cell plays, firing at the chain's action time. Without 'actions' the cell's own persisted chain is used, which is what session.set_slot wrote. Refused rather than truncated when the chain is longer than the engine's table. 'queued' is the model thread's answer; armed_cells and the rest are the engine's published reading, up to one audio period later. Not a project edit: no transaction is recorded. |
| `session.get_state` | no | The Session View: grid dimensions, the global launch quantisation, every non-empty clip slot with its launch settings, every scene override, and the launch engine's read-back (completed launches, the grid line the newest starts fired on, how many clips started on that one line). |
| `session.launch_scene` | no | Trigger every non-empty clip in one scene row at once and report the tick each was scheduled for. `in_sync` is true when every launched clip resolved to the SAME grid line, which is what a scene launch means; `sync_tick` names that line. Not a project edit: no transaction is recorded. |
| `session.launch_slot` | no | Trigger one clip slot: queue a launch, quantised to the slot's own (or the supplied) boundary, and report the tick it was scheduled for. The engine records the start when the clock reaches that line. Not a project edit: no transaction is recorded. |
| `session.set_grid` | yes | Resize the Session View grid (column = a song track, row = a scene). Existing cells keep their content and position; new cells are empty. Reversible through the ProjectJournal (action checkpoint on the <session> block). |
| `session.set_quantisation` | yes | Set the session's global launch quantisation - the grid line every slot whose own quantisation is 'global' waits for. Reversible through the ProjectJournal (action checkpoint on the <session> block). |
| `session.set_scene` | yes | Set one scene's name and/or its tempo and time-signature overrides; a field that is supplied is written and its override switched on, a field that is not is left alone. Reversible through the ProjectJournal (action checkpoint). |
| `session.set_slot` | yes | Define one grid cell: a MIDI clip ('type':'midi' + 'pattern'), an audio clip ('type':'audio' + 'source'), or empty ('type':'empty'); plus its launch mode, launch quantisation and playback settings. Only the fields supplied are written. Reversible through the ProjectJournal (action checkpoint on the <session> block). |
| `session.stop_all` | no | Drop every launched slot on the audio thread's next period, whichever scene or clip it came from. It is one atomic request, so it is safe while the transport runs. Not a project edit: no transaction is recorded. |
| `session.stop_slot` | no | Stop one clip slot at its next quantisation boundary. A slot that never started stays idle, so this is safe to call unguarded. Not a project edit: no transaction is recorded. |

### settings (2 ids)

| command | mutating | description |
| --- | --- | --- |
| `settings.get` | no | Read one UI/engine setting by the key the config file uses, '<class>/<attribute>' (for example audioengine/audiodev, ui/saveinterval, app/configured). 'value' is the config file's own string form and 'present' says whether the key is set at all rather than defaulted. |
| `settings.set` | yes | Write one UI/engine setting by its config-file key and persist the config file, exactly as the settings dialog does on OK. 'value' is the config file's own string form (booleans are "1"/"0"). Settings the engine reads at startup (audioengine/audiodev, samplerate, ...) take effect on the next start. The write is recorded with the previous value as its inverse, but ConfigManager is not journalled, so control.undo cannot reverse it. |

### telemetry (2 ids)

| command | mutating | description |
| --- | --- | --- |
| `telemetry.consent` | no | Open the telemetry consent screen - the Help menu's "Telemetry - what we send..." action, with the live preview of the exact payload. A13/UnattendedRun: this one is a modal screen, so it declares `requires: display, human` and every unattended caller is refused, typed, before the handler runs - an agent can never consent on the user's behalf. Read the state with telemetry.status. |
| `telemetry.status` | no | Read-only: whether telemetry is compiled in, whether consent is on, which groups are on, and the exact payload submit() would send (the same bytes the consent screen previews). This is the agent's way to see the telemetry state without being able to change it - consent stays a human act. |

### track (18 ids)

| command | mutating | description |
| --- | --- | --- |
| `track.add` | yes | Append a track of the given type (default instrument) and return its stable trk-<n> id. Not reversible: see the transaction's mechanism. |
| `track.folder_get_state` | no | One folder addressed by its trk-<n> id: its mode (group or routing), the pinned and collapsed flags, the mixer channel it owns in routing mode, and every child with the channel that child is on. track.list, track.get_state and arrangement.get_state also carry each track's `folder`, so the relation is readable from the arrangement view of the song as well. Writes nothing. |
| `track.folder_set_collapsed` | yes | Collapse or expand a folder. This is the flag the track list's collapse would read; 0.3.0 persists it and drives it from here only (no chevron is drawn). Reversible through the ProjectJournal (the folder's own Track checkpoint). |
| `track.get_state` | no | One track addressed by its trk-<n> id: the number the track was given at creation, which it keeps until it is deleted. A malformed id is invalid_args; a well-formed id naming no live track is not_found. Addressing is scoped to the SONG container, so a track inside a nested container is not reachable by id. |
| `track.list` | no | Every track in the song container, with its stable trk-<n> id. The id is assigned at creation and persists in the project file. Addressing is scoped to the SONG container: a track inside a nested container (the <trackcontainer> a pattern track carries) is not reachable by id, exactly as it is not addressable by index. |
| `track.move` | yes | Reorder a track: put it at `index` in the song's track list (0-based, the index track.list prints). Reversible through one recorded action step - control.undo puts the track back where it was, and the redo sends it forward again. Refuses an index outside the song rather than clamping it. A track that is not in the song container is refused too: the engine's own reorder erases-then-inserts, so a foreign pointer would be DUPLICATED into the song. |
| `track.remove` | yes | Delete a track together with its clips. dry_run previews it. |
| `track.rename` | yes | Rename a track. Reversible through the ProjectJournal (Track checkpoint). |
| `track.set_arm` | yes | Arm or disarm a song track for recording. Arming starts a capture on the record route the track's position in the song maps to: the interleaved input channel named by 'input_channel' (default: the route's own index) is written to a 24-bit WAV and the take is journalled beside it, so a crash mid-take is recoverable (record.recovery_get_state). 'file' must be absolute and defaults to zene-take-trk<N>.wav beside this instance's recovery file. The frames a route records come from the engine's input path, so a route armed while nothing is staged records silence: record.input_get_state reports whether a capture device is open and how many frames are staged ('capture_capable', 'capture_open', 'capture_reason', 'input_frames_staged'). Arming is reversible - the recorded inverse is track.set_arm with armed: false, which stops the capture and retires the journal and leaves the take on disk; disarming is not, and says so in its own record, because re-arming starts a NEW take. |
| `track.set_folder` | yes | Put a track into a folder, or take it out of one with an empty 'folder' (the container root). The folder relation is an attribute on the child's own <track> element and the child keeps its row in the flat track list, so this changes membership only. A folder is a track of type folder (track.add type=folder); a target that is not one is refused, typed, and a relation that would close a cycle is refused too. |
| `track.set_mute` | yes | Mute or unmute a track. Reversible through the ProjectJournal (the track's mute BoolModel checkpoint). |
| `track.set_pinned` | yes | Pin or unpin a folder. This is the flag a non-scrolling pinned strip would read; 0.3.0 persists it and drives it from here only. Reversible through the ProjectJournal (the folder's own Track checkpoint). |
| `track.set_routing` | yes | Switch a folder between its two modes: false is GROUP - the organisational mode and the default - where each child keeps its own mixer channel, and true is ROUTING, where the folder takes a mixer channel of its own and every child's output is summed through it. Refused, typed, when the track is not a folder or when a routing folder has no children to sum. |
| `track.set_solo` | yes | Solo or unsolo a track. This is the product's whole solo action: the soloed track is unmuted and the others muted (TrackView drives that from the solo model), and the result carries the mute/solo state of every track. |
| `track.visibility_set_apply` | yes | Make exactly the members of a named visibility set visible and hide every other track of the song. A visibility flag is a VIEW flag: it mutes nothing and changes no render (the registered transcript proves that with a negative control). An unknown name is not_found, typed. |
| `track.visibility_set_list` | no | Every named visibility set this project holds, with its members and which one is active. Writes nothing. Each track's own `visible` flag is reported by track.list, track.get_state and arrangement.get_state. |
| `track.visibility_set_remove` | yes | Delete a named visibility set. The tracks' own visible flags are NOT changed: a set is a saved selection over them, and the flags are the project's own state. An unknown name is not_found, typed. |
| `track.visibility_set_save` | yes | Create or replace a named visibility set: a named, id-based list of tracks that track.visibility_set_apply makes visible. Every named track must be live (a well-formed id naming none is not_found). Saved with the project, so a set survives project.save / project.open. |

### transport (13 ids)

| command | mutating | description |
| --- | --- | --- |
| `transport.get_state` | no | Playback position and transport flags. |
| `transport.play` | no | Start playback of the current song. |
| `transport.punch_clear` | yes | Disarm the punch region and forget it, leaving the timeline in the state a project that never punched has (which is also what gets written to the project file: the punch attributes are omitted). Refused, typed, when there is no region to clear. Reversible through the ProjectJournal (Timeline checkpoint: the recorded inverse is transport.punch_set with the region that was cleared). |
| `transport.punch_get_state` | no | The transport's punch region: its range, whether it is armed, and `punch_active` - Timeline::punchCapturesAt() at the current play position, i.e. whether a capture starting here would be inside the region. Seek inside and outside the range to see the gate answer both ways. Writes nothing. |
| `transport.punch_set` | yes | Set the transport's punch region - the tick range [start, end) that recording captures inside - and arm it (pass "enabled": false to set the range without arming it). An empty range is refused. The region is project state: it is written with the timeline (`punch0pos`/`punch1pos`/`punchstate` on the <timeline> element, and only when it is set, so a project that never punches saves the bytes it always has) and one control.undo takes it back off through the Timeline's own checkpoint. Reversible through the ProjectJournal. The AUDIO-SIDE GATE IS NOT WIRED in 0.3.0: the region and the predicate are real, and no capture path in this build consults them yet (docs/KNOWN-LIMITATIONS.md). |
| `transport.seek` | yes | Move the play head to an absolute position in ticks. |
| `transport.set_tempo` | yes | Set the song tempo in BPM. |
| `transport.stop` | no | Stop playback. |
| `transport.tempo_map_add` | yes | Add a tempo and/or time-signature event to the map, or replace the half it names at that tick (the other half is kept). Adding an event brings the map into force unless `active` is passed false: an event no timeline read would obey is a trap. Refused, typed, when the event fails the engine's own bounds or carries neither half, and a refusal writes nothing. |
| `transport.tempo_map_clear` | yes | Remove every event AND switch the map off, in one step (one undo). Rejects an already empty, inactive map with invalid_args: an edit that changes nothing is not an edit. The inverse is the whole captured map, so this command names no single inverse command - the transaction records the map itself. |
| `transport.tempo_map_get` | no | The tempo map: every tempo and time-signature event, whether the map is in force, and what its queries answer AT THE PLAY HEAD - the tempo, the time signature and the elapsed seconds, read through the map's own ticks-to-time conversion. An empty or inactive map reports the global tempo at every position, and before the first event the global tempo is what is in force (docs/TEMPO-MAP.md). |
| `transport.tempo_map_remove` | yes | Remove the tempo map event at an exact tick. A tick carrying no event is typed not_found. Removing the LAST event leaves an empty map, which is the pre-tempo-map engine: every tick answers the global tempo again. |
| `transport.tempo_map_set_active` | yes | Switch the tempo map's authority on or off WITHOUT editing its events: off, every tick answers the global tempo again and the stored events are still saved with the project; on, the timeline obeys them. A call that sets the state it already has is invalid_args. |

### vca (14 ids)

| command | mutating | description |
| --- | --- | --- |
| `vca.assign` | yes | Put a mixer channel into a group, so the group's fader scales it. Refused, typed, for ch-0 (master), for a channel already in this group, and for a channel that belongs to ANOTHER group - a channel is in at most one, which is what keeps 'soloing a member' single-valued; the refusal names the group holding it. Reversible through the ProjectJournal (a recorded undo step). |
| `vca.create` | yes | Create a VCA / mix-and-edit group and return its vca-<n> id. A group owns one fader that scales every member mixer channel RELATIVE to the channel's own fader (moving it never writes a member's fader, which is what makes it exactly reversible), a mute, a solo, and - once vca.track_add has named them - a set of tracks whose media edits it locks together. A group holds no members at birth; add mixer channels with vca.assign and tracks with vca.track_add. Reversible through the ProjectJournal (the recorded inverse removes the group it created). |
| `vca.edit_move` | yes | Move a clip to an absolute tick position AND move every other member of the group's edit set by the SAME delta, so the members stay locked to one timeline (this is the phase-locked multitrack edit). The named clip is the anchor; a member track whose clips overlap the anchor's pre-command span is moved by the delta, a member with nothing there is reported in unlocked_tracks, and a member whose track is gone in skipped_tracks. Refused, typed, when the group's phase lock is off, when the anchor's track is not in the edit set, or when the set has fewer than two live tracks. One control.undo returns every moved clip. Reversible through the ProjectJournal (a composite Clip checkpoint covering every clip the lock moved). |
| `vca.get_state` | no | One group addressed by its vca-<n> id: the name, the fader value, the gain actually published to the members (0 when the group is muted), the mute and solo flags, the phase lock, every member channel with the gain it is scaled by, and the edit set with the ids whose track no longer exists reported separately as missing_tracks. Writes nothing. |
| `vca.list` | no | Every group of the mix with its whole state: the fader and the gain published from it, the mute and solo flags, the phase lock, the member mixer channels with the gain each one is being scaled by right now, and the edit set as trk-<n> ids (with the ids that no longer name a live track separated out). Writes nothing. |
| `vca.remove` | yes | Delete a group. Its member channels play at their own faders again (the published gain is reset), and the edit set goes with the group. The delete is EXACTLY reversible: one control.undo re-creates the group with its id, name, fader, mute, solo, phase-lock flag, member channels and edit tracks. |
| `vca.rename` | yes | Rename a group. The name is what a group is called in the project file's <vcagroup> element and what its fader, mute and solo display names are built from; it is not an address, so vca-<n> ids are unaffected. An empty name is refused, typed. Reversible through the ProjectJournal (a recorded undo step). |
| `vca.set_gain` | yes | Set a group's fader (0..2, 1.0 is unity and 2.0 is +6 dB). The fader SCALES every member channel relative to the channel's own fader - no member model is written - so moving it and moving it back leaves every member bit-identical, and a member keeps the value the user set for it. The result reports the gain actually published to the members, which is 0 while the group is muted. Reversible through the ProjectJournal (the group's fader model checkpoint). |
| `vca.set_mute` | yes | Mute or unmute a group. Mute is a gain of zero published from the group, NOT the members' own mute models - so muting a group silences every member, a soloed member of a muted group stays silent, and each member's own mute state survives the pair untouched. Reversible through the ProjectJournal (the group's mute model checkpoint). |
| `vca.set_phase_lock` | yes | Switch a group's phase lock on or off. With the lock ON, a media edit made on one of the group's tracks is applied to every other member at the same source position (see vca.edit_move); with it OFF the membership is kept but the takes can be edited independently again. The default for a new group is ON. Reversible through the ProjectJournal (a recorded undo step). |
| `vca.set_solo` | yes | Solo or unsolo a group. Soloing a group makes exactly its members audible (the same exclusive solo a channel has, so soloing a group clears any other group's), and clearing the flag restores the pre-solo mute state of every channel. Reversible: the whole action - the flag, every other group's flag and every channel's mute - is one recorded undo step. |
| `vca.track_add` | yes | Add a track to a group's edit set, so media edits on it are locked to every other member's. Membership is by the track's STABLE trk-<n> id and is recorded on the GROUP, not on the track; the group does not have to be a mixer group of anything (a group can be an edit group alone). Survives save/reload (the group's <vcagroup> element carries an <edittrack track="n"/> child per member). Reversible through the ProjectJournal (a recorded undo step). |
| `vca.track_remove` | yes | Take a track out of a group's edit set. No clip moves: the track simply stops being locked to the others. A track that is not in the set is REFUSED rather than silently accepted. Reversible through the ProjectJournal (a recorded undo step). |
| `vca.unassign` | yes | Take a mixer channel out of a group. The channel plays at its own fader again (its published gain returns to unity) and keeps that fader's value throughout. A channel that is not a member of this group is REFUSED rather than silently accepted, so a wrong id cannot look like a success. Reversible through the ProjectJournal (a recorded undo step). |

### warp (6 ids)

| command | mutating | description |
| --- | --- | --- |
| `warp.add` | yes | Pin one frame of a sample clip's audio to a timeline position (a warp marker). The clip keeps its markers in source-frame order and the engine refuses a marker that would reuse a source frame or put the set out of order - nothing is written in that case. Reversible through the ProjectJournal (Clip checkpoint). |
| `warp.list` | no | The warp map of a sample clip: its markers (the source frame each one pins and the clip-relative timeline offset it lands on, in the order the engine holds them), its tempo mode, its declared source tempo and the engine's marker cap. Read-only. |
| `warp.move` | yes | Move the warp marker that pins a given source frame to a new timeline offset, keeping it pinned to that same frame of the audio. The new offset must stay strictly between the marker's neighbours. Reversible through the ProjectJournal (Clip checkpoint). |
| `warp.remove` | yes | Delete the warp marker that pins a given source frame, leaving the rest of the map in place. Removing the last marker leaves the clip unwarped, so its mapping is the linear one again. Reversible through the ProjectJournal (Clip checkpoint). |
| `warp.set` | yes | Set a sample clip's warp map: replace the whole marker list ('markers'; an empty array clears it), choose the tempo mode ('follow' the project tempo, or 'source' - the clip leads with the tempo it was recorded at) and/or declare that source tempo in BPM. One call is one undoable step. Reversible through the ProjectJournal (Clip checkpoint). |
| `warp.stretch` | yes | Choose how a warped sample clip renders its rate change: 'resample' (the default, and what the engine has always done - the rate goes to the resampler and the pitch moves with it, so a 2x warp is an octave up) or 'preserve_pitch' (the same rate is rendered by the WSOLA stretcher, AudioStretcher: the clip lasts as long as the mapping says and keeps its own pitch). Refused for a clip that renders linearly - with no marker and no source tempo there is no rate change to render. Reversible through the ProjectJournal (Clip checkpoint). |

### wasm (8 ids)

| command | mutating | description |
| --- | --- | --- |
| `wasm.get_state` | no | The hosted module's state: whether anything is hosted, and its path/format/fuel budget; when one is, its declared channel count and latency (the values the host reads from the channels and latency globals AND then clamps, ABI doc section 4.1), whether it exports process(), its linear-memory size, and all 16 parameter slots. 'last_log' is the text the most recent call passed to host_log() - reading it clears it. Writes nothing. |
| `wasm.list` | no | What this build's WASM DSP sandbox can host: the ABI version and import module, the exports a module must declare (with their wasm types) and the host imports it may use, the hard limits the host enforces (channels, parameter slots, log bytes, linear-memory and store limits, the default fuel budget, the largest block), and every .wat/.wasm module in a directory. The directory is 'root', defaulting to 'wasm-modules' - the directory the build assembles the demo modules into - resolved against the process's working directory. Also reports the currently hosted module. |
| `wasm.load` | yes | Host a module in the sandbox: 'path' is a .wat (assembled with the runtime's own wat2wasm, so no extra toolchain is needed) or a .wasm file. The module is compiled and instantiated, and 'fuel' overrides the per-call fuel budget (the default is the ABI's). ALL OR NOTHING: a module that fails to compile or instantiate is refused, typed, and the module already hosted is left alone - the host builds the new instance before replacing the old one. Reports the hosted module's own state, including the channels and latency the ABI reads from its globals. |
| `wasm.pool` | no | The shared WASM worker pool: how many lane threads this process runs (bounded, shared by every hosted module - not one per module), how many workers are registered, how many lane passes found work, how many queued blocks the lanes processed, and what the audio path's wake-ups cost: `wakeups` (a parked lane was woken), `wakes_suppressed` (a lane was already awake, so the audio thread took no syscall) and `parks` (a lane went to sleep). Counters are process-wide and never reset. Writes nothing. |
| `wasm.process` | no | Run one block through the hosted module and report what happened. process() is entered once per channel plane, exactly as WasmWorker enters it, and plane 0's output is read back. 'frames' (1..8192) and 'sample_rate' default to 48 and 48000; 'input' supplies plane 0's samples, and when it is absent the conformance suite's rising ramp is used. 'status' is 'ok', 'trap' or 'error': a trap is REPORTED rather than thrown, which is the sandbox's whole purpose. The call's own outcome is a successful result; only having nothing to run is a refusal. |
| `wasm.render_offline` | no | Render a module offline - one block in flight, on a fresh worker of the shared pool, so a module's state advances in block order - and report whether the render is reproducible WITHIN A MEASURED TOLERANCE rather than against a hash. 'repeats' pooled renders (2..8, default 2) are compared pairwise, and two inline (no-pool) renders of the same input give this build's own run-to-run floor; 'deterministic' is the worst pooled pair being within that floor in both differing frames and largest absolute difference. 'comparator_self_test' perturbs one sample and reports that the comparator finds it, so 'identical' is a measurement and not a constant. 'module' defaults to the hosted module; 'frames' (default 4096), 'block_frames' (default 256), 'sample_rate' (default 48000) and 'input' (plane 0; a ramp by default) describe the render. Writes nothing. |
| `wasm.set_param` | yes | Write one of the 16 parameter slots a module reads through env.host_get_param(index): 'index' is 0..15 and 'value' an f32. Refused, typed, when no module is hosted or the index is outside the range - never clamped silently, because a silent clamp is indistinguishable from a working write. 'previous' reports the slot's old value and one control.undo puts it back. This does NOT touch the wasm_effect plugin's 8 parameter models: those are project state, persisted with the project - reach them with plugin.param_set on a wasm_effect device instead. |
| `wasm.unload` | yes | Drop the hosted module, releasing its wasmtime store and the linear memory that went with it. Refused, typed, when nothing is hosted - there is no silent no-op. The inverse of wasm.load and of itself: the dropped module's path is recorded and one control.undo hosts it again, from a cold memory image. |

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
| agent-surface gate | 340 commands, 339 swept, 1 allowlisted, 48 reflected, 42 baselined, 0 problems | `python3 tests/agent-surface-gate.py ../build/zene tests/data/agent-control-fixture.mmp --check`; latest report `../build/tests/agent-surface-report.json` |
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

**Cross-check, with its residual.** The A16 figure this document headlines is the **ratcheted** one:
the registered `ReversibilityContractTest` compares the published block against a histogram computed
from the live table on every run, and `tools/dawproject-proof.sh` (part 2) is the probe that re-takes
it. A source-level re-derivation with the `grep` two rows above — over the same commit — counts 346
declared rows (312 ungated + 17 `session.*` + 2 `telemetry.*` + 8 `wasm.*` + 7 `stem.*`), i.e. 339 for
this build's configuration, and 157 `true_inverse` against the published 158. **Settled (independent
verification, 2026-09-19):** the one missed row is `chord.set`, declared with the `RCO` macro in
`src/core/ControlReversibilityTableChord.cpp:88` — a form the one-line `(RC|R)\("` pattern above does
not match; 339 + 1 = 340 and 157 + 1 = 158, so the published figure is correct. The anti-drift test's own
comparison remains the authority; see `docs/CAPABILITIES-0.3.0-VERIFICATION.txt`.

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
