/*
 * ControlRegistryGroups.h - the command-group registration points the
 *                          0.3.0-alpha wave's last merges appended to
 *                          ControlRegistry.h (SPEC-zene-studio A11-A16):
 *                          comp.*, the guard-compiled wasm.*, browser.*,
 *                          modulator.* and note.expression_*, link.*.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_CONTROL_REGISTRY_GROUPS_H
#define LMMS_CONTROL_REGISTRY_GROUPS_H

#include "lmms_export.h"

namespace lmms
{

//! Forward declaration: every function below takes a reference, and this header
//! is included by ControlRegistry.h at a point where the class is defined - but
//! it is also includable on its own (the group translation units may).
class ControlRegistry;

/*! comp.lane_add / lane_remove / lane_list / assign - take lanes and the
 *  assignment of takes to them (task #600). The engine half is include/TakeLane.h
 *  and `Track::takeLanes()`; docs/COMPING.md holds the element shape and what is
 *  deliberately not wired yet. */
LMMS_EXPORT void registerCompCommands(ControlRegistry& registry);
//! comp.select / comp.rebuild / comp.get_state - the composite half, in its own
//! translation unit (the clip, warp and rack groups' split).
LMMS_EXPORT void registerCompEditCommands(ControlRegistry& registry);

/*! wasm.list / get_state / process - what the sandbox can host, what it is
 *  hosting and what a block through it does - plus the mutating half.
 *
 *  GUARDED BY #ifdef LMMS_HAVE_WASM, in both the declaration and the definition,
 *  because the sandbox IS a compile-time feature: the wasmtime C API is an
 *  optional dependency (cmake/modules/FindWasmtime.cmake), and WANT_WASM
 *  degrades to OFF without it (CMakeLists.txt:957-963). A build without wasmtime
 *  compiles src/wasm out entirely, so the registry must not carry ids whose
 *  handler could not exist - the rule the session.* and telemetry.* groups
 *  follow. src/core/ControlRegistry.cpp guards its call with the same #ifdef and
 *  the A16 table guards its six rows with it too, so all three stay consistent in
 *  both directions. */
#ifdef LMMS_HAVE_WASM
LMMS_EXPORT void registerWasmCommands(ControlRegistry& registry);
//! wasm.load / unload / set_param - the mutating half, in its own translation
//! unit (the automation, warp, rack and comp groups' read/edit split). Called by
//! registerWasmCommands; the registry has exactly one wasm.* registration point.
LMMS_EXPORT void registerWasmEditCommands(ControlRegistry& registry);
#endif

/*! The stem.* group (docs/FEATURE-LIST-0.3.0.md row 26, board task #653): the
 *  offline stem-separation engine made drivable - stem.get_state, the job verbs
 *  (stem.job_start / job_status / job_result / job_cancel) and the model-store
 *  verbs (stem.model_get_state / model_download). The engine half is
 *  include/StemSeparation/ (StemJobManager, StemModelStore, the two backends)
 *  and was already in the tree with five registered tests; the only route to it
 *  was tools/stem_split_cli.py, outside the socket, plus a GUI-only clip action.
 *
 *  WANT_STEM_SPLIT defaults to ON (CMakeLists.txt:130, owner decision 14), so
 *  the group is in the default build; a build that deliberately configures it
 *  OFF compiles none of the engine, so the registry must not carry ids whose
 *  handler could not exist - the rule the telemetry.*, session.* and wasm.*
 *  groups follow. Its call site is guarded by the same #ifdef, and so are its
 *  seven A16 rows, so the registry and the contract table stay consistent in
 *  both directions. */
LMMS_EXPORT void registerStemCommands(ControlRegistry& registry);
//! stem.model_get_state / stem.model_download - the model-store half, in its
//! own translation unit (the folder-track, session, warp, rack, comp and
//! automation groups' read/edit split, for the file-length ratchet). Called by
//! registerStemCommands; the registry has exactly one stem.* registration point.
LMMS_EXPORT void registerStemModelCommands(ControlRegistry& registry);

//! The browser.* group (W8 tag/metadata search plus the waveform peak cache):
//! browser.roots, browser.query, browser.tags and browser.peaks. The engine half
//! is include/BrowserCatalog.h (the roots the browser tabs read, the metadata an
//! audio file can be probed for, the persisted tag store) and
//! include/BrowserPeakCache.h; this group is the ONLY way to reach any of it -
//! there is no UI for tags, no UI for a query and no UI for the peak cache.
LMMS_EXPORT void registerBrowserCommands(ControlRegistry& registry);
//! browser.tag.add / browser.tag.remove, and (0.5.0 L11, WF-007)
//! browser.category.set / browser.category.clear - the mutating half, in its
//! own translation unit (the automation and warp groups' read/edit split). All
//! four record a snapshot-class transaction whose inverse is a paired command.
LMMS_EXPORT void registerBrowserTagCommands(ControlRegistry& registry);

/*! The modulation layer (#602): modulator.get_state / create / remove /
 *  rate_set - the layer itself and a modulator's own LFO. The engine half is
 *  include/ModulationLayer.h (a song-level, timeline-locked LFO per modulator,
 *  driving a set of parameters by a relative amount); docs/MODULATION.md holds
 *  the design decisions and the honest limits. Split from the route half for
 *  the same reason the automation, warp, rack and comp groups are. */
LMMS_EXPORT void registerModulatorCommands(ControlRegistry& registry);
//! modulator.target_set / depth_set / target_remove - the route half, in its
//! own translation unit: which parameters a modulator drives, and by how much.
LMMS_EXPORT void registerModulatorRouteCommands(ControlRegistry& registry);

/*! note.expression_set / get / clear - #602's per-note half, and the control
 *  surface #601's per-note MPE expression never had. These commands drive the
 *  Note fields and the optional mpepitch/mpepressure/mpetimbre attributes #601
 *  already stores (docs/MPE.md); they are NOT a second expression store. */
LMMS_EXPORT void registerNoteExpressionCommands(ControlRegistry& registry);
/*! link.get_state / set_enabled / set_quantum / set_start_stop_sync /
 *  set_session_tempo - session tempo, beat-phase and start/stop sync (D11,
 *  WF-051). The engine half is include/LinkSync.h: real Ableton Link (the
 *  vendored library, src/3rdparty/ableton-link) wherever the build carries it,
 *  else the zene-link-style fallback only Zene instances speak
 *  (docs/LINK-SYNC.md). The transport's Sync toggle (SessionSyncAction) runs
 *  link.set_enabled; everything else is driven through this group. */
LMMS_EXPORT void registerLinkCommands(ControlRegistry& registry);

/*! link.audio_get_state / link.audio_set_enabled - the PCM stream between two Zene
 *  instances (Q050-024, docs/LINK-AUDIO.md). Not Link transport sync and not Ableton's
 *  LinkAudio: it shares no state with registerLinkCommands' group. */
LMMS_EXPORT void registerLinkAudioCommands(ControlRegistry& registry);

/*! bounce.in_place / freeze.track / freeze.region / freeze.unfreeze - freeze /
 *  bounce-in-place. The engine half is include/BounceInPlace.h (the offline
 *  render of one track's own output, in a child process) and Track's own frozen
 *  take (Track::FrozenTake, the play() substitution InstrumentTrack and
 *  SampleTrack make, and the `frozen` element of the track's project XML);
 *  this group is what makes any of it drivable, and it is the ONLY way to
 *  reach it - there is no interface for freeze in this release. */
LMMS_EXPORT void registerFreezeCommands(ControlRegistry& registry);

/*! The folder half of the track.* group (owner items 3+20+21): track.set_folder,
 *  track.folder_set_collapsed, track.set_routing, track.set_pinned and
 *  track.folder_get_state, plus the four named-visibility-set verbs in their own
 *  translation unit (the read/edit split the warp, rack, comp and automation
 *  groups follow). The engine half is include/TrackFolder.h (a Track subclass
 *  that REFERENCES its children and never owns them, with a group mode and a
 *  routing mode that sums them through one mixer channel of its own) and
 *  TrackContainer's visibility-set store; this group is what makes any of it
 *  drivable, and it is the ONLY way to reach it - there is no folder affordance,
 *  no pin toggle and no set switcher in this release's interface. */
LMMS_EXPORT void registerTrackFolderCommands(ControlRegistry& registry);
//! track.visibility_set_save / _apply / _remove / _list - the named
//! visibility sets, in their own translation unit. Called by
//! registerTrackFolderCommands; the registry has exactly one track-folder
//! registration point.
LMMS_EXPORT void registerTrackFolderSetCommands(ControlRegistry& registry);

