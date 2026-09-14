/*
 * ControlReversibilityTableAction.cpp - THE SPEC A16 classification table's
 *                                       ACTION rows: the true_inverse rows
 *                                       whose inverse is a RECORDED OPERATION
 *                                       rather than a live object checkpoint.
 *
 * This file is data. It is the SECOND half of the table's first block, split
 * off along the seam ControlReversibilityTable.cpp's own header names - within
 * true_inverse the inverse is either the engine's own live object checkpoint
 * (a plain, a COMPOSITE or a parameter/model checkpoint) or an ACTION
 * checkpoint: a recorded undo step that runs when the stack unwinds, because
 * there is no live state to restore. This file holds the second kind:
 *
 *   ControlReversibilityTable.cpp         true_inverse, LIVE object checkpoint
 *   THIS FILE                             true_inverse, ACTION checkpoint
 *   ControlReversibilityTableSnapshot.cpp  snapshot
 *   ControlReversibilityTablePassive.cpp   irreversible + not_mutating
 *
 * The rows here are one created or deleted object (track.add / track.remove /
 * mixer.add_channel / rack.add_chain and the rack macro and zone operations,
 * which re-insert the captured item at its own index), one scalar the engine
 * does not journal (transport.seek, settings.set, audio.device_set,
 * export.set_dither, export.set_src_quality, project.restore_revision), one
 * captured XML block replayed whole (plugin.state_load, plugin.preset_load, and
 * the session.* edits, which restore the <session> block through
 * SessionModel::restoreState).
 *
 * reversibilityRowTable() in ControlReversibilityTable.cpp JOINS this block
 * with the live-checkpoint half, so callers - ReversibilityTable's constructor,
 * the anti-drift test and control.transactions - still read ONE true_inverse
 * block with ONE row count. The split exists because this fork's file-length
 * ratchet measures a file as a unit, and every lane that adds a command appends
 * a row here.
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

#include "ControlReversibility.h"

// lmmsconfig.h carries the ZENE_TELEMETRY_ENABLED and LMMS_HAVE_SESSION_VIEW
// switches. ControlReversibility.h pulls in neither, and the session.* rows
// below are guarded by the second one, so it has to be included explicitly -
// without it the #ifdef reads "off" in a session-view build and silently drops
// the six rows the registry declares.
#include "lmmsconfig.h"

/*!
 * The ACTION half of the first literal block of THE classification table: the
 * true_inverse rows whose inverse is a recorded operation (see the file header
 * for the seam and ControlReversibilityTable.cpp for the other half).
 */

