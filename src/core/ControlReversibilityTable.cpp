/*
 * ControlReversibilityTable.cpp - THE SPEC A16 classification table's LIVE rows:
 *                                  one row per command whose inverse is the
 *                                  engine's own live checkpoint, and the reason
 *                                  for its class.
 *
 * This file is data. It is deliberately separate from
 * ControlReversibility.cpp (the machinery) so that the whole contract can be
 * read and reviewed as a table in one place, and so the anti-drift test has
 * exactly one thing to compare against the registry.
 *
 * It holds the true_inverse rows whose inverse is a LIVE checkpoint - a plain
 * object checkpoint, a COMPOSITE one (several objects restored as ONE step), or
 * a parameter/model checkpoint - and the not_mutating read-only inspector rows
 * that have lived in this file since before the split (24 of those 25 rows are
 * also stated in ControlReversibilityTablePassive.cpp; they are carried here
 * rather than dropped so that reversibilityRowTable()'s row count, and the
 * table's, do not move). The true_inverse rows whose inverse is a RECORDED
 * ACTION - a created or deleted object, a scalar outside the project, a
 * captured XML block the transaction replays - are in
 * ControlReversibilityTableAction.cpp, the snapshot rows in
 * ControlReversibilityTableSnapshot.cpp and the irreversible / not_mutating
 * rows in ControlReversibilityTablePassive.cpp.
 *
 * ReversibilityTable's constructor reads all four files through
 * reversibilityRowTable(), which JOINS this file's rows with the action block's
 * (see the function at the end): the true_inverse block is still ONE table with
 * ONE row count, and the anti-drift test still compares the registry against
 * every row. The files are separate because this fork's file-length ratchet
 * measures a file as a unit, not because the contract is four contracts.
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

#include <vector>

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
	// restores it: a plain object checkpoint, a COMPOSITE checkpoint
	// (several objects, one undo step) or a parameter/model checkpoint.
	// undo() pops all of these through one ProjectJournal. The true_inverse
	// rows whose inverse is a RECORDED ACTION are in
	// ControlReversibilityTableAction.cpp; reversibilityRowTable() joins the
	// two halves, so this is still ONE block with ONE row count.
	// =====================================================================

	R("transport.set_tempo", RC::TrueInverse, true,
		"one scalar on the Song, which is a JournallingObject",
		"ProjectJournal (Song checkpoint): Song::saveState carries the tempo",
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

	// The tempo map's true_inverse rows (030/w15-tempo-map): restored after a
	// union took the other side wholesale and dropped them.
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

	// The per-note expression pair (#602's per-note half, driving #601's own
	// fields). A Note is a SerializingObject, not a JournallingObject, so there
	// is no note-level checkpoint: the clip's is the live object that carries
	// the note list, and it is the same mechanism note.velocity_set reverses
	// with.
	R("note.expression_set", RC::TrueInverse, true,
		"the expression lives in the Note's own serialized state, and the "
		"MidiClip that owns the note list is a JournallingObject",
		"ProjectJournal (MidiClip checkpoint: MidiClip::loadSettings clears and "
		"re-loads the clip's note list, which is the mechanism the piano roll's "
		"own note edits reverse with)",
		""),
	R("note.expression_clear", RC::TrueInverse, true,
		"dropping the expression also drops the note's optional mpe* "
		"attributes, which is a change to the clip's serialized state",
		"ProjectJournal (MidiClip checkpoint: the recorded checkpoint restores "
		"the note list, attributes included - a cleared expression comes back "
		"with its presence flag and all three axes)",
		""),

	// The two CLIP-editing verbs of the groove group (docs/GROOVE-POOL.md).
	// The groove pool's OWN edits are action-checkpoint rows and live in
	// ControlReversibilityTableAction.cpp: the pool is project state the Song
	// checkpoint does not carry (it is not in the track container). What these
	// two do is move NOTES, and a MidiClip's serialized state IS its note
	// list, so the clip's own checkpoint is the inverse - the mechanism
	// note.move and note.velocity_set already reverse with.
	R("groove.apply", RC::TrueInverse, true,
		"the groove moves notes: each note's position and velocity is part of "
		"the MidiClip's serialized note list, and the clip is a "
		"JournallingObject",
		"ProjectJournal (MidiClip checkpoint: one undo restores every position "
		"and velocity the apply moved, whatever the strength was)",
		""),
	R("groove.quantize", RC::TrueInverse, true,
		"same object and same reasoning as groove.apply - the grid, the "
		"strength and the humanise jitter all land in the note list, and the "
		"jitter is a pure function of the seed rather than hidden state, so the "
		"pre-command state is exactly what the checkpoint captured",
		"ProjectJournal (MidiClip checkpoint)",
		""),
};

constexpr int kRowCount = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));

} // namespace

/*! The true_inverse block, in its TWO files JOINED: this file's
 *  live-checkpoint rows first, then ControlReversibilityTableAction.cpp's
 *  action rows. The block is split across two translation units (see the
 *  header), but every caller - ReversibilityTable's constructor, and through it
 *  control.transactions and tests/…/ReversibilityContractTest - still reads ONE
 *  block with ONE row count. The join is built once, on the first call; the
 *  rows themselves are static data.
 */
const ReversibilityRow* reversibilityRowTable(int* rowCount)
{
	static const std::vector<ReversibilityRow> joined = [] {
		int actionCount = 0;
		const ReversibilityRow* actionRows = reversibilityActionRowTable(&actionCount);
		std::vector<ReversibilityRow> both(kRows, kRows + kRowCount);
		both.insert(both.end(), actionRows, actionRows + actionCount);
		return both;
	}();
	if (rowCount != nullptr) { *rowCount = static_cast<int>(joined.size()); }
	return joined.data();
}

} // namespace control
} // namespace lmms