/*! The `vca.*` group (OWNER-31 item 11, "phase-locked multitrack edit groups";
 *  the mix half is task #622's entity). It drives include/VcaGroup.h and, for
 *  the group's own container, src/core/Mixer.cpp (`<vcagroup>` elements beside
 *  the channels, each carrying one `<member channel="n"/>` per member mixer
 *  channel and one `<edittrack track="n"/>` per edit-set track). Three halves
 *  in three translation units (the folder-track, session, warp, rack, comp and
 *  automation groups' split, for the file-length ratchet):
 *  ControlCommandsVca.cpp (create / remove / list / get_state / rename - and
 *  the group's ONLY registration point), ControlCommandsVcaMix.cpp (set_gain /
 *  set_mute / set_solo / assign / unassign) and ControlCommandsVcaEdit.cpp
 *  (set_phase_lock / track_add / track_remove / edit_move). No compile-time
 *  switch: a group is a plain entity on the Mixer and its edit set is a list of
 *  stable track ids, so its fourteen ids are honest in every configuration.
 *  This group is the ONLY way to reach any of it in 0.3.0 - there is no VCA
 *  strip, no group menu and no phase-lock toggle in the interface, and before
 *  it the only way to get a group at all was to hand-edit the project file. */
LMMS_EXPORT void registerVcaCommands(ControlRegistry& registry);
//! vca.set_gain / set_mute / set_solo / assign / unassign - the mix half, in
//! its own translation unit. Called by registerVcaCommands.
LMMS_EXPORT void registerVcaMixCommands(ControlRegistry& registry);
//! vca.set_phase_lock / track_add / track_remove - the edit SET and the lock
//! switch, in their own translation unit (the split that kept the group under
//! the 500-line file ratchet: those three and vca.edit_move were one file until
//! it reached 521 lines). Called by registerVcaCommands through
//! registerVcaEditCommands.
LMMS_EXPORT void registerVcaEditSetCommands(ControlRegistry& registry);
//! vca.edit_move - the phase-locked move itself, in its own translation unit.
//! Called by registerVcaEditCommands; the registry has exactly one vca.*
//! registration point.
LMMS_EXPORT void registerVcaEditCommands(ControlRegistry& registry);
/*! clock.get_state / clock.master_set / clock.slave_set - MIDI clock, the DAW
 *  as a clock master and as a clock slave. The engine half is include/MidiClock.h
 *  and its sources (the pulse generator, the tempo tracker, the widened MIDI
 *  input and output switches and the per-audio-period hook in
 *  Song::processNextBuffer); this group is what makes any of it drivable, and it
 *  is the ONLY way to reach it - there is no interface for a MIDI clock in this
 *  release. MTC is not generated: the engine has no frame rate or SMPTE offset
 *  to build one from, and clock.get_state reports that as `mtc: "absent"`. */
LMMS_EXPORT void registerClockCommands(ControlRegistry& registry);

/*! livecode.schedule / livecode.unschedule / livecode.get_state - SCHEDULED Lua
 *  evaluation (board card #708): a script loaded once and evaluated by the
 *  engine on its own - every bar, every beat, or each transport edge - with
 *  nothing re-triggering it, under a per-fire instruction budget so a runaway
 *  hits a bound instead of a stall. `livecode` because `clock` is taken (the
 *  MIDI clock group above) and `script.clock` would name a sub-verb of a group
 *  whose verbs run scripts NOW; the registry derives the group from the first
 *  dot-delimited segment, so these three ids are their own group. The engine
 *  half is include/ScriptClock.h (the grid, the poll and the bounded fire). */
LMMS_EXPORT void registerLivecodeCommands(ControlRegistry& registry);

/*! mts.get_state / mts.load_scale / mts.load_keymap / mts.set_tuning /
 *  mts.set_note / mts.reset / mts.master_set - the session-wide dynamic
 *  tuning table (board card #712; MTS-ESP host-wide tuning). The engine half
 *  is include/SessionTuning.h / src/core/SessionTuning.cpp: ONE 128-entry
 *  table every instrument reads AT RENDER when it is active - Interval/Scale/
 *  Keymap stay the Microtuner's (docs/MIDI-DEPTH.md:20), this adds only the
 *  session-wide object. thirdparty/mts-esp/ (upstream ODDSound/MTS-ESP, 0BSD
 *  - see the README beside the files) is the master API the table publishes
 *  through once mts.master_set arms it. The group is the ONLY way to reach
 *  the table: there is no interface surface for it in this release
 *  (docs/KNOWN-LIMITATIONS.md). `mts.*` was free in the registry (`clock.*`
 *  is the MIDI clock's). */
LMMS_EXPORT void registerMtsCommands(ControlRegistry& registry);


//! The retrospective MIDI capture surface (owner item 14, docs/MIDI-RETRO-CAPTURE.md),
//! and the two halves of the chain-preset group (OWNER-31 item 2). Moved here from
//! include/ControlRegistry.h, which is where this file's own note says the groups
//! appended after the comp/wasm/browser/modulator/link wave belong: the chain and
//! capture declarations were the last two additions and they took that header over
//! the 500-line file-length ratchet. Same namespace, same signatures - a caller
//! still includes include/ControlRegistry.h, which includes this file.
//! midi.retro_capture_arm / midi.retro_capture_status / midi.retro_capture_to_clip
//! - the retrospective MIDI capture surface (owner item 14,
//! docs/MIDI-RETRO-CAPTURE.md). The arm switch is mode state (no transaction); the
//! to-clip command is one journalled Track checkpoint over the clip it creates.
LMMS_EXPORT void registerMidiRetroCaptureCommands(ControlRegistry& registry);
/*! chain.list / chain.get_state / chain.save - the READ and CAPTURE half of the
 * chain-preset group (the 0.3.0 ladder's "plugin-chain presets", OWNER-31
 * item 2). A chain preset is a named copy of a chain's devices WITH each
 * device's own state, kept in the user preset tree (chainpresets/) so it is
 * usable across projects; it is not a rack chain (rack.add_chain). The engine
 * half is include/ControlChainPresetSupport.h.
 */
LMMS_EXPORT void registerChainReadCommands(ControlRegistry& registry);
//! chain.apply / chain.rename / chain.remove - the EDIT half, in its own
//! translation unit (the automation and warp groups' split).
LMMS_EXPORT void registerChainEditCommands(ControlRegistry& registry);
/*! The SAVED-STORE half of the export group (feature row 70): export.preset_list
 * / export.preset_add / export.preset_apply / export.preset_remove - the render
 * and export presets themselves, and the settings the NEXT render is started
 * with. The engine half is include/ControlExportPresetSupport.h; the applied
 * preset reaches the render as the child process's own command line (see
 * controlExportPresetRenderArgs), never as a second render path.
 */
LMMS_EXPORT void registerExportPresetCommands(ControlRegistry& registry);
/*! The mixer group's ROUTING verbs - mixer.route_to / mixer.send_to /
 * mixer.sidechain_to / mixer.route_remove - in their own translation unit. They
 * are part of the mixer group (the ids keep the `mixer.` prefix); the file
 * is separate because ControlCommandsMixer.cpp is near gate 7's file-length cap,
 * the automation and warp groups' split. docs/specs/AGENT-TOOLING.md:186 names
 * route_to / send_to as part of this release's mixer surface, and the sidechain
 * half of feature row 27 ("PDC and sidechain") is here.
 */
LMMS_EXPORT void registerMixerRouteCommands(ControlRegistry& registry);
/*! pdc.report - the plugin-delay-compensation read (feature row 27, "PDC and
 * sidechain"): the mixer's published total latency, every channel's alignment
 * point and chain latency, the compensation applied at every send, and whether
 * sidechain routing exists. Read-only by design: the compensation is recomputed
 * by Mixer::updateLatencyCompensation() every period, so no command sets it.
 */
LMMS_EXPORT void registerPdcCommands(ControlRegistry& registry);
/*! routing.get_state - the routing-graph read (feature row 28): the graph a
 * target's signal is processed through, its nodes, connections and cached
 * processing order, plus a mixer channel's rack graph. An inspector: the engine's
 * threading contract forbids live topology edits (include/RoutingGraph.h).
 */
LMMS_EXPORT void registerRoutingCommands(ControlRegistry& registry);
/*! bus.list / bus.create / bus.remove - the parallel-bus topology (feature row
 * 29, "Audio ports / AudioBus"). A bus is a MixerChannel with is_bus set
 * (Mixer::createBusChannel, Phase D task #587); its set verbs are the mixer's
 * own, so this group is only the topology.
 */
