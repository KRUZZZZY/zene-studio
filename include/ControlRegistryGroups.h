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

/*! clock.get_state / clock.master_set / clock.slave_set - MIDI clock, the DAW
 *  as a clock master and as a clock slave. The engine half is include/MidiClock.h
 *  and its sources (the pulse generator, the tempo tracker, the widened MIDI
 *  input and output switches and the per-audio-period hook in
 *  Song::processNextBuffer); this group is what makes any of it drivable, and it
 *  is the ONLY way to reach it - there is no interface for a MIDI clock in this
 *  release. MTC is not generated: the engine has no frame rate or SMPTE offset
 *  to build one from, and clock.get_state reports that as `mtc: "absent"`. */
LMMS_EXPORT void registerClockCommands(ControlRegistry& registry);

} // namespace lmms

#endif // LMMS_CONTROL_REGISTRY_GROUPS_H
