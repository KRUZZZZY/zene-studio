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
 *  The feature is opt-in at configure time: WANT_STEM_SPLIT defaults to OFF
 *  (CMakeLists.txt:120) and a default build compiles none of the engine, so the
 *  registry must not carry ids whose handler could not exist - the rule the
 *  telemetry.*, session.* and wasm.* groups follow. Its call site is guarded by
 *  the same #ifdef, and so are its seven A16 rows, so the registry and the
 *  contract table stay consistent in both directions. */
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
//! browser.tag.add / browser.tag.remove - the mutating half, in its own
//! translation unit (the automation and warp groups' read/edit split). Both
//! record a snapshot-class transaction whose inverse is the paired command.
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
 *  set_session_tempo - session tempo and beat-phase sync (D11 "Ableton Link
 *  sync"). The engine half is include/LinkSync.h and
 *  include/LinkPeerTransport.h (the seam a real Ableton Link transport would
 *  implement); this group is what makes any of it drivable, and it is the ONLY
 *  way to reach it - there is no interface for session sync in this release.
 *  The model is this project's own ("zene-link-style": Link's semantics, not
 *  Link's library, which is not vendored - the licence finding that vendoring
 *  it is permitted is docs/LINK-SYNC.md section 1). */
LMMS_EXPORT void registerLinkCommands(ControlRegistry& registry);

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
 * the automation and warp groups' split. ableton-gap/AGENT-TOOLING.md:186 names
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
 * ableton-gap/AGENT-TOOLING.md:269 boards) is the scan the factory already has.
 */
LMMS_EXPORT void registerPluginScanCommands(ControlRegistry& registry);
//! plugin.scan_cache_quarantine_add / plugin.scan_cache_quarantine_remove /
//! plugin.rescan - the mutating half of the scan-cache group, in its own
//! translation unit (the automation and warp groups' split).
LMMS_EXPORT void registerPluginScanEditCommands(ControlRegistry& registry);
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
/*! crash.list_reports / crash.acknowledge_report / crash.discard_report /
 * crash.upload_report - the crash reporter's agent surface (feature row 54).
 * The engine half is include/CrashReporter.h, installed from main() before this
 * socket exists; crash.list_reports is the read ableton-gap/AGENT-TOOLING.md:194
 * names, the two writers are the module's acknowledge/discard operations, and
 * crash.upload_report is registered and REFUSES because this build has no
 * network code at all (the automation.mode_set shape).
 */
LMMS_EXPORT void registerCrashReporterCommands(ControlRegistry& registry);
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
 *    docs/specs/SPEC-slide-notes and never had an id for.
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
/*! record.get_state / arm_track / disarm_track / disarm_all - the ROUTE verbs of
 *  the `record.` group, in their own translation unit (0.3.0, feature rows 14 and
 *  64; docs/RECORD-INPUTS.md): the multi-track recorder's own state, and the verbs
 *  that arm and stop one of its routes. Declared here rather than beside the other
 *  record.* declarations because include/ControlRegistry.h sits at the
 *  file-length ratchet's limit (500 lines) and this header is where the groups
 *  that outgrew that budget are declared. Called by
 *  registerControlCommands() beside registerRecordingInputCommands(). */
LMMS_EXPORT void registerRecordingRouteCommands(ControlRegistry& registry);
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
} // namespace lmms

#endif // LMMS_CONTROL_REGISTRY_GROUPS_H