LMMS_EXPORT void registerBusCommands(ControlRegistry& registry);
/*! port.get_state / port.set_pin - the audio-ports pin matrix (feature row 29):
 * the device's AudioPortsModel, its in/out matrices and the pin write the
 * PinConnector view performs (AudioPortsModel::Matrix::setPin).
 */
LMMS_EXPORT void registerPortCommands(ControlRegistry& registry);
/*! The five groups above, registered as ONE call from ControlRegistry.cpp's
 * registerControlCommands(). They are one feature set - the routing surface of
 * rows 27-29 - and ControlRegistry.cpp's registration block sits at gate 7's
 * file-length cap, so the aggregation lives here rather than as five lines
 * there.
 */
LMMS_EXPORT void registerRoutingSurfaceCommands(ControlRegistry& registry);
/*! feedback.* - the cycle-permitted signal-graph submode (board card #709): with
 *  the mode off (the default) the mixer's cycle rule is untouched; inside it a
 *  loop-closing send is accepted WITH PDC suspended for it, stated at every point
 *  of use with REAPER's warning verbatim. Rationale: src/core/ControlCommandsFeedback.cpp;
 *  bounds it does NOT cover: docs/KNOWN-LIMITATIONS.md.
 */
LMMS_EXPORT void registerFeedbackCommands(ControlRegistry& registry);
/*! patcher.get_state / patcher.set_wiring - the patcher node-graph group
 * (feature row 69, "Patcher node-graph driving"): the graph a target's signal
 * is processed through, read in PATCH terms (the role each node is addressed by,
 * its own parameters, the wiring) and EDITABLE. The edit builds a new graph off
 * the audio thread and publishes it under the audio engine's model-change guard
 * - the seam that makes a topology edit non-concurrent with process(), which is
 * what include/RoutingGraph.h's threading contract requires - and the authored
 * wiring is re-applied by every derived rebuild (EffectChain::rebuildRoutingGraph),
 * so a hand-wired edge is no longer discarded by the next plugin.load. A group of
 * its own rather than a setter on routing.*: that group is the inspector of the
 * same graph, and docs/PATCHER-GRAPH.md records the design and its bounds.
 */
LMMS_EXPORT void registerPatcherCommands(ControlRegistry& registry);
/*! patcher.set_wiring - the group's EDIT half, registered by
 * registerPatcherCommands() and defined in src/core/ControlCommandsPatcherEdit.cpp.
 * The two halves are two translation units for the reason the automation and
 * plugin-scan groups are: gate 7 measures a file. The rationale for the verb, the
 * order of operations that makes every refusal write nothing, and the A16 record
 * it leaves are on that file's own header.
 */
LMMS_EXPORT void registerPatcherEditCommands(ControlRegistry& registry);
/*! The plugin scan cache and its quarantine list (feature row 46): the READ half
 * (plugin.scan_cache_get_state / scan_cache_list / scan_cache_lookup, in
 * src/core/ControlCommandsPluginScan.cpp) and the EDIT half
 * (plugin.scan_cache_quarantine_add / scan_cache_quarantine_remove and
 * plugin.rescan, in src/core/ControlCommandsPluginScanEdit.cpp), its own two
 * translation units along the seam ControlCommandsAutomation.cpp /
 * ...AutomationEdit.cpp established. The engine half is
 * include/PluginScanCache.h; this group is what makes it drivable, and it is
 * what retires the "hand-editing a JSON file" route the audit's row names.
 * A quarantined file is hidden from discovery, and plugin.rescan (the id
 * docs/specs/AGENT-TOOLING.md:269 boards) is the scan the factory already has.
 */
LMMS_EXPORT void registerPluginScanCommands(ControlRegistry& registry);
//! plugin.scan_cache_quarantine_add / plugin.scan_cache_quarantine_remove /
//! plugin.rescan - the mutating half of the scan-cache group, in its own
//! translation unit (the automation and warp groups' split).
LMMS_EXPORT void registerPluginScanEditCommands(ControlRegistry& registry);
//! plugin.scan_start / plugin.scan_status / plugin.scan_cancel (Q050-090) -
//! the background rescan: the same scan without blocking the window, in its
//! own translation unit for the reason the read and edit halves have theirs.
LMMS_EXPORT void registerPluginScanBackgroundCommands(ControlRegistry& registry);
/*! plugin.host_chunking (feature row 82, CODE-4): the chunking contract both
 * plugin host paths keep - "a request larger than the prepared block is split
 * into chunks of at most that block" - together with the counters the audio
 * path increments, so the property is observable on a running instance and not
 * only in a unit test. The engine half is include/PluginHostChunking.h; the
 * counters live in the core because the hosts are plugin modules
 * (plugins/Vst3Effect/Vst3Host.cpp and plugins/ClapEffect/ClapHost.cpp write
 * into them). Read-only: one not_mutating A16 row.
 */
LMMS_EXPORT void registerPluginHostChunkingCommands(ControlRegistry& registry);
/*! plugin.host_notes (feature row 79, board task #669): the note path the CLAP
 * host keeps - clap.note-ports discovery, the plug-in's own port index, the
 * bounded lock-free queue between LMMS' MIDI route and the audio thread, and
 * the notes delivered into the plug-in's input event list - together with the
 * audio layout of the CLAP plug-in loaded last, so the CLAP instrument's
 * audio-output configuration is observable on a running instance and not only
 * in a unit test. The engine half is include/PluginHostNotes.h; the counters
 * live in the core for the same reason the chunking counters do (the host is a
 * plugin module, plugins/ClapEffect/ClapHost.cpp writes into them, and
 * plugins/ClapInstrument uses the same host). Read-only: one not_mutating A16
 * row in src/core/ControlReversibilityTableClapInstrument.cpp.
 */
LMMS_EXPORT void registerPluginHostNotesCommands(ControlRegistry& registry);
/*! crash.list_reports / crash.acknowledge_report / crash.discard_report /
 * crash.upload_report - the crash reporter's agent surface (feature row 54).
 * The engine half is include/CrashReporter.h, installed from main() before this
 * socket exists; crash.list_reports is the read docs/specs/AGENT-TOOLING.md:194
 * names, the two writers are the module's acknowledge/discard operations, and
 * crash.upload_report is registered and REFUSES because this build has no
 * network code at all (the automation.mode_set shape).
 */
LMMS_EXPORT void registerCrashReporterCommands(ControlRegistry& registry);
/*! crash.enable / crash.disable - the crash reporter's ARM pair (feature row 54,
 * board task #643), in its own translation unit
 * (src/core/ControlCommandsCrashControl.cpp). main() still arms the reporter
 * before this socket exists - a crash with no socket must still be reported -
 * but arming is now reversible at the engine (crashreporter::uninstall /
 * handlersArmed / reportDirectory, include/CrashReporter.h), so the two verbs
 * exist: enable arms (to the remembered directory, or an explicit one) and
 * disable disarms, deleting nothing. Both report `armed` from the kernel's own
 * dispositions rather than from a flag, and both are A16 `snapshot` rows whose
 * recorded inverse is the paired command.
 */
LMMS_EXPORT void registerCrashControlCommands(ControlRegistry& registry);
/*! The `mastering.*` group's READ half - mastering.list_candidates (the candidate
 * set auto-mastering wave 1 generates: MasteringJob::defaultCandidates, with each
 * candidate's named target, the standard its numbers come from, the dynamics
 * stage and the chain settings implied) and mastering.get_state (the last run's
 * own report plus the live, hashed state of the files it wrote).
 *
 * Feature rows 25 and 72 of docs/FEATURE-LIST-0.3.0.md; the engine
 * (src/core/MasteringJob.cpp, src/core/MasteringChain.cpp) is in the tree and
 * proven by the registered ctest MasteringTest. No compile-time switch: the
 * chain, the job and the meter are in every configuration.
 */
LMMS_EXPORT void registerMasteringCommands(ControlRegistry& registry);
/*! mastering.run - the group's one WRITING verb, in its own translation unit (the
 * automation, warp, vca and chain-preset groups' read/edit split): one project
 * render in a child process feeds every candidate, each candidate is written as a
 * wav and measured against its own named target, and the files it creates are
 * taken back by a recorded ACTION checkpoint (SPEC A16). It ranks nothing: there
 * is no validated preference scorer for master variants of one song, which is why
 * the choice is the user's (docs/AUTO-MASTERING.md section 8).
 */
