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
 * ReversibilityTable's constructor reads all six files through
 * reversibilityRowTable(), which JOINS this file's rows with the action block's
 * and the four group files': the block is still ONE table with ONE row count,
 * and the anti-drift test still compares the registry against every row. The
 * files are separate because the file-length ratchet measures a file as a unit,
 * not because the contract is five contracts.
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
	R("midi.retro_capture_to_clip", RC::TrueInverse, true,
		"the clip it materialises the captured window into is a NEW clip inside a "
		"Track, and a Track checkpoint carries every clip it holds - the same "
		"container argument as clip.add. One checkpoint covers the whole capture, "
		"so ONE control.undo removes the clip and every note in it together",
		"ProjectJournal (Track checkpoint): the checkpoint is taken before "
		"Track::createClip() and the notes go through MidiClip::addNote() inside it, "
		"so Track::restoreState re-loads the track without the new clip. The "
		"transaction records clip.delete as its inverse operation",
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
	R("midi.retro_capture_status", RC::NotMutating, false, "reads the capture ring and the MIDI client", "no write", ""),
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

	// The four transport.tempo_map_* rows are NOT repeated here. Their mechanism
	// is "action checkpoint", which is the action half's own definition, and
	// ControlReversibilityTableAction.cpp carries all four: reversibilityRowTable()
	// appends that file's rows to this one and insertRows() lets the LAST row for an
	// id win, so the assembled table has always read action's copy and the copy that
	// used to sit here was dead data. The duplicate was removed 2026-09-13 (lane
	// 030/retro-capture) when this file crossed the 500-line cap - the ids, their
	// class and their mechanism in the assembled table are unchanged, which is what
	// ReversibilityContractTest's histogram (167 rows) re-checks.

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
	// ---- freeze / bounce-in-place ----
	// All three freeze verbs write ONE thing: the take, as four ATTRIBUTES on
	// the track's OWN element - frozenAudio / frozenStart / frozenEnd /
	// frozenMuted, never a <frozen> child (Track::loadTrack turns an
	// unrecognised child of <track> into a real Clip, and a metadata-marked
	// child does not survive a save at all - both measured in Track::saveTrack's
	// own comment) - plus, for a region, the muted flags of the clips it covers.
	// Both live on the Track, so the Track's own journal checkpoint is a real
	// inverse, and Track::loadTrack's reset-on-absence is what makes clearing the
	// take restore-able: the recorded XML carries no frozenAudio attribute, so a
	// restore removes the take rather than leaving it in place.
	R("freeze.track", RC::TrueInverse, true,
		"the freeze adds the track's frozenAudio/frozenStart/frozenEnd attributes "
		"and makes its play() return the take instead of the clips; the source's "
		"own clips, positions and mute flags are NOT touched (a whole-track freeze "
		"mutes nothing), so the whole change is those attributes",
		"ProjectJournal (Track checkpoint: Track::restoreState re-loads the "
		"track's own XML, and Track::loadTrack clears the take when the element "
		"carries no frozenAudio attribute - the reset-on-absence rule a checkpoint "
		"restore depends on)",
		"the rendered WAV stays on disk - undoing a freeze is not a delete; "
		"remove the file named in before/track.get_state if it is not wanted"),
	R("freeze.region", RC::TrueInverse, true,
		"the freeze adds the track's frozenAudio attributes AND mutes the clips "
		"that start inside the region, which is a change to the track's serialized "
		"clip state",
		"ProjectJournal (Track checkpoint: the same single checkpoint captures "
		"the clips' mute flags and the absence of the frozenAudio attributes, so "
		"one step puts both back)",
		"the rendered WAV stays on disk, as for freeze.track"),
	R("freeze.unfreeze", RC::TrueInverse, true,
		"dropping the take unmutes the clips the freeze muted and removes the "
		"frozenAudio attributes, both of which are changes to the track's "
		"serialized state",
		"ProjectJournal (Track checkpoint: Track::loadTrack clears the take on "
		"absence and re-loads the clips' muted flags from the XML, so one undo "
		"brings the exact take back - re-running freeze.track would render a new "
		"one)",
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
	// ---- punch in/out (0.3.0) ----
	// Both verbs write ONE thing: the punch region on the timeline's own
	// <timeline> element (punch0pos / punch1pos / punchstate). Timeline is a
	// JournallingObject with its own jo_id, so its checkpoint is a live inverse -
	// and the attributes are written only for a non-default region, which is why
	// Timeline::loadSettings CLEARS them on absence: without that reset a
	// checkpoint taken before the first punch call could not take the region
	// back off.
	R("transport.punch_set", RC::TrueInverse, true,
		"the punch region and its arm flag are part of the timeline's own "
		"serialized state, and the timeline is a JournallingObject",
		"ProjectJournal (Timeline checkpoint: Timeline::saveState writes the "
		"region onto the <timeline> element, and Timeline::loadSettings clears it "
		"when the element carries no punch attribute - the reset-on-absence rule a "
		"checkpoint restore of the pre-first-punch state depends on)",
		""),
	R("transport.punch_clear", RC::TrueInverse, true,
		"clearing the region writes the same three attributes back to their "
		"default, which is serialized as their absence",
		"ProjectJournal (Timeline checkpoint: the same checkpoint and the same "
		"reset-on-absence rule; the recorded inverse is transport.punch_set with "
		"the region that was cleared)",
		""),
};

constexpr int kRowCount = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));

} // namespace

/*! The true_inverse block, in its FIVE files JOINED: this file's
 *  live-checkpoint rows first, then the recorded-ACTION rows
 *  (ControlReversibilityTableAction.cpp), then the three GROUP files
 *  (ControlReversibilityTable{TrackFolder,Vca,Routing,ScanAndCrash}.cpp). The block is split
 *  across five translation units (see the header), but every caller -
 *  ReversibilityTable's constructor, and through it control.transactions and
 *  tests/…/ReversibilityContractTest - still reads ONE block with ONE row count.
 *  The join is built once, on the first call; the rows are static data.
 */
const ReversibilityRow* reversibilityRowTable(int* rowCount)
{
	static const std::vector<ReversibilityRow> joined = [] {
		std::vector<ReversibilityRow> all(kRows, kRows + kRowCount);
		// The group files, in the order ReversibilityTable's map wants them: a
		// repeated id is OVERWRITTEN, so the order is part of the contract.
		for (const ReversibilityRow* (*rowsFor)(int*) : {reversibilityActionRowTable,
				reversibilityTrackFolderRowTable, reversibilityVcaRowTable, reversibilityRoutingRowTable,
				reversibilityVerbRowTable, reversibilityScanAndCrashRowTable, reversibilityMasteringRowTable,
				reversibilityInterchangeRowTable})
		{
			int count = 0;
			const ReversibilityRow* rows = rowsFor(&count);
			all.insert(all.end(), rows, rows + count);
		}
		return all;
	}();
	if (rowCount != nullptr) { *rowCount = static_cast<int>(joined.size()); }
	return joined.data();
}

} // namespace control
} // namespace lmms
