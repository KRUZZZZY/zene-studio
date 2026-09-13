/*
 * ControlReversibilityTable.cpp - THE SPEC A16 classification table: one row
 *                                  for every command the control surface
 *                                  registers, and the reason for its class.
 *
 * This file is data. It is deliberately separate from
 * ControlReversibility.cpp (the machinery) so that the whole contract can be
 * read and reviewed as a table in one place, and so the anti-drift test has
 * exactly one thing to compare against the registry.
 *
 * It holds the FIRST of the table's three literal blocks - the true_inverse
 * rows, whose inverse is a LIVE checkpoint on the engine's own undo stack.
 * ControlReversibilityTableSnapshot.cpp holds the second (snapshot: a bounded
 * recorded state, replayed by an inverse command or named as the manual
 * fallback) and ControlReversibilityTablePassive.cpp the third (irreversible
 * and not_mutating: no inverse, or nothing written). ReversibilityTable's
 * constructor reads all three: the file split exists because this fork's
 * file-length ratchet measures a file as a unit, not because the contract is
 * three contracts.
 *
 * The blocks are split by WHAT THE INVERSE IS, not by command group, so each
 * file answers one question: "what does the engine actually put back?"
 *
 * A row that ends in a coalescing target (the RC() macro) declares that a run
 * of that command on ONE named thing is a single undo step - which is what
 * makes a 200-step drag one Ctrl+Z. See docs/UNDO-BOUNDS.md.
 *
 * Reconciled against ableton-gap/A16-STATUS-MEASURED.md (the parent's measured
 * baseline of 36 exercised mutating commands, 17 reversible:true). The rows
 * where this table DISAGREES with that measurement are marked "DISAGREEMENT"
 * and the doc that ships beside this code (docs/A16-REVERSIBILITY.md) records
 * the argument for each.
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

// lmmsconfig.h carries the ZENE_TELEMETRY_ENABLED packager-kill-switch define.
// ControlReversibility.h does not pull it in, and this file's rows are guarded
// by that switch, so it has to be included explicitly - without it the #ifdef
// below reads "off" even in a telemetry-enabled build and silently drops the
// two telemetry.* rows the registry does declare.
#include "lmmsconfig.h"

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
/*! A literal row for a command that COALESCES. RC(id, class, reversible,
 * reason, mechanism, fallback, target) is R plus \a target: the name(s) of
 * the argument(s) whose VALUES identify the thing a gesture is being made
 * on - comma-separated when two arguments together name it. Two consecutive
 * calls of the same command with the same target values are ONE undo step,
 * which is what makes a 200-step drag one Ctrl+Z instead of 200. This is
 * DECLARATION ONLY: the rule, the window and the bound it lives in are in
 * docs/UNDO-BOUNDS.md and include/ProjectJournal.h.
 */
#define RC(id, cls, rev, reason, mechanism, fallback, target) \
	{ id, cls, reason, mechanism, fallback, rev, target }

/*! The contract table. Order: the three classes of SPEC A16 first (true
 * inverse, snapshot, irreversible), then the commands that write nothing. */