LMMS_EXPORT void registerMasteringRunCommands(ControlRegistry& registry);
/*! The `detect.*` group (feature row 34 of docs/FEATURE-LIST-0.3.0.md): transient /
 *  BPM / key detection for an imported audio file, over the control surface -
 *  detect.analyze (the SUGGESTION: one file in, tempo + first transient + key out,
 *  nothing written), detect.apply (the ACCEPTANCE: the tempo into the tempo map
 *  and the key into the project's own `<detected-key>` field, as ONE undoable
 *  step) and detect.get_state (what the project holds, the two method names, the
 *  bounds and the accuracy sentence).
 *
 * The engine half is include/ImportDetection.h over the Qt-free arithmetic in
 * include/ImportDetectionDsp.h, and the key's vocabulary is the PRE-EXISTING one
 * (InstrumentFunctionNoteStacking::ChordTable, the names the piano roll's scale
 * combo is filled from) - this feature names no new scales. No compile-time
 * switch: the decoder, the arithmetic and the project field are in every
 * configuration, so its three ids are honest in every one.
 *
 * BACKLOG.md item 10 is the scope this follows rather than exceeds: (a) tempo and
 * (b) the first transient, shown as suggestions, "never applied silently". The
 * accuracy that is MEASURED (synthesised input with a known answer) and the
 * accuracy that is NOT (real music, unverified on this box) are both stated in
 * detect.get_state's own reply and in docs/IMPORT-DETECTION.md.
 */
LMMS_EXPORT void registerDetectCommands(ControlRegistry& registry);
//! detect.apply - the group's one WRITING verb, in its own translation unit (the
//! automation, warp, vca and mastering groups' read/edit split). It is the
//! ACCEPTANCE half: the tempo into the tempo map and the key into the project's
//! own field, both captured before the first write and refused wholesale when a
//! half the analysis found nothing for was asked for. Called by
//! registerDetectCommands; the registry has exactly one `detect` registration
//! point.
LMMS_EXPORT void registerDetectApplyCommands(ControlRegistry& registry);

/*! The 0.3.0 note/scale/device wave (board task #648; feature-list rows 11, 66 and
 *  81) - six registration points, each its own translation unit because Gate 7
 *  measures a file and the groups are one group each:
 *
 *  - registerNoteRandomCommands: note.random_seed_get / random_seed_set (the seeded
 *    AND persisted pair - the project's MIDI seed, Song::midiSeed, serialized in the
 *    header) and note.randomize (the seeded roll over a clip's velocities and
 *    positions, NoteRandom's pure function of the seed and each note's identity).
 *  - registerNoteSlideCommands: note.slide_set / note.slide_clear - the FL-style
 *    slide (portamento) flag, which the engine has carried since
 *    docs/specs/SPEC-slide-notes.md and never had an id for.
 *  - registerNoteSelectCommands: note.select - the piano roll's note selection, split
 *    out of the note.* file (ControlCommandsNotes.cpp) to keep it under the 500-line
 *    ratchet; called from registerNoteCommands.
 *  - registerNoteTransformCommands: note.transpose / note.velocity_offset /
 *    note.velocity_scale - NoteTransform's three transforms. The grid quantise is
 *    deliberately NOT re-wrapped: groove.quantize already drives
 *    NoteTransform::quantizeNotes with a strength, a humanise amount and a seed.
 *  - registerScaleCommands / registerScaleEditCommands: the `scale.*` group - the
 *    scale and key vocabulary of ChordTable as commands (the read half, the two
 *    context writers scale.root_set / scale.set, and the one clip-editing verb
 *    scale.snap_notes). The context is the group's own process state and is
 *    deliberately not serialized; the piano roll's key/scale selector is NOT wired
 *    to it (docs/KNOWN-LIMITATIONS.md).
 *  - registerDeviceCommands: the registry's first `device.*` group -
 *    device.mpe_get_state / device.mpe_set, the MPE input switch and the axes that
 *    do and do not reach playback.
 *
 *  No compile-time switch: NoteRandom, NoteTransform, ChordTable and MpeExpression
 *  are in src/core in every configuration, so these ids are honest in every one.
 */
LMMS_EXPORT void registerNoteRandomCommands(ControlRegistry& registry);
LMMS_EXPORT void registerNoteSlideCommands(ControlRegistry& registry);
LMMS_EXPORT void registerNoteSelectCommands(ControlRegistry& registry);
LMMS_EXPORT void registerNoteTransformCommands(ControlRegistry& registry);
LMMS_EXPORT void registerScaleCommands(ControlRegistry& registry);
LMMS_EXPORT void registerScaleEditCommands(ControlRegistry& registry);
LMMS_EXPORT void registerDeviceCommands(ControlRegistry& registry);
/*! meter.get_state / meter.arm / meter.measure_file - the BS.1770-4 loudness and
 *  true-peak surface (feature row 24 of docs/FEATURE-LIST-0.3.0.md, "LUFS /
 *  loudness metering"): the LIVE master readout (integrated, momentary,
 *  short-term, the loudest short-term window and true peak, measured from the
 *  periods the engine is rendering by a passive tap, include/MasterLoudnessTap.h)
 *  and the same five numbers for a RENDERED FILE, measured now from the file's
 *  own bytes with the EBU R128 verdict.
 *
 *  WHY IT IS ITS OWN GROUP. The measurement core (`LufsMeter`, the ITU-R
 *  BS.1770-4 / EBU R128 meter) and its offline consumer (`LoudnessReport`, which
 *  the render path feeds) are both in the tree and proven - and were, until this
 *  group, reachable by nothing but a render: no `lufs.` or `meter.` id existed
 *  at all, so nothing an agent could send measured anything. This group is what
 *  makes any of it drivable; it is the ONLY way to measure the live master in
 *  this release (there is no loudness meter widget), and it forks no DSP - every
 *  number it publishes comes out of the merged `LufsMeter`.
 *
 *  No compile-time switch: the meter, the report and the tap are in every
 *  configuration, so its three ids are honest in every one. The render-path
 *  half of the same feature (the `.loudness.txt` sidecar the export dialog and
 *  `--loudness-report` produce) is exposed through `export.get_settings` /
 *  `export.set_loudness_report` in src/core/ControlCommandsExport.cpp.
 */
LMMS_EXPORT void registerMeterCommands(ControlRegistry& registry);
/*! clip.link_create / clip.link_remove / clip.link_get_state / clip.link_sync -
 *  the linked / smart clip relation (feature-list row 6, board task #645): two
 *  clips that share one source, so an edit to one is seen by all of them, with
 *  the link surviving save/reload and an unlink that detaches for good.
 *
 *  The engine half is include/ClipLinks.h (the relation: Clip::linkId(), the
 *  group, and the mirror that does the propagating); the design decision - a
 *  persisted group id plus a WRITE-THROUGH MIRROR, not a shared content object
 *  and not copy-on-write - is recorded there and in docs/LINKED-CLIPS.md. The
 *  ids keep the `clip.` prefix so an agent finds them where it finds clip.trim;
 *  `link.*` is a different feature (docs/LINK-SYNC.md).
 */
LMMS_EXPORT void registerClipLinkCommands(ControlRegistry& registry);
/*! clip.link_get_state / clip.link_sync - the same group's READ half and its
 *  repair verb, in their own translation unit (the automation, warp, vca and
 *  chain-preset groups' read/edit split): the group has four ids and the
 *  file-length ratchet is not moved for a new feature. Ids keep the `clip.`
 *  prefix, so an agent finds the read where it finds the write.
 */
LMMS_EXPORT void registerClipLinkStateCommands(ControlRegistry& registry);
/*! sample.generate - the destructive waveform editor's FIRST slice (board card
 *  #706, design/specs/DECISION-706-DESTRUCTION-MODEL.md): a tone / white / pink
 *  / silence buffer produced into a NEW SampleClip, routed through the
 *  non-destructive clip model (Track::createClip + SampleClip::setSampleBuffer),
 *  one ProjectJournal checkpoint per generate. The four bakers of the same
 *  group (amplify / normalize / reverse / fade) are declared next. Declared here
 *  rather than in include/ControlRegistry.h because that header sits at the
 *  file-length ratchet's limit; registered by
 *  src/core/ControlRegistryRegistrations.cpp. Socket-only this release: no
 *  interface reaches these ids (docs/KNOWN-LIMITATIONS.md). */
