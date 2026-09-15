/*
 * ControlRegistryRegistrations.cpp - the registry's COMPOSITION: which command
 *                                    groups registerControlCommands() brings up,
 *                                    and in what order.
 *
 * WHY IT IS ITS OWN TRANSLATION UNIT. registerControlCommands() is the one
 * function that knows the whole surface, so every command-group lane appends a
 * call - and usually a rationale comment - to it. The train that merged the
 * `vca.*` group and the routing surface into release/0.3.0 appended both at
 * once and the file crossed the 500-line file ratchet (502 lines, 494 before
 * the merge), so the composition MOVED here, verbatim, rather than the ratchet
 * moving for it. The seam is real: this file answers "which groups exist in
 * this configuration", ControlRegistry.cpp answers "what a command is and what
 * the registry does with one".
 *
 * The rationale for each group is on its declaration in
 * include/ControlRegistryGroups.h (which ControlRegistry.h includes); the
 * #ifdef blocks below are the only place that decides which groups a
 * configuration gets, so a group whose handler could not exist in this build is
 * never registered.
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

#include "ControlRegistry.h"

// The #ifdef blocks in registerControlCommands() below use the two macros the
// build sets (LMMS_HAVE_SESSION_VIEW, ZENE_TELEMETRY_ENABLED); they live in
// lmmsconfig.h, which reached ControlRegistry.cpp transitively and did NOT
// reach this file. Without this include a session/telemetry build would silently
// register neither group - the failure was measured (ReversibilityContractTest:
// "session.clear is in the table but not registered") and this include is the
// fix. Included by name rather than relying on another header's chain.
#include "lmmsconfig.h"

namespace lmms
{

void registerControlCommands(ControlRegistry& registry)
{
	registerControlGroupCommands(registry);
	registerTransportCommands(registry);
	// The structural half of track.* (task #664): track.move, the arrangement's
	// order. Registered beside the transport/track group it extends, for the
	// same reason the tempo map's unit is: one group, several TUs.
	registerTrackStructureCommands(registry);
	registerTransportTempoMapCommands(registry);
	registerTransportPunchCommands(registry);
	registerRecordingCommands(registry);
	registerRecordingRecoveryCommands(registry);
	// The capture-IN half of the `record.` group (0.3.0, feature rows 14 and
	// 64) and the retrospective AUDIO capture half (row 16), each its own
	// translation unit. No compile-time switch: the input path's plan is a
	// config value and the recorder is a member of the engine in every
	// configuration, so the ids are honest in every one (a build with no
	// capture device says so in record.input_get_state rather than hiding the
	// command).
	registerRecordingInputCommands(registry);
	registerRecordingRouteCommands(registry);
	registerRecordingRetroCommands(registry);
	registerMixerCommands(registry);
	registerProjectCommands(registry);
	// The project-asset reference group (feature row 38): detection, hashing and
	// relink over a project FILE.
	registerProjectArchiveCommands(registry);
	// The mmpz-git depth group (feature row 42, task #612): 3-way merge, diff,
	// conflict reporter and audible-diff wrappers around tools/mmpz-git/mmpz_git.py.
	registerProjectMmpzGitCommands(registry);

	registerSurfaceCommands(registry);
#ifdef ZENE_TELEMETRY_ENABLED
	// The telemetry.* group travels with the client. -DZENE_TELEMETRY=OFF
	// removes the client, so the registry must not carry ids that would
	// describe commands no handler in this binary could answer.
	registerTelemetryCommands(registry);
#endif

	registerArrangementCommands(registry);
	registerClipCommands(registry);
	registerClipEditsCommands(registry);
	registerClipTrimCommands(registry);
	registerNoteCommands(registry);
	registerNoteProbabilityCommands(registry);
	registerRenderStemsCommands(registry);
	registerPluginCommands(registry);
	registerDspCommands(registry);
	registerSettingsCommands(registry);
	registerMidiRetroCaptureCommands(registry);
	// The controller-surface group (feature row 19): soft-takeover, LED/feedback
	// output and the mapping templates, on top of the MIDI-learn path above.
	registerControllerSurfaceCommands(registry);
	registerAutomationCommands(registry);
	registerAutomationEditCommands(registry);
	registerWarpCommands(registry);
	// The groove pool and quantise surface (docs/GROOVE-POOL.md): no
	// compile-time switch, because the pool is a plain value on Song and the
	// arithmetic is a function over a note list, so its ids are honest in every
	// configuration.
	registerGrooveCommands(registry);
	registerScriptCommands(registry);
#ifdef LMMS_HAVE_SESSION_VIEW
	// The session.* group travels with the Session View data layer: without
	// LMMS_HAVE_SESSION_VIEW there is no grid to address, and the registry
	// must not carry ids whose handler could not exist (the same rule the
	// telemetry.* group above follows). The A16 table guards its rows with the
	// same #ifdef, so the two stay consistent in both directions.
	registerSessionCommands(registry);
	registerSessionLaunchCommands(registry);
#endif // LMMS_HAVE_SESSION_VIEW
	registerExportCommands(registry);
	// The render/export presets (feature row 70) - the saved store and the
	// selection the next render is started with. No compile-time switch: the
	// store is a directory in the user preset tree and the applied selection is
	// a plain value, so its ids are honest in every configuration.
	registerExportPresetCommands(registry);
	registerRackCommands(registry);
	// The chain-preset group: the store and its read/capture half, then the
	// edit half (chain.apply / rename / remove), each its own translation unit.
	registerChainReadCommands(registry);
	registerChainEditCommands(registry);
	// The browser group carries no compile-time switch: it reads directories and
	// a JSON file in the config directory, both of which exist in every
	// configuration, so its ids are always honest.
	registerBrowserCommands(registry);
	// Take lanes and the composite (comping, task #600): the take half and the
	// composite half, each its own translation unit.
	registerCompCommands(registry);
	registerCompEditCommands(registry);
#ifdef LMMS_HAVE_WASM
	// The wasm.* group travels with the WASM DSP sandbox: without LMMS_HAVE_WASM
	// there is no wasmtime to host a module in, and the registry must not carry
	// ids whose handler could not exist (the rule the telemetry.* and session.*
	// groups above follow). The A16 table guards its six rows with the same
	// #ifdef, so the two stay consistent in both directions.
	registerWasmCommands(registry);
#endif // LMMS_HAVE_WASM
	// The stem.* group travels with the offline stem-separation engine, the
	// same rule again: without LMMS_HAVE_STEM_SPLIT there is no StemJobManager,
	// no backend and no model store, so a default build (WANT_STEM_SPLIT=OFF,
	// CMakeLists.txt:120) must not carry these ids. The A16 table guards its
	// seven stem rows with the same #ifdef.
#ifdef LMMS_HAVE_STEM_SPLIT
	registerStemCommands(registry);
#endif // LMMS_HAVE_STEM_SPLIT
	// The modulation layer (#602): the layer + LFO half, the route half, and the
	// per-note expression group. No compile-time switch - the layer is a plain
	// value on Song and a Note field, so its ids are honest in every
	// configuration.
	registerModulatorCommands(registry);
	registerModulatorRouteCommands(registry);
	registerNoteExpressionCommands(registry);
	// Session tempo/phase sync (D11 "Ableton Link sync"): the model in
	// include/LinkSync.h, its UDP multicast transport, and the link.* commands.
	// No compile-time switch: the model needs no audio device, no project and no
	// display, and reports its own transport's availability, so its ids are
	// honest in every configuration (docs/LINK-SYNC.md).
	registerLinkCommands(registry);
	// Freeze / bounce-in-place: the offline render (include/BounceInPlace.h) and
	// the frozen take a track then plays instead of its clips. No compile-time
	// switch: the render is the product's own CLI render path and the take is a
	// plain value on Track, so its ids are honest in every configuration.
	registerFreezeCommands(registry);
	// Folder tracks (owner items 3+20+21): a Track subclass that references its
	// children, its two modes and the named visibility sets. No compile-time
	// switch - a folder is a plain Track type and the sets are container state,
	// so its ids are honest in every configuration.
	registerTrackFolderCommands(registry);
	// MIDI clock (0.3.0): the DAW as a clock master and as a clock slave. No
	// compile-time switch; the bound it states is in docs/KNOWN-LIMITATIONS.md.
	registerClockCommands(registry);
	// VCA / mix-and-edit groups (OWNER-31 item 11): one fader over member mixer
	// channels, plus the phase-locked multitrack edit set. No compile-time
	// switch - a group is a plain entity on the Mixer and its edit set is a list
	// of stable track ids, so its ids are honest in every configuration.
	registerVcaCommands(registry);
	// The routing surface (feature rows 27-29) and the mixer's routing verbs; the
	// rationale for each group is on its declaration in ControlRegistryGroups.h.
	registerRoutingSurfaceCommands(registry);
	// The patcher node graph (feature row 69): the same graph routing.get_state
	// reads, addressed by ROLE and EDITABLE. Its own group because it writes
	// where routing.* is an inspector by decision; the rationale, the threading
	// argument and the bounds are on its declaration and in docs/PATCHER-GRAPH.md.
	registerPatcherCommands(registry);
	// The plugin scan cache and its quarantine list (feature row 46): the read
	// half, then the edit half, each its own translation unit.
	registerPluginScanCommands(registry);
	registerPluginScanEditCommands(registry);
	// CODE-4 (feature row 82): the chunking contract both plugin host paths
	// keep, and the counters their audio paths increment. The hosts are plugin
	// modules, so the counters and the command live in the core
	// (src/core/PluginHostChunking.cpp) and the modules write into them.
	registerPluginHostChunkingCommands(registry);
	// The crash reporter (feature row 54). No compile-time switch: the module is
	// a no-op on Windows rather than compiled out, and its read answers in every
	// configuration (reporting no directory there), so the ids are honest either
	// way and the two writers refuse, typed, when the reporter is not installed.
	registerCrashReporterCommands(registry);
	// Auto-mastering, wave 1 (feature rows 25 and 72; docs/AUTO-MASTERING.md):
	// the read half (the candidate set the engine generates and the last run's
	// report) and the one writer, which runs the shipped CLI action in a child
	// process. No compile-time switch: the chain, the job and the BS.1770-4
	// meter are in every configuration, so its ids are honest in every one.
	registerMasteringCommands(registry);
	registerMasteringRunCommands(registry);
	// The 0.3.0 note/scale/device wave (board task #648; feature-list rows 11, 66 and
	// 81): the note randomisation, transform and slide verbs, the scale.* group (read
	// half, the two context writers and the one clip-editing verb) and the registry's
	// first device.* group. No compile-time switch - every engine they drive
	// (NoteRandom, NoteTransform, ChordTable, MpeExpression) is in src/core in every
	// configuration, so their ids are honest in every one.
	registerNoteRandomCommands(registry);
	registerNoteSlideCommands(registry);
	registerNoteTransformCommands(registry);
	registerScaleCommands(registry);
	registerScaleEditCommands(registry);
	registerDeviceCommands(registry);
	// The loudness surface (feature row 24, "LUFS / loudness metering"): the live
	// master tap and the file measurement, plus the render-path report that
	// export.get_settings exposes. No compile-time switch: the BS.1770-4 meter,
	// the report and the tap are in every configuration, so its three ids are
	// honest in every one.
	registerMeterCommands(registry);
	// Linked / smart clips (feature-list row 6, board task #645): two clips that
	// share one source, so an edit to one is seen by all of them. No compile-time
	// switch - the relation is a clip attribute and the content channel is the
	// clip's own note list, so its ids are honest in every configuration. The
	// group's four ids are two translation units (the read/repair half is
	// registered by the same call chain, at its declaration).
	registerClipLinkCommands(registry);
	// Standard MIDI File conductor interchange (feature row 33 of
	// docs/FEATURE-LIST-0.3.0.md): the tempo map written as, and read back
	// from, a format-1 conductor track another DAW can read. No compile-time
	// switch - the encoder and the reader are plain value code over Song's
	// tempo map and a file, both of which exist in every configuration, so
	// its ids are honest in every one.
	registerInterchangeCommands(registry);
	// DAWproject import / export (feature row 37, docs/DAWPROJECT-INTERCHANGE.md):
	// the published format read and written - tracks, clips, the tempo map and the
	// mixer, with the losses reported. No compile-time switch - the writer, the
	// reader and the ZIP container are plain value code over the session and a
	// file, both of which exist in every configuration, so its ids are honest in
	// every one.
	registerDawProjectCommands(registry);
	// The chord track, its detection and its generators (feature row 35). No
	// compile-time switch: the track is plain project state on the Song, the
	// vocabulary is the piano roll's own ChordTable and the arithmetic is a
	// function over a note list, so its ids are honest in every configuration
	// (docs/CHORD-TRACK.md, include/ControlRegistryGroups.h).
	registerChordCommands(registry);
}

} // namespace lmms
