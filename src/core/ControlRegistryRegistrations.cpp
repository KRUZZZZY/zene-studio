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
	registerTransportTempoMapCommands(registry);
	registerTransportPunchCommands(registry);
	registerRecordingCommands(registry);
	registerRecordingRecoveryCommands(registry);
	registerMixerCommands(registry);
	registerProjectCommands(registry);
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
	// The plugin scan cache and its quarantine list (feature row 46): the read
	// half, then the edit half, each its own translation unit.
	registerPluginScanCommands(registry);
	registerPluginScanEditCommands(registry);
	// The crash reporter (feature row 54). No compile-time switch: the module is
	// a no-op on Windows rather than compiled out, and its read answers in every
	// configuration (reporting no directory there), so the ids are honest either
	// way and the two writers refuse, typed, when the reporter is not installed.
	registerCrashReporterCommands(registry);
}

} // namespace lmms