LMMS_EXPORT void registerSampleCommands(ControlRegistry& registry);
/*! sample.amplify / sample.normalize / sample.reverse / sample.fade - the
 *  group's four BAKERS, in their own translation unit (the clip group's
 *  read/edit split): each computes a transformed COPY of the clip's frames and
 *  swaps it in with SampleClip::setSampleBuffer, so the inverse of every one is
 *  the Clip checkpoint taken before the write - one undo step, no buffer in the
 *  transaction record, no inverse machinery (decision §2.1/§3.2). */
LMMS_EXPORT void registerSampleEditCommands(ControlRegistry& registry);
/*! record.get_state / arm_track / disarm_track / disarm_all - the ROUTE verbs of
 *  the `record.` group, in their own translation unit (0.3.0, feature rows 14 and
 *  64; docs/RECORD-INPUTS.md): the multi-track recorder's own state, and the verbs
 *  that arm and stop one of its routes. Declared here rather than beside the other
 *  record.* declarations because include/ControlRegistry.h sits at the
 *  file-length ratchet's limit (500 lines) and this header is where the groups
 *  that outgrew that budget are declared. Called by
 *  registerControlCommands() beside registerRecordingInputCommands(). */
LMMS_EXPORT void registerRecordingRouteCommands(ControlRegistry& registry);
/*! automation.ramp_set / automation.ramp_get - the sample-accuracy half of the
 *  `automation.` group (feature-list row 9, board task #646,
 *  docs/SAMPLE-ACCURATE-AUTOMATION.md): render one automation clip at SAMPLE
 *  precision inside each audio block (the clip's own `sample_accurate` flag,
 *  driven here), and read back the ramp the audio thread published for every
 *  automated parameter of the song. Declared here rather than beside the other
 *  automation.* declarations because include/ControlRegistry.h sits at the
 *  file-length ratchet's limit and this header is where the groups that outgrew
 *  that budget live. Called by registerControlCommands() beside
 *  registerAutomationEditCommands(). */
LMMS_EXPORT void registerAutomationRampCommands(ControlRegistry& registry);
/*! safestart.get_state / acknowledge / clear / set_skip - safe-start mode after a
 *  crash (feature row 77 of docs/FEATURE-LIST-0.3.0.md, OWNER-31 item 31, board
 *  task #666). The engine half is include/SafeStart.h: the crash MARKER (written
 *  when a session begins, unlinked on the clean-exit path, so a marker found at
 *  launch means the previous run died), the acknowledgement that says the NEXT
 *  launch is the normal one, and the load-time PREDICATE
 *  (safestart::shouldSkipPluginInstance) that Plugin::instantiate consults, so a
 *  third-party plugin instance is replaced by the engine's DummyPlugin for the
 *  session that follows an unclean exit. This group is the ONLY way it is
 *  operated in 0.3.0: there is no dialog, no banner and no toolbar button for
 *  it (docs/KNOWN-LIMITATIONS.md carries the absence line). */
LMMS_EXPORT void registerSafeStartCommands(ControlRegistry& registry);
/*! oop.get_state / list_families / set_mode / restart / reset_crashes -
 *  out-of-process plugin hosting BEYOND ZynAddSubFx (feature row 80 of
 *  docs/FEATURE-LIST-0.3.0.md, board card #670). The engine half is
 *  include/OutOfProcessHosting.h: the family table - what each plugin family's
 *  out-of-process story IS in this build, with a one-sentence reason where it
 *  has none - and the client-process record the crash-loop REFUSAL is measured
 *  against. The surface is the only way the choice is made in a headless
 *  instance and the only place the crash accounting is readable at all; there is
 *  no page, dialog or column for it in the interface
 *  (docs/KNOWN-LIMITATIONS.md carries the absence line). Registered from
 *  src/core/ControlRegistryRegistrations.cpp. */
LMMS_EXPORT void registerOutOfProcessCommands(ControlRegistry& registry);
/*! revisions.list / revisions.compare / revisions.restore - the in-app revision
 *  timeline (feature-list row 76, OWNER-31 item 30) over the revision artefacts
 *  this engine ALREADY writes: the keep-3 rotation `project.save` performs
 *  (`<file>.rev0..rev2`), the `<file>.bak` a save from the interface leaves, the
 *  autosave `recover.mmp` and its `.info` identity sidecar, and the project's own
 *  git history where it lives in a repository (read through one bounded `git log`
 *  - no new store, no format change). The engine half is
 *  include/RevisionTimeline.h; `restore` is a recorded-action true_inverse whose
 *  inverse is project.restore_revision's own rotation, and `compare` is a
 *  STRUCTURAL element-count summary, not a semantic diff (`mmpz-git diff` owns
 *  that). No compile-time switch - every artefact is a file the engine already
 *  reads and writes, so the ids are honest in every configuration.
 *
 *  Declared here rather than beside the project.* declarations because
 *  include/ControlRegistry.h sits at the file-length ratchet's limit; registered
 *  from src/core/ControlRegistryRegistrations.cpp. */
LMMS_EXPORT void registerRevisionsCommands(ControlRegistry& registry);
/*! The `interchange.*` group - the Standard MIDI File conductor track (feature
 *  row 33 of docs/FEATURE-LIST-0.3.0.md: tempo-map export / SMF cross-DAW
 *  interchange). Four ids: interchange.smf_convention (the tick/PPQ,
 *  tempo-unit and time-signature convention as data on the wire),
 *  interchange.smf_export (write the tempo map as a format-1 conductor track
 *  another DAW can read), interchange.smf_read (read a file's conductor events
 *  back without touching the session - what makes a round trip checkable
 *  against the file rather than against its hash) and interchange.smf_import
 *  (apply them to the tempo map, one undo).
 *
 *  The engine half is include/SmfInterchange.h + src/core/SmfInterchange.cpp,
 *  which is a hand-written encoder and a bounds-checked reader rather than the
 *  note-oriented MidiFile.hpp under plugins/MidiExport or the vendored portsmf
 *  behind plugins/MidiImport - neither is reachable from src/core, where a
 *  tempo map lives. No compile-time switch: the map is a plain value on Song
 *  and the file format is not optional, so its ids are honest in every
 *  configuration. The stated limits (events are steps, so no tempo curve;
 *  only the conductor track is written; only tempo and metre meta events are
 *  read) are in docs/SMF-INTERCHANGE.md and, in one line, in
 *  docs/KNOWN-LIMITATIONS.md.
 */
LMMS_EXPORT void registerInterchangeCommands(ControlRegistry& registry);
/*! The `dawproject.*` group (feature row 37: DAWproject import / export) - the published format read and written: dawproject.convention, .export, .read, .import. Engine half: include/DawProjectInterchange.h. Joined into reversibilityRowTable() by its own group file. */
LMMS_EXPORT void registerDawProjectCommands(ControlRegistry& registry);

/*! The `chord.*` group (feature row 35: "Chord track, chord detection,
 *  progression tools, generators"): chord.get_state / chord.detect /
 *  chord.progression_list, then the track edits (chord.set / chord.remove /
 *  chord.clear / chord.detect_to_track) and the two generators
 *  (chord.track_write / chord.progression_generate).
 *
 *  The engine half is include/ChordTrack.h (an ordered, persistent chord track
 *  written as ONE <chord-track> element inside <song>),
 *  include/ChordDetect.h (what a clip's notes spell) and
 *  include/ChordProgression.h (the catalogue and the SEEDED generator), and
 *  NONE of them is a new scale or chord vocabulary: every name they use comes
 *  from InstrumentFunctionNoteStacking::ChordTable - the 95 entries behind the
 *  piano roll's own chord and scale selectors - through
 *  include/ChordVocabulary.h, which is a read-only view over it. This group is
 *  what makes any of it drivable, and it is the ONLY way to reach it: there is
 *  no chord-track lane, no chord ruler and no generator panel in this
 *  release's interface (docs/CHORD-TRACK.md).
 *
 *  Two halves in two translation units (the automation, warp, vca and
 *  chain-preset groups' read/edit split): ControlCommandsChord.cpp holds the
 *  three reads and the group's ONLY registration point, and
 *  ControlCommandsChordEdit.cpp the six writers. The split is also a
 *  reversibility split: the four track verbs record an ACTION checkpoint (the
 *  track is project state the Song's journal checkpoint does not carry), the
 *  two generators reverse through the CLIP's own journal checkpoint. */
