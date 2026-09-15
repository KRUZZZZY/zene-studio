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
} // namespace lmms

#endif // LMMS_CONTROL_REGISTRY_GROUPS_H