const ReversibilityRow kRows[] = {

	// =====================================================================
	// true_inverse - a live checkpoint on the engine's own ProjectJournal
	// restores it. Either a plain object checkpoint, a COMPOSITE checkpoint
	// (several objects, one undo step) or an ACTION checkpoint (a recorded
	// operation, for a created or deleted object that has no live state to
	// restore). undo() pops all of these through one ProjectJournal.
	// =====================================================================
	R("transport.set_tempo", RC::TrueInverse, true,
		"one scalar on the Song, which is a JournallingObject",
		"ProjectJournal (Song checkpoint): Song::saveState carries the tempo",
		""),
	R("transport.seek", RC::TrueInverse, true,
		"the play head is engine state, not project state, and is not a "
		"JournallingObject - so no object checkpoint exists for it",
		"action checkpoint: the recorded undo step calls Song::setPlayPos with "
		"the tick the transaction's before-state holds",
		""),
	R("track.rename", RC::TrueInverse, true,
		"the name is part of the Track's own serialized state",
		"ProjectJournal (Track checkpoint)",
		""),
	R("track.set_mute", RC::TrueInverse, true,
		"the muted flag is a BoolModel, i.e. a JournallingObject of its own",
		"ProjectJournal (Track mute BoolModel checkpoint)",
		""),
	R("track.set_solo", RC::TrueInverse, true,
		"the action is NOT one object: the solo flag rides the solo BoolModel "
		"and Track::toggleSolo (driven from the solo model's dataChanged) "
		"writes EVERY other track's mute. DISAGREEMENT with "
		"A16-STATUS-MEASURED.md, which records this as reversible:false "
		"(\"partial: one checkpoint covers one object\")",
		"COMPOSITE checkpoint: every track of the song container is snapshotted "
		"as ONE checkpoint (ProjectJournal::addJournalCheckPoint(QVector)), so "
		"one control.undo and one Ctrl+Z restore the solo flag and every mute "
		"together. This is SPEC A16 deliverable 3",
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
	R("clip.add", RC::TrueInverse, true,
		"the clip is created inside a Track, and a Track checkpoint carries "
		"every clip it holds",
		"ProjectJournal (Track checkpoint)",
		""),
	R("clip.delete", RC::TrueInverse, true,
		"same container: the clip list is part of the Track's serialized state",
		"ProjectJournal (Track checkpoint)",
		""),
	R("clip.duplicate", RC::TrueInverse, true,
		"the duplicate is a second clip in the same Track",
		"ProjectJournal (Track checkpoint)",
		""),
	RC("clip.move", RC::TrueInverse, true,
		"position is a Clip property and the Clip is a JournallingObject",
		"ProjectJournal (Clip checkpoint)",
		"",
		"clip"),
	RC("clip.resize", RC::TrueInverse, true,
		"length is a Clip property",
		"ProjectJournal (Clip checkpoint)",
		"",
		"clip"),
	R("clip.split", RC::TrueInverse, true,
		"the split rewrites the clip list of one Track",
		"ProjectJournal (Track checkpoint)",
		""),
	R("clip.set_fade", RC::TrueInverse, true,
		"the two fade ramps are part of the Clip's own serialized state "
		"(Clip::saveClipEdits writes them onto the clip's element), so the Clip "
		"checkpoint the command takes is a live inverse of both",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("clip.set_gain", RC::TrueInverse, true,
		"the clip gain is part of the same Clip serialized state as the fades",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("clip.crossfade", RC::TrueInverse, true,
		"the command writes TWO clips, and it refuses unless both are on the same "
		"track - which is what lets one Track checkpoint cover the pair. Its "
		"recorded inverse op is named as the manual one (no single command "
		"restores two clips' fades) and control.undo's default 'journal' "
		"mechanism unwinds the checkpoint instead, exactly as clip.split does",
		"ProjectJournal (Track checkpoint)",
		""),
	R("note.add", RC::TrueInverse, true,
		"the note list belongs to the MidiClip, which is a JournallingObject",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("note.remove", RC::TrueInverse, true,
		"same clip-owned list",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("note.move", RC::TrueInverse, true,
		"note position is part of the clip's saved note list",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("note.resize", RC::TrueInverse, true,
		"note length is part of the clip's saved note list",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("note.velocity_set", RC::TrueInverse, true,
		"note volume is part of the clip's saved note list",
		"ProjectJournal (Clip checkpoint)",
		""),
	RC("mixer.set_volume", RC::TrueInverse, true,
		"the fader is a FloatModel, i.e. a JournallingObject",
		"ProjectJournal (MixerChannel volume model checkpoint)",
		"",
		"channel"),
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
	R("automation.add_point", RC::TrueInverse, true,
		"the point lives on an AutomationClip, which is a JournallingObject - "
		"EXCEPT on the call that has to create the AutomationClip first",
		"ProjectJournal (AutomationClip checkpoint); on the creating call, an "
		"action checkpoint that removes the automation track the command "
		"created, so the first point is one undoable step like every later one. "
		"DISAGREEMENT with A16-STATUS-MEASURED.md, which records the creating "
		"call as reversible:false",
		""),
	R("automation.remove_point", RC::TrueInverse, true,
		"the point list is the clip's own state",
		"ProjectJournal (AutomationClip checkpoint)",
		""),
	R("automation.clear", RC::TrueInverse, true,
		"the point list is the clip's own state",
		"ProjectJournal (AutomationClip checkpoint)",
		""),
	R("warp.add", RC::TrueInverse, true,
		"the marker set is the SampleClip's own serialized state - the additive "
		"<warp> child element #597 writes",
		"ProjectJournal (Clip checkpoint): SampleClip::saveSettings writes the "
		"<warp> element and SampleClip::loadSettings re-reads it, so the "
		"checkpoint taken before the first marker was added restores an "
		"unwarped clip",
		""),
	R("warp.move", RC::TrueInverse, true,
		"a marker's timeline offset is part of the same serialized map",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("warp.remove", RC::TrueInverse, true,
		"removing a marker rewrites the same <warp> element, and removing the "
		"last one leaves an empty map the checkpoint restores exactly",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("warp.set", RC::TrueInverse, true,
		"one call may write the marker list, the tempo mode and the source "
		"tempo, and all three are fields of the same serialized <warp> element",
		"ProjectJournal (Clip checkpoint): the three fields travel as ONE "
		"checkpoint, so one control.undo restores the whole warp map",
		""),
	R("plugin.bypass", RC::TrueInverse, true,
		"the On/Off control is the Effect's enabled model",
		"ProjectJournal (Effect enabled-model checkpoint)",
		""),
	RC("plugin.param_set", RC::TrueInverse, true,
		"the parameter is an AutomatableModel, i.e. a JournallingObject",
		"ProjectJournal (parameter model checkpoint)",
		"",
		"target,plugin,name,index"),
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

	// ---------- read-only inspectors ----------
	R("app.version", RC::NotMutating, false, "reads the build identity", "no write", ""),
	R("arrangement.get_state", RC::NotMutating, false, "reads the model", "no write", ""),
	R("audio.device_list", RC::NotMutating, false, "reads the device table", "no write", ""),
	R("automation.get_state", RC::NotMutating, false, "reads the model", "no write", ""),
	R("control.commands_list", RC::NotMutating, false, "reads the registry", "no write", ""),
	R("control.ping", RC::NotMutating, false, "liveness probe", "no write", ""),
	R("control.surface_report", RC::NotMutating, false, "reads the menu/toolbar reflection", "no write", ""),
	R("control.transactions", RC::NotMutating, false, "reads the transaction record", "no write", ""),
	R("control.version", RC::NotMutating, false, "reads the version strings", "no write", ""),
	R("dsp.get_state", RC::NotMutating, false, "reads the device chains", "no write", ""),
	R("midi.device_list", RC::NotMutating, false, "reads the MIDI client", "no write", ""),
	R("mixer.get_state", RC::NotMutating, false, "reads the mixer", "no write", ""),
	R("plugin.list", RC::NotMutating, false, "reads the device catalogue", "no write", ""),
	R("plugin.param_get", RC::NotMutating, false, "reads a parameter", "no write", ""),
	R("plugin.preset_list", RC::NotMutating, false, "reads a preset directory", "no write", ""),
	R("project.get_state", RC::NotMutating, false, "reads the project state", "no write", ""),
	R("roll.get_state", RC::NotMutating, false, "reads the note list", "no write", ""),
	R("script.list", RC::NotMutating, false, "reads the scripts directory", "no write", ""),
	R("settings.get", RC::NotMutating, false, "reads one config value", "no write", ""),
#ifdef ZENE_TELEMETRY_ENABLED
	R("telemetry.status", RC::NotMutating, false,
		"reads the consent record and the payload builder", "no write", ""),
#endif // ZENE_TELEMETRY_ENABLED
	R("track.get_state", RC::NotMutating, false, "reads one track", "no write", ""),
	R("track.list", RC::NotMutating, false, "reads the track container", "no write", ""),
	R("transport.get_state", RC::NotMutating, false, "reads the transport", "no write", ""),
	R("transport.tempo_map_get", RC::NotMutating, false,
		"reads the tempo map and what its queries answer at the play head", "no write", ""),
	R("warp.list", RC::NotMutating, false, "reads a clip's warp map", "no write", ""),
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


	// Rows restored from ControlReversibilityTableTrueInverse.cpp, which this
	// structure retires: 030/w14-comping branched before that split and appended
	// its comp.* rows to the old file. Same class (true_inverse), same content.
	R("comp.lane_add", RC::TrueInverse, true,
		"the lane list is part of the Track's serialized state (the <takelanes> "
		"element), so the checkpoint taken before the add restores a track "
		"whose lane list does not carry the new lane",
		"ProjectJournal (Track checkpoint). The recorded inverse op is a REAL "
		"command this surface implements: comp.lane_remove, with the lane "
		"comp.lane_add just handed out",
		""),
	R("comp.lane_remove", RC::TrueInverse, true,
		"the removal rewrites the lane list AND re-points every composite "
		"segment that named the lane to the track's base lane, and both lists "
		"are the Track's own serialized state",
		"ProjectJournal (Track checkpoint): one undo restores the lane, its "
		"name and every segment that had been re-pointed",
		""),
	R("comp.assign", RC::TrueInverse, true,
		"the lane tag is a field of the CLIP's own serialized state - "
		"Clip::saveClipEdits writes the `lane` attribute onto the clip's "
		"element and Clip::loadClipEdits re-reads it, resetting to 0 when the "
		"attribute is absent - so the Clip checkpoint the command takes is a "
		"live inverse of the assignment",
		"ProjectJournal (Clip checkpoint). The recorded inverse op is a REAL "
		"command: comp.assign with the clip's previous lane, which the "
		"transaction's before-state names",
		""),
	R("comp.select", RC::TrueInverse, true,
		"a selection splits and rewrites the composite, which is part of the "
		"Track's serialized state; the transaction's before-state holds the "
		"whole previous segment list, because no single command re-creates it "
		"(there is no un-select: a composite is total over its span)",
		"ProjectJournal (Track checkpoint): one control.undo restores the "
		"segment list the selection painted over, byte for byte through the "
		"same <takelanes> element the project file carries",
		""),
	R("comp.rebuild", RC::TrueInverse, true,
		"it can WRITE: given a span it clamps the composite to it and fills "
		"every gap with the base lane, and it re-sorts and merges neighbouring "
		"segments that are the same lane at a continuous offset. Both the "
		"composite and the lane list it reads are the Track's serialized state",
		"ProjectJournal (Track checkpoint): the segment list before the rebuild "
		"is in the transaction's before-state and the checkpoint restores it. "
		"On an already well-formed composite the command is idempotent and the "
		"restored state is the same state, which the test asserts rather than "
		"assumes",
		""),
};

constexpr int kRowCount = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));

} // namespace

const ReversibilityRow* reversibilityRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kRowCount; }
	return kRows;
}

} // namespace control
} // namespace lmms