LMMS_EXPORT void registerChordCommands(ControlRegistry& registry);
//! chord.set / chord.remove / chord.clear / chord.detect_to_track - the four
//! TRACK-editing verbs (their inverse is a recorded action checkpoint).
LMMS_EXPORT void registerChordEditCommands(ControlRegistry& registry);
//! chord.track_write / chord.progression_generate - the group's two GENERATORS,
//! in their own translation unit (this file's own split, and the group's
//! reversibility seam: they write notes into a clip, so their inverse is the
//! clip's own journal checkpoint rather than a recorded action).
LMMS_EXPORT void registerChordWriteCommands(ControlRegistry& registry);
/*! project.missing_assets / project.hash_assets / project.relink - the
 * project-asset reference group (feature row 38: "project collection /
 * archive, hashing, relink").
 *
 * The engine half is include/ControlProjectAssets.h (the READ TU
 * src/core/ControlProjectAssets.cpp and the WRITE TU
 * src/core/ControlProjectAssetsRelink.cpp); this group is its control surface.
 * Detection and hashing read a project FILE - so they answer for a project
 * that cannot be loaded and for one that is not open - and relink rewrites the
 * one reference it was asked to, through a recorded action checkpoint.
 *
 * The PORTABLE-BUNDLE half of the feature row is OUT (Bar 3): nothing in this
 * group copies media or writes any file except the one project file relink was
 * given. Named in docs/KNOWN-LIMITATIONS.md and the release notes.
 */
LMMS_EXPORT void registerProjectArchiveCommands(ControlRegistry& registry);
/*! project.media_usage / project.media_cleanup - the Project Media completion (Q050-121):
 *  per-file reference counts from the saved project document, the unused files in the managed
 *  `media` folder, and the rechecked removal of a selected set of those unused files.
 *  The engine half is include/ControlProjectMedia.h. The cleanup is IRREVERSIBLE: a deleted
 *  media file is not restorable by project undo. */
LMMS_EXPORT void registerProjectMediaCommands(ControlRegistry& registry);
/*! The mmpz-git depth group (feature row 42, task #612): project.merge,
 *  project.diff, project.conflicts, project.audible_diff, over
 *  tools/mmpz-git/mmpz_git.py.  No compile-time switch: shipped everywhere.
 */
LMMS_EXPORT void registerProjectMmpzGitCommands(ControlRegistry& registry);

/*! The declarations that do not fit beside their own group are here: include/ControlRegistry.h
 *  includes this header, in the same namespace with the same signatures, so a caller
 *  includes Registry.h exactly as before. track.move (task #664, feature row 75) moved
 *  here by the 0.3.0-alpha wave-2 train, whose registrations took that header to 504
 *  lines; it is back to the 502 it inherited. */
LMMS_EXPORT void registerTrackStructureCommands(ControlRegistry& registry);

//! control.id_contract - the stable-id contract as a readable table (feature row 51).
//! Split out of ControlCommandsControl.cpp for the file-length ratchet: src/core/ControlCommandsIdContract.cpp.
LMMS_EXPORT void registerIdContractCommand(ControlRegistry& registry);
//! project.undo_history / project.set_undo_history - WF-033, the undo history saved with the project
//! (src/core/ControlCommandsUndoHistory.cpp, docs/UNDO-HISTORY-PERSISTENCE.md).
LMMS_EXPORT void registerUndoHistoryCommands(ControlRegistry& registry);
/*! controller.surface_state / soft_takeover / feedback / template_save /
 *  template_list / template_apply / template_delete - the ENGINE HALF of MIDI
 *  controller surfaces (feature row 19). The engine is
 *  include/ControllerSurface.h (the template store) with the per-binding
 *  soft-takeover and LED/feedback state on MidiController
 *  (include/MidiController.h) and the output measurement on MidiPort
 *  (include/MidiPort.h).
 *
 *  It builds ON the landed MIDI-learn path (src/core/MidiLearn.cpp, the
 *  midi.learn_toggle / midi.device_list ids): a control bound by learn, by a
 *  project load or by controller.template_apply all answer to the same
 *  address, the model's fullDisplayName().
 *
 *  OSC IS OUT (Bar 3): this group is MIDI controller surfaces only. There is no
 *  interface for any of it in this release - the group is the only way to reach
 *  it. Named in docs/KNOWN-LIMITATIONS.md and the release notes. */
LMMS_EXPORT void registerControllerSurfaceCommands(ControlRegistry& registry);
/*! controller.template_save / template_list / template_apply / template_delete -
 *  the four FILE verbs of the same group, in their own translation unit
 *  (src/core/ControlCommandsControllerTemplates.cpp) because the group measured
 *  706 lines against the 500-line whole-tree ratchet and a fork-NEW file over
 *  the limit fails the gate. Called by registerControllerSurfaceCommands; the
 *  registry still has exactly one controller.* registration point. */
LMMS_EXPORT void registerControllerTemplateCommands(ControlRegistry& registry);

/*! The Session View's completion halves (board task #641, the #596 work):
 *  Follow Actions and Arrangement Record.
 *
 *  - registerSessionFollowCommands: session.follow_set / session.follow_get_state
 *    - arm one cell's Follow Action chain and read the engine's fires back. The
 *    engine half is include/SessionFollow.h and src/core/SessionFollow.cpp (the
 *    evaluation the data layer's FollowAction chains have carried since #594 and
 *    nothing consulted).
 *  - registerSessionRecordCommands: session.arrangement_record_arm /
 *    _status / _land plus session.back_to_arrangement - the performance's own
 *    event ring (include/SessionArrangementRecorder.h) and the one verb that
 *    writes it into the arrangement timeline as clips.
 *
 *  Both travel with the Session View data layer, native engine code since
 *  ARCH-4 S10 removed WANT_SESSION_VIEW, so registerControlCommands() calls
 *  them unconditionally; the registry must still not carry
 *  ids whose handler could not exist - the rule the telemetry.*, wasm.* and
 *  stem.* groups follow. Their six A16 rows are guarded by the same switch in
 *  src/core/ControlReversibilityTableSessionView.cpp, so the table and the
 *  registry stay consistent in both directions.
 *
 *  These are declared HERE rather than in include/ControlRegistry.h because that
 *  header is at the file-length ratchet (502 lines) and this header is where the
 *  groups that outgrew it are declared. There is no interface for any of it in
 *  this release: an agent can drive all of it through --control-socket, and
 *  nobody can from the UI (docs/KNOWN-LIMITATIONS.md). */
LMMS_EXPORT void registerSessionFollowCommands(ControlRegistry& registry);
//! R5.2: session.scene_follow_set (ControlCommandsSessionSceneFollow.cpp).
LMMS_EXPORT void registerSessionSceneFollowCommands(ControlRegistry& registry);
LMMS_EXPORT void registerSessionRecordCommands(ControlRegistry& registry);
/*! session.arrangement_record_land - the one WRITING verb of the Arrangement
 *  Record group, in its own translation unit (the automation, warp, vca and
 *  chain-preset groups' read/edit split, and here also the file-length ratchet's
 *  reason: the group measured 518 lines against the 500-line limit as one file).
 *  It creates the arrangement clips the recorded performance describes, over one
 *  Track journal checkpoint per touched track. Called by
 *  registerSessionRecordCommands; the registry has exactly one registration point
 *  for this half. */