namespace lmms
{
namespace control
{

namespace
{

using RC = ReversibilityClass;

//! A literal row: R(id, class, reversible, reason, mechanism, fallback).
#define R(id, cls, rev, reason, mechanism, fallback) \
	{ id, cls, reason, mechanism, fallback, rev, nullptr }
/*! A literal row for a command that COALESCES: RC(id, class, reversible,
 * reason, mechanism, fallback, target) is R plus \a target - the argument
 * name(s) whose VALUES identify the thing a gesture is being made on. Two
 * consecutive calls of the same command with the same target values are ONE
 * undo step; the rule and its bound are in docs/UNDO-BOUNDS.md.
 */
#define RC(id, cls, rev, reason, mechanism, fallback, target) \
	{ id, cls, reason, mechanism, fallback, rev, target }

const ReversibilityRow kActionRows[] = {

	// =====================================================================
	// true_inverse, the ACTION half: the inverse is a RECORDED OPERATION,
	// not a live object. The transaction's before-state carries what is
	// needed and the recorded undo step runs when the stack unwinds.
	// reversibilityRowTable() joins this block with the live-checkpoint half
	// in ControlReversibilityTable.cpp, so the two are still ONE table.
	// =====================================================================

	R("transport.seek", RC::TrueInverse, true,
		"the play head is engine state, not project state, and is not a "
		"JournallingObject - so no object checkpoint exists for it",
		"action checkpoint: the recorded undo step calls Song::setPlayPos with "
		"the tick the transaction's before-state holds",
		""),
	// Rows restored from ControlReversibilityTableTrueInverse.cpp, which this
	// structure retires: 030/w15-tempo-map branched before that split and wrote
	// its tempo-map rows into the old file, so the merge that took the split
	// left four REGISTERED commands (transport.tempo_map_add, remove, clear,
	// set_active) with no row at all - ReversibilityContractTest and the
	// group's own ControlTempoMapCommandsTest both reported it. Same class
	// (true_inverse, action checkpoint), same content, verbatim.
	R("transport.tempo_map_add", RC::TrueInverse, true,
		"the tempo map is a value type on the Song, not a JournallingObject, so "
		"no object checkpoint covers it",
		"action checkpoint: the recorded undo step writes the map captured before "
		"the edit back through TempoMapPublisher::edit",
		""),
	R("transport.tempo_map_remove", RC::TrueInverse, true,
		"the same value type, one event removed",
		"action checkpoint: the recorded undo step restores the map captured "
		"before the removal, event for event",
		""),
	R("transport.tempo_map_clear", RC::TrueInverse, true,
		"it removes every event AND switches the map off in one command, so a "
		"per-event inverse would not be one step",
		"action checkpoint: the recorded undo step restores the whole captured "
		"map, events and active flag together, as ONE Ctrl+Z",
		""),
	R("transport.tempo_map_set_active", RC::TrueInverse, true,
		"the flag is engine-read project state with no model of its own",
		"action checkpoint: the recorded undo step restores the map captured "
		"before the switch, so the flag comes back with the events",
		""),
	R("track.add", RC::TrueInverse, true,
		"a created Track has no before-state to restore, so the inverse is the "
		"operation, not a snapshot. DISAGREEMENT with A16-STATUS-MEASURED.md, "
		"which records this as reversible:false (no checkpoint exists)",
		"action checkpoint: the recorded undo step removes the created track "
		"through the product's own TrackContainerView::deleteTrackView path "
		"(the path whose checkpoint upstream left commented out); a fresh "
		"instrument track carries only defaults, so removing it restores the "
		"container exactly. The project-scoped next-id counter is monotonic and "
		"is NOT rewound: a re-add gets a fresh trk-<n>, which is the documented "
		"limit of this inverse",
		""),
	R("track.remove", RC::TrueInverse, true,
		"a deleted JournallingObject's journal id resolves to nullptr, so a "
		"checkpoint cannot bring it back. DISAGREEMENT with "
		"A16-STATUS-MEASURED.md, which records this as reversible:false",
		"action checkpoint: the track's own XML (Track::saveState into a "
		"JournalData DataFile) is captured before the delete and the recorded "
		"undo step recreates it with Track::create(element, song) - the same "
		"call TrackContainer::loadSettings makes. The capture is bounded like "
		"the device-state snapshot (64 KiB)",
		""),
	R("mixer.add_channel", RC::TrueInverse, true,
		"a created MixerChannel has no before-state; the mixer is a "
		"JournallingObject but restoring its checkpoint would destroy and "
		"recreate every channel, and a MixerView holds those pointers - the "
		"same GUI-safety reason the TrackContainer checkpoints are commented "
		"out upstream. DISAGREEMENT with A16-STATUS-MEASURED.md, which records "
		"this as reversible:false",
		"action checkpoint: the recorded undo step deletes the channel it "
		"created (Mixer::deleteChannel), through the same code path "
		"mixer.remove_channel uses; a fresh channel carries only defaults, so "
		"removing it restores the mixer exactly",
		""),
	R("plugin.state_load", RC::TrueInverse, true,
		"the load replaces the device's settings, and the device's PREVIOUS "
		"settings are captured in the transaction's before-state",
		"action checkpoint: the recorded undo step restores the captured "
		"state XML through the same controlRestoreDeviceState path "
		"plugin.state_load itself uses",
		""),
	R("plugin.preset_load", RC::TrueInverse, true,
		"same as plugin.state_load: the preset replaces the device's "
		"parameters and the previous settings are captured first",
		"action checkpoint: the recorded undo step restores the captured "
		"state XML",
		""),
	R("settings.set", RC::TrueInverse, true,
		"ConfigManager is not a JournallingObject, so there is no object "
		"checkpoint - but the previous value is a bounded scalar",
		"action checkpoint: the recorded undo step writes the previous value "
		"back and saves the config file, exactly as the command does",
		""),
	R("export.set_dither", RC::TrueInverse, true,
		"ExportRenderSettings is not a JournallingObject, so there is no object "
		"checkpoint - but the choice is a bounded scalar (on/off) and the "
		"previous value is captured before the write",
		"action checkpoint: the recorded undo step restores the previous dither "
		"choice through ExportRenderSettings::setDither, exactly as the command "
		"sets the new one. The step goes on the engine's own stack, so a user's "
		"Ctrl+Z and control.undo are one history",
		""),
	R("export.set_src_quality", RC::TrueInverse, true,
		"same shape as export.set_dither: a bounded enum owned by "
		"ExportRenderSettings rather than by a project object, with the previous "
		"converter selection captured before the write",
		"action checkpoint: the recorded undo step restores the previous "
		"SrcQuality through ExportRenderSettings::setSrcQuality, exactly as the "
		"command sets the new one",
		""),
	// --- Session sync (link.*, D11 "Ableton Link sync") -------------------
	// Four rows of the same shape as the two above: the session sync model
	// (include/LinkSync.h) is process state the engine does not journal, and
	// every one of these commands is a bounded scalar it owns - a flag, an
	// integer in a closed range, or a tempo. The previous value is captured
	// before the write and the recorded step puts it back through the model's
	// own setter.
	//
	// The one that is NOT a plain scalar is worth the sentence: a declared
	// session tempo is undone by DECLARING the previous one, which advances the
	// revision and is announced to the session - i.e. control.undo on
	// link.set_session_tempo sends the peers back to the tempo they had, rather
	// than only putting a number back in this process. That is the honest
	// inverse for a shared value, and it is why the row is true_inverse rather
	// than a claim that a session tempo is local state.
	R("link.set_enabled", RC::TrueInverse, true,
		"the sync model is process state (include/LinkSync.h): it is not a "
		"JournallingObject, so there is no object checkpoint - but joining or "
		"leaving the session is one bounded flag, and its previous value is "
		"captured before the write. Enabling also MEASURES whether this host can "
		"RECEIVE an announcement (a bounded loopback probe, "
		"docs/LINK-SYNC.md section 3) and reports the measurement in "
		"transport.loopback_probe; that is a report, not state, so it adds "
		"nothing to what this row's inverse has to restore",
		"action checkpoint: the recorded undo step calls LinkSyncEngine::setEnabled "
		"with the value in before, exactly as the command sets the new one",
		""),
	R("link.set_quantum", RC::TrueInverse, true,
		"same shape: the quantum is an integer in a closed range (1..64 beats) "
		"owned by the sync model, with the previous value captured before the "
		"write and the range checked before it, so a refused quantum records "
		"nothing",
		"action checkpoint: the recorded undo step calls LinkSyncEngine::setQuantum "
		"with the value in before",
		""),
	R("link.set_start_stop_sync", RC::TrueInverse, true,
		"same shape: one boolean the sync model owns and announces, with the "
		"previous value captured before the write",
		"action checkpoint: the recorded undo step calls "
		"LinkSyncEngine::setStartStopSync with the value in before",
		""),
	R("link.set_session_tempo", RC::TrueInverse, true,
		"the session tempo lives on the sync model's timeline (a double on the "
		"wire, applied to the Song's integer tempo model), so the previous value "
		"is captured before the declaration and the write itself is the inverse's "
		"mechanism",
		"action checkpoint: the recorded undo step DECLARES the previous tempo "
		"through LinkSyncEngine::setSessionTempo, which re-anchors the shared beat "
		"and advances the revision - so peers follow the undo exactly as they "
		"followed the declaration",
		""),
	R("audio.device_set", RC::TrueInverse, true,
		"the preference is a scalar in the config file, not in the project",
		"action checkpoint: the recorded undo step writes the previous device "
		"name back and saves the config file",
		""),
	R("project.restore_revision", RC::TrueInverse, true,
		"a revision restore is a file write; before it runs, the file it "
		"replaces is rotated into the same bounded revision set",
		"action checkpoint: the recorded undo step restores the revision the "
		"restore replaced",
		""),
#ifdef LMMS_HAVE_SESSION_VIEW
	// The session.* group: SessionModel is not a JournallingObject, so each edit
	// captures the whole <session> block and records ONE action checkpoint that
	// restores it (the true_inverse form clip.split and track.add use).
	R("session.set_grid", RC::TrueInverse, true,
		"grid dimensions live in the <session> block, which is not a JournallingObject", "action checkpoint: the captured <session> XML is restored whole through SessionModel::restoreState, so dimensions, slots and scenes come back together", ""),
	R("session.set_quantisation", RC::TrueInverse, true,
		"one field of the <session> block (launchquantisation)", "action checkpoint: the captured <session> block is restored whole", ""),
	R("session.set_scene", RC::TrueInverse, true,
		"a scene's name and its tempo / time-signature overrides live in the <session> block", "action checkpoint: the block is restored whole, so an override that was OFF before is OFF again rather than merely zeroed", ""),
	R("session.set_slot", RC::TrueInverse, true,
		"a clip slot's reference and launch settings are cells of the <session> block and have no journalled object behind them", "action checkpoint: the block is restored whole, so the slot's previous reference kind, launch mode and playback settings return together", ""),
	R("session.clear_slot", RC::TrueInverse, true,
		"clearing a cell destroys a reference id or an audio source path that no live object holds a copy of", "action checkpoint: the captured <session> block is the only place the cleared reference still exists, and it is restored", ""),
	R("session.clear", RC::TrueInverse, true,
		"it empties every cell, every scene override and the global quantisation at once, on a model the engine does not journal", "action checkpoint: ONE control.undo restores the whole session, so clearing a grid is one undoable step rather than one per cell", ""),
#endif // LMMS_HAVE_SESSION_VIEW

	// ---- racks (#599): the chains, the selector, the macros and the zones ----
	R("rack.add_chain", RC::TrueInverse, true,
		"a created chain has no before-state to restore; the rack is a member "
		"of the MixerChannel, not a JournallingObject, so no object checkpoint "
		"exists for it",
		"action checkpoint: the recorded undo step removes the chain the "
		"command created, through the same Rack::removeChain rack.remove_chain "
		"uses, so the rack is exactly as it was - a fresh chain carries no "
		"effects",
		""),
	R("rack.set_selected", RC::TrueInverse, true,
		"the selection is a scalar the rack owns and the rack does not journal, "
		"so the inverse is the operation rather than an object checkpoint",
		"action checkpoint: the recorded undo step calls Rack::setSelectedChain "
		"with the previous selection the transaction's before-state holds",
		""),
	R("rack.macro_add", RC::TrueInverse, true,
		"a created macro has no before-state; the macro list lives on the rack, "
		"which is not a JournallingObject",
		"action checkpoint: the recorded undo step removes the macro the "
		"command created (RackMacros::removeMacro). A new macro carries only "
		"its name and its value, so removing it restores the list exactly",
		""),
	R("rack.macro_remove", RC::TrueInverse, true,
		"a removed macro has no live object behind it, and the rack journals "
		"nothing",
		"action checkpoint: the recorded undo step re-inserts the captured "
		"macro - name, value and the whole target list - at its own index "
		"(RackMacros::insertMacro), so the list and the macro-<n> ids come back "
		"exactly",
		""),
	R("rack.macro_target_add", RC::TrueInverse, true,
		"a target is the macro's own data: no model is created or destroyed by "
		"binding one",
		"action checkpoint: the recorded undo step removes the target the "
		"command appended, at the same index",
		""),
	R("rack.macro_target_remove", RC::TrueInverse, true,
		"same list; a removed target has no live object behind it",
		"action checkpoint: the recorded undo step re-inserts the captured "
		"target at its own index, so the bind order - which is the order the "
		"targets are applied in - comes back exactly",
		""),
	RC("rack.macro_set", RC::TrueInverse, true,
		"the call writes MORE THAN ONE object: the macro's own scalar, which "
		"lives on the rack and is not journalled, plus every parameter model it "
		"drives. A model checkpoint alone would leave the macro's value behind, "
		"and the macro alone is not a JournallingObject",
		"action checkpoint: ONE recorded undo step puts the macro's value back "
		"and writes every parameter the call changed back to the value it had. "
		"Each target is re-resolved by chain/effect/parameter name when the "
		"step runs - never a raw device pointer - and a target whose device is "
		"gone is skipped rather than dereferenced",
		"",
		"channel,macro"),
	R("rack.zone_add", RC::TrueInverse, true,
		"a created zone has no before-state; the zone list is the rack's own "
		"data",
		"action checkpoint: the recorded undo step drops the zone the command "
		"appended, the same shape rack.add_chain uses, so a later edit to "
		"another zone cannot make the undo eat a zone nobody asked about",
		""),
	R("rack.zone_remove", RC::TrueInverse, true,
		"a removed zone has no live object behind it",
		"action checkpoint: the recorded undo step re-inserts the captured zone "
		"at its own index, so the list - and the order the first-match rule "
		"reads - comes back exactly",
		""),

	// =====================================================================
	// The modulation layer (#602, docs/MODULATION.md). A modulator is not a
	// JournallingObject: it is a plain value on the Song's
	// ModulationLayerPublisher, and a Song checkpoint captures the TRACK
	// CONTAINER, which does not hold it - the same finding the tempo map
	// records. Every layer edit is therefore a recorded ACTION, and every
	// action restores the parameters' own values before it rebuilds the
	// layer's write set (include/ModulationLayer.h,
	// rebuildModulationRuntimeRestoring).
	// =====================================================================
	R("modulator.create", RC::TrueInverse, true,
		"a created modulator has no before-state; the layer is the publisher's "
		"own value and no object checkpoint carries it",
		"action checkpoint: the recorded undo step removes the modulator the "
		"command created, through the same ModulationLayer::removeModulator "
		"modulator.remove uses; a new modulator carries a name, a source and no "
		"routes",
		""),
	R("modulator.remove", RC::TrueInverse, true,
		"a removed modulator and its route list have no live object behind them",
		"action checkpoint: the recorded undo step re-inserts the captured "
		"modulator - name, source and every route - at its own index, so the "
		"layer and the modulator-<n> ids come back exactly. The removal also "
		"hands every target back to its captured base before the runtime is "
		"rebuilt, so a removed modulator leaves no parameter parked",
		""),
	R("modulator.rate_set", RC::TrueInverse, true,
		"an LFO's shape, rate, phase and polarity are the layer's own data; no "
		"object checkpoint carries them",
		"action checkpoint: the recorded undo step writes the previous source "
		"back through ModulationLayer::setSource and re-resolves the "
		"modulator's targets from the values they had before it drove them. "
		"Switching 'active' off is the same step, so deactivating a modulator "
		"hands its parameters back",
		""),
	R("modulator.target_set", RC::TrueInverse, true,
		"a created route has no before-state, and the parameter it binds is not "
		"changed by the binding itself - the base value the route modulates "
		"around is captured from the model as it stands",
		"action checkpoint: the recorded undo step removes the route this "
		"command appended (ModulationLayer::removeRoute at the same index) and "
		"hands the parameter back to the value it had before it was bound",
		""),
	R("modulator.depth_set", RC::TrueInverse, true,
		"a depth is a scalar in the layer, not a model value: nothing the "
		"journal could checkpoint changes, and the route's captured base is "
		"deliberately NOT re-read (a depth change is not a move of the "
		"parameter)",
		"action checkpoint: the recorded undo step writes the previous depth "
		"back through ModulationLayer::setDepth. The transaction's before-state "
		"holds the previous depth",
		""),
	R("modulator.target_remove", RC::TrueInverse, true,
		"a removed route has no live object behind it",
		"action checkpoint: the recorded undo step re-inserts the captured "
		"route at its own index, so the bind order - which is the order the "
		"routes are applied in - comes back exactly, and the target's base is "
		"re-captured from the value it was handed back to",
		""),

	// The groove pool's own edits (docs/GROOVE-POOL.md). The pool is project
	// state on the Song but NOT in the track container, so a Song journal
	// checkpoint - which carries the container - does not hold it: the same
	// finding the tempo-map and modulation-layer rows above record. Each step
	// therefore writes the captured <groove-pool> element back, and the
	// recorded inverse names a REAL command a reader can re-issue.
	R("groove.extract", RC::TrueInverse, true,
		"the capture writes a NAMED template into the project's pool, which is "
		"project state the Song checkpoint does not carry",
		"action checkpoint: the recorded step writes the captured <groove-pool> "
		"element back. The descriptor names the real inverse - groove.set with "
		"the replaced groove's own steps when the name existed, groove.remove "
		"when the capture created it",
		""),
	R("groove.set", RC::TrueInverse, true,
		"writes one template (geometry and every step) into the pool, replacing "
		"a groove of the same name",
		"action checkpoint: the recorded step writes the captured pool back, so "
		"the replaced groove returns with its own steps and its own position",
		""),
	R("groove.remove", RC::TrueInverse, true,
		"a deleted template has no live object behind it; the pool element is "
		"the only copy of it",
		"action checkpoint: the recorded step writes the captured pool back, so "
		"the removed groove returns at its own position in the pool",
		""),
	R("groove.rename", RC::TrueInverse, true,
		"the name is the key, so a rename rewrites a template's identity - the "
		"pool element is the only place that identity lives",
		"action checkpoint: the recorded step writes the captured pool back, and "
		"the descriptor re-issues groove.rename with the names swapped",
		""),
	// =====================================================================
	// 0.3.0 `clock.*` - MIDI clock, the DAW as a clock master and slave.
	// =====================================================================
	R("clock.master_set", RC::TrueInverse, true,
		"the clock's mode is engine state on a plain object, not project state, "
		"and not a JournallingObject - so no object checkpoint covers it, and "
		"recording the whole project for one flag would be a much larger act "
		"than the one the command performs",
		"action checkpoint: the recorded undo step calls MidiClock::restoreMaster "
		"with the enabled flag and the port name the transaction's before-state "
		"holds, and unsubscribes the destination port this call subscribed when "
		"the before-state named none - so control.undo puts the subscription back "
		"and not only the flag",
		""),


	// The chain-preset store (OWNER-31 item 2). These four are true_inverse for
	// ONE shared reason, and it is not the project: the store is a file tree
	// OUTSIDE the project (the user preset tree's chainpresets/, so a preset is
	// usable across projects), which no Song checkpoint carries and no live
	// object restores. Each command therefore records an ACTION checkpoint that
	// undoes its own file operation - the groove pool's four edits' mechanism.
	// The two read-only ids are in ControlReversibilityTablePassive.cpp.
	R("chain.save", RC::TrueInverse, true,
		"captures the target's chain - the ordered device list plus each "
		"device's own state document - and writes ONE file in the preset store. "
		"The file is outside the project, so the project's own journal does not "
		"carry it",
		"action checkpoint: the recorded step removes the preset file this "
		"command created, or writes the revision it replaced (before.previous_"
		"sha256) back to the preset's own path; the redo half re-writes the "
		"captured document",
		""),
	R("chain.apply", RC::TrueInverse, true,
		"REPLACES the target's effect chain (devices and their settings) with "
		"the preset's. The device list is not a live object a checkpoint "
		"restores, and the devices' state is not journalled",
		"action checkpoint: the chain's own <fxchain> XML is captured before the "
		"write (EffectChain::saveSettings) and the recorded step writes it back "
		"through EffectChain::loadSettings, the project loader's own path. A chain "
		"too large to capture within the bounded snapshot is REFUSED rather than "
		"replaced without an inverse",
		""),
	R("chain.rename", RC::TrueInverse, true,
		"renames one preset file in the store. The name is the key - the file name "
		"IS the store's index - and the store is outside the project",
		"action checkpoint: the recorded step renames the file back to "
		"before.path, the same operation chain.rename performs, and the redo half "
		"renames it forward again",
		""),
	R("chain.remove", RC::TrueInverse, true,
		"deletes one preset file from the store, outside the project",
		"action checkpoint: the preset's bytes are captured before the removal and "
		"the recorded step writes them back to the same path, byte for byte, so the "
		"preset returns exactly as it was",
		""),
};

constexpr int kActionRowCount = static_cast<int>(sizeof(kActionRows) / sizeof(kActionRows[0]));

} // namespace

const ReversibilityRow* reversibilityActionRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kActionRowCount; }
	return kActionRows;
}

} // namespace control
} // namespace lmms