LMMS_EXPORT void registerSessionRecordLandCommands(ControlRegistry& registry);
//! 0.5.0 (WF-054): session.pattern_list (ControlCommandsSessionPatterns.cpp).
LMMS_EXPORT void registerSessionPatternCommands(ControlRegistry& registry);
//! 0.5.0 (WF-126): surface.mcu_* - a Mackie Control surface (ControlCommandsMcu.cpp).
LMMS_EXPORT void registerMcuCommands(ControlRegistry& registry);
//! 0.5.0 L04 (ED-048): marker.* (ControlCommandsMarkers.cpp).
LMMS_EXPORT void registerMarkerCommands(ControlRegistry& registry);
//! 0.5.0 L04 (ED-011/012): range.* (ControlCommandsRange.cpp).
LMMS_EXPORT void registerRangeCommands(ControlRegistry& registry);
//! 0.5.0 L04 (ED-042): clip.gain_envelope_set / get (ControlCommandsClipEnvelope.cpp).
LMMS_EXPORT void registerClipEnvelopeCommands(ControlRegistry& registry);
//! 0.5.0 L07 (ED-130): clip.controllers_get / set (ControlCommandsClipControllers.cpp).
LMMS_EXPORT void registerClipControllerCommands(ControlRegistry& registry);
//! 0.5.0 L07 (ED-145/113): track.input_transform_get / set (ControlCommandsInputTransform.cpp).
LMMS_EXPORT void registerInputTransformCommands(ControlRegistry& registry);
//! 0.5.0 L07 (ED-137): warp.quantize - quantise audio (ControlCommandsWarpQuantize.cpp).
LMMS_EXPORT void registerWarpQuantizeCommand(ControlRegistry& registry);
//! 0.5.0 L07 (ED-122): note.pitch_curve_get / set (ControlCommandsNoteCurves.cpp).
LMMS_EXPORT void registerNoteCurveCommands(ControlRegistry& registry);
//! 0.5.0 L07 (ED-078): note.across - one note edit over several clips (ControlCommandsNoteAcross.cpp).
LMMS_EXPORT void registerNoteAcrossCommand(ControlRegistry& registry);
//! 0.5.0 L05 (ED-150/162/163): clip.transients / split_at / slice_to_instrument (ControlCommandsAudioSlice.cpp).
LMMS_EXPORT void registerAudioSliceCommands(ControlRegistry& registry);
//! 0.5.0 L13 (WF-124): controller.map / unmap (ControlCommandsControllerMap.cpp).
LMMS_EXPORT void registerControllerMapCommands(ControlRegistry& registry);
//! 0.5.0 L09 (MX-037): mixer.listen / listen_mode (ControlCommandsMixerListen.cpp).
LMMS_EXPORT void registerMixerListenCommands(ControlRegistry& registry);
//! 0.5.0 L09 (MX-070/071): vca.set_parent / set_links (ControlCommandsVcaNest.cpp, registered by registerVcaCommands).
LMMS_EXPORT void registerVcaNestCommands(ControlRegistry& registry);
//! 0.5.0 L08: mixer.set_many and the strip settings (ControlCommandsMixerStrip.cpp).
LMMS_EXPORT void registerMixerStripCommands(ControlRegistry& registry);
LMMS_EXPORT void registerMixerRenameCommands(ControlRegistry& registry);  //!< 0.5.0 theme 13: mixer.rename_channel (ControlCommandsMixerRename.cpp)
//! 0.5.0 L10 (MX-076/092): plugin.copy, chain.copy, plugin.ab (ControlCommandsDeviceCopy.cpp).
LMMS_EXPORT void registerDeviceCopyCommands(ControlRegistry& registry);
//! 0.5.0 L10 (MX-166): plugin.organise / plugin.organisation (ControlCommandsPluginOrganise.cpp).
LMMS_EXPORT void registerPluginOrganiseCommands(ControlRegistry& registry);
//! 0.5.0 L10 (MX-163): plugin.paths_get / plugin.paths_set (ControlCommandsPluginPaths.cpp).
LMMS_EXPORT void registerPluginPathsCommands(ControlRegistry& registry);
//! 0.5.0 L10 (MX-103): rack.wrap (ControlCommandsRackWrap.cpp).
LMMS_EXPORT void registerRackWrapCommand(ControlRegistry& registry);
//! 0.5.0 L10 (MX-106): remote.connect / disconnect / target / page / state (ControlCommandsRemote.cpp).
LMMS_EXPORT void registerRemoteCommands(ControlRegistry& registry);
//! 0.5.0 L10 (MX-111): track.drum_rack (ControlCommandsDrumRack.cpp).
LMMS_EXPORT void registerDrumRackCommand(ControlRegistry& registry);
//! 0.5.0 L03 (WF-080 / WF-082 / ED-168): transport.record_options, comp.cycle (ControlCommandsRecordingPass.cpp).
LMMS_EXPORT void registerRecordingPassCommands(ControlRegistry& registry);
//! 0.5.0 L02 (WF-069): session.bindings_connect / bind / bind_learn / unbind / bindings (ControlCommandsLaunchBindings.cpp).
LMMS_EXPORT void registerLaunchBindingCommands(ControlRegistry& registry);
//! 0.5.0 L02 (WF-055): session.capture_to_slot (ControlCommandsSessionCapture.cpp).
LMMS_EXPORT void registerSessionCaptureCommand(ControlRegistry& registry);
//! 0.5.0 L02 (WF-055): session.record_slot / record_slot_status / record_slot_land - audio into a launcher cell
//! (ControlCommandsSessionAudioRecord.cpp).
LMMS_EXPORT void registerSessionAudioRecordCommands(ControlRegistry& registry);
//! 0.5.0 L10 (Q050-156): record.sampler_capture / sampler_capture_stop / sampler_capture_status /
//! sampler_capture_land - an input take recorded straight into a sampler instrument
//! (ControlCommandsSamplerTake.cpp).
LMMS_EXPORT void registerSamplerTakeCommands(ControlRegistry& registry);
//! 0.5.0 L01 (MX-159): transport.low_latency (ControlCommandsLowLatency.cpp).
LMMS_EXPORT void registerLowLatencyCommand(ControlRegistry& registry);
//! 0.5.0 L11 (WF-006): browser.collection_save / remove / collections / collection_run.
LMMS_EXPORT void registerBrowserCollectionCommands(ControlRegistry& registry);
//! 0.5.0 L11 (WF-024 / WF-016): track.preset_save / preset_load, plugin.replace (ControlCommandsTrackPresets.cpp).
LMMS_EXPORT void registerTrackPresetCommands(ControlRegistry& registry);
//! 0.5.0 L12 (ED-205): range.bounce (ControlCommandsRangeBounce.cpp).
LMMS_EXPORT void registerRangeBounceCommand(ControlRegistry& registry);
//! 0.5.0 L12 (WF-036): project.import_tracks (ControlCommandsImportTracks.cpp).
LMMS_EXPORT void registerImportTracksCommand(ControlRegistry& registry);
//! 0.5.0 L12 (WF-049): project.handoff (ControlCommandsHandoff.cpp).
LMMS_EXPORT void registerHandoffCommand(ControlRegistry& registry);
//! 0.5.0 L04 (ED-030/032): clip.repeat, clip.time_scale (ControlCommandsClipArrange.cpp).
LMMS_EXPORT void registerClipArrangeCommands(ControlRegistry& registry);
//! 0.5.0 L06: automation.lane_add / add_trim / draw_shape / transform / thin / set_curve /
//! last_touched (ControlCommandsAutomationLanes.cpp).
LMMS_EXPORT void registerAutomationLaneCommands(ControlRegistry& registry);
//! 0.5.0 L07: note.scale / ratchet / list / transform_where (ControlCommandsNoteShape.cpp) and
//! key_names.* (ControlCommandsKeyNames.cpp).
LMMS_EXPORT void registerNoteShapeCommands(ControlRegistry& registry);
LMMS_EXPORT void registerKeyNameCommands(ControlRegistry& registry);
//! 0.5.0 L07 (W3): note.invert / arpeggiate / flam / transpose_in_key / repeat / generate
//! (ControlCommandsNoteTools.cpp).
LMMS_EXPORT void registerNoteToolCommands(ControlRegistry& registry);
//! 0.5.0 L07 (W3): note.move_to_track / split_by_pitch, scale.detect, transport.panic
//! (ControlCommandsNoteMoves.cpp).
LMMS_EXPORT void registerNoteMoveCommands(ControlRegistry& registry);
//! 0.5.0 L07 (W3): note.follow_chords, clip.quantize, scale.define / custom (ControlCommandsNoteHarmony.cpp).
LMMS_EXPORT void registerNoteHarmonyCommands(ControlRegistry& registry);
//! 0.5.0 L07 (W3): note.lyrics / group / ungroup, track.articulations(_set), transport.groove /
//! midi_options (ControlCommandsNotePlayback.cpp).
LMMS_EXPORT void registerNotePlaybackCommands(ControlRegistry& registry);
//! 0.5.0 L08 (W3): mixer.options / send_enable / create_return / find / adjust / order_by_tracks,
//! vca.color_members, chain.bypass_all / clean (ControlCommandsMixerTools.cpp).
LMMS_EXPORT void registerMixerToolCommands(ControlRegistry& registry);
//! 0.5.0 L02 (W3): session.release_slot / jump_slot / capture_scene / set_playlist / launch_group /
//! setlist / setlist_go (ControlCommandsSessionPerform.cpp).
LMMS_EXPORT void registerSessionPerformCommands(ControlRegistry& registry);
//! 0.5.0 L02 (WF-064): session.subscene_create / subscene_rename / subscene_delete / subscene_launch
//! (ControlCommandsSessionSubScenes.cpp).
LMMS_EXPORT void registerSessionSubSceneCommands(ControlRegistry& registry);
//! 0.5.0 L03 (W3): comp.move_edge / select_group (ControlCommandsCompGroup.cpp); transport.metronome_options /
//! punch_here, record.remaining (ControlCommandsRecordTools.cpp).
LMMS_EXPORT void registerCompGroupCommands(ControlRegistry& registry);
LMMS_EXPORT void registerRecordToolCommands(ControlRegistry& registry);
//! 0.5.0 L04 (W3): clip.list / remove_silence, arrangement.next_transient (ControlCommandsArrangeTools.cpp);
//! section.* (ControlCommandsSections.cpp).
LMMS_EXPORT void registerArrangeToolCommands(ControlRegistry& registry);
LMMS_EXPORT void registerSectionCommands(ControlRegistry& registry);
//! 0.5.0 L05/L06 (W3): sample.crop / make_unique, clip.fade_curve (ControlCommandsSampleTools.cpp); automation.switches /
//! trim_get / trim_set / move_point (ControlCommandsAutomationTools.cpp).
LMMS_EXPORT void registerSampleToolCommands(ControlRegistry& registry);
LMMS_EXPORT void registerAutomationToolCommands(ControlRegistry& registry);
//! 0.5.0 L05 (W3): clip.audio_to_midi, sample.spectrogram (ControlCommandsAudioAnalysis.cpp).
LMMS_EXPORT void registerAudioAnalysisCommands(ControlRegistry& registry);
//! 0.5.0 L04 (Q050-031): clip.import_file, a dropped MIDI or audio file as a clip (ControlCommandsClipImportFile.cpp).
LMMS_EXPORT void registerClipImportFileCommands(ControlRegistry& registry);
//! 0.5.0 L05 (ED-156): sample.edit_range (ControlCommandsSampleRange.cpp).
LMMS_EXPORT void registerSampleRangeCommands(ControlRegistry& registry);
//! 0.5.0 L05 (Q050-106): sample.split_stereo, the stereo source as two mono WAV files
//! (ControlCommandsSampleStereo.cpp).
LMMS_EXPORT void registerSampleStereoCommands(ControlRegistry& registry);
//! 0.5.0 L09 (MX-053, MX-140, MX-042): mixer.send_meters, mixer.eq_compare, browser.preview_output, monitor.control_room
//! (ControlCommandsMixerMeters.cpp).
LMMS_EXPORT void registerMixerMeterCommands(ControlRegistry& registry);
//! 0.5.0 L09 (MX-062): track.midi_out (ControlCommandsTrackMidiOut.cpp).
LMMS_EXPORT void registerTrackMidiOutCommands(ControlRegistry& registry);
//! 0.5.0 L04 (ED-049): track.version_list / save / switch / remove (ControlCommandsTrackVersions.cpp).
LMMS_EXPORT void registerTrackVersionCommands(ControlRegistry& registry);
//! 0.5.0 L07 (ED-086): clip.export_musicxml (ControlCommandsScoreExport.cpp).
LMMS_EXPORT void registerScoreExportCommands(ControlRegistry& registry);
//! 0.5.0 W3 L13 / L14 (customisation, help and access). Interface-side groups, defined in src/gui:
//! theme.* (ControlCommandsTheme.cpp).
LMMS_EXPORT void registerThemeCommands(ControlRegistry& registry);
//! track.overview / set_color / set_visible / set_icon, mixer.cpu_load (src/core/ControlCommandsTrackLooks.cpp).
LMMS_EXPORT void registerTrackLookCommands(ControlRegistry& registry);
//! app.ask / app.whats_new (src/core/ControlCommandsAssistant.cpp), and what window.whats_new calls once the
//! notes are on screen: this version's notes count as shown.
LMMS_EXPORT void registerAssistantCommands(ControlRegistry& registry);
LMMS_EXPORT void markWhatsNewSeen();
//! window.stay_on_top / tidy / large_clock / tuner / ask / whats_new / focus_panel / accessibility_report and
//! view.crosshair (src/gui/ControlCommandsWorkspace.cpp).
LMMS_EXPORT void registerWorkspaceCommands(ControlRegistry& registry);
//! location.* / selection.* (src/gui/ControlCommandsViewMemory.cpp); control.history_jump
//! (src/core/ControlCommandsHistoryJump.cpp).
LMMS_EXPORT void registerViewMemoryCommands(ControlRegistry& registry);
LMMS_EXPORT void registerHistoryJumpCommand(ControlRegistry& registry);
//! settings.search / export / import / preset_save / preset_apply / preset_list / preset_remove
//! (src/core/ControlCommandsSettingsPresets.cpp).
LMMS_EXPORT void registerSettingsPresetCommands(ControlRegistry& registry);
//! tuner.set / tuner.get_state (src/core/ControlCommandsTuner.cpp).
LMMS_EXPORT void registerTunerCommands(ControlRegistry& registry);
//! 0.5.0 L12 (W3): mixer.snapshot_store / recall / list / remove (ControlCommandsMixerSnapshots.cpp); project.settings /
//! metadata / locks, track.set_locked, clip.set_locked (ControlCommandsProjectInfo.cpp).
LMMS_EXPORT void registerMixerSnapshotCommands(ControlRegistry& registry);
LMMS_EXPORT void registerProjectInfoCommands(ControlRegistry& registry);
//! 0.5.0 L11 (W3): browser.save_clip (ControlCommandsLibraryClips.cpp); browser.reindex / index_status / index_query /
//! similar (ControlCommandsBrowserIndex.cpp); browser.thumbnail / preview / preview_stop (ControlCommandsBrowserMedia.cpp);
//! browser.browse / history / history_go / sources / remember_source (ControlCommandsBrowserNavigation.cpp).
LMMS_EXPORT void registerLibraryClipCommands(ControlRegistry& registry);
LMMS_EXPORT void registerBrowserIndexCommands(ControlRegistry& registry);
LMMS_EXPORT void registerBrowserMediaCommands(ControlRegistry& registry);
LMMS_EXPORT void registerBrowserNavigationCommands(ControlRegistry& registry);
//! 0.5.0 L12 (W3): render.queue_add / convert / queue_list / queue_cancel / queue_clear / queue_report, app.tasks /
//! task_cancel (ControlCommandsRenderQueue.cpp); track.flatten (ControlCommandsTrackFlatten.cpp).
LMMS_EXPORT void registerRenderQueueCommands(ControlRegistry& registry);
LMMS_EXPORT void registerTrackFlattenCommands(ControlRegistry& registry);
//! 0.5.0 L12 (W3): project.subproject_add / subprojects / subproject_refresh (ControlCommandsSubprojects.cpp);
//! project.alternative_save / alternatives / alternative_switch / alternative_remove (ControlCommandsAlternatives.cpp).
LMMS_EXPORT void registerSubprojectCommands(ControlRegistry& registry);
LMMS_EXPORT void registerAlternativeCommands(ControlRegistry& registry);
//! 0.5.0 L10 (W3): rack.split, mixer.console_apply, plugin.impulse_load (ControlCommandsDeviceTools.cpp); plugin.param_search / param_rename / randomise /
//! blend / fold / options / delta (ControlCommandsDeviceParams.cpp).
LMMS_EXPORT void registerDeviceToolCommands(ControlRegistry& registry);
LMMS_EXPORT void registerDeviceParamCommands(ControlRegistry& registry);
//! 0.5.0 L10 (W3): modulator.formula_set / trigger_set (ControlCommandsModulatorTools.cpp).
LMMS_EXPORT void registerModulatorToolCommands(ControlRegistry& registry);
//! 0.5.0 L10 (W3): plugin.record / recorder_land (ControlCommandsRecorder.cpp).
LMMS_EXPORT void registerRecorderCommands(ControlRegistry& registry);
//! 0.5.0 L10 (W3): clip.process (ControlCommandsClipProcess.cpp).
LMMS_EXPORT void registerClipProcessCommand(ControlRegistry& registry);
//! 0.5.0 W3 engine (MX-160, MX-082, MX-063, ED-179): automation.latency_compensation, mixer.post_inserts, chain.receive,
//! clip.pre_fade (ControlCommandsW3Engine.cpp).
LMMS_EXPORT void registerW3EngineCommands(ControlRegistry& registry);
//! 0.5.0 Q050-040 (L07): clip.set_mute - the song editor's own clip mute dispatches this
//! command too, so GUI and agents share one implementation (ControlCommandsClipMute.cpp;
//! its own unit because the clip group's file ratchet is not moved for a new feature).
LMMS_EXPORT void registerClipMuteCommands(ControlRegistry& registry);
} // namespace lmms

#endif // LMMS_CONTROL_REGISTRY_GROUPS_H
