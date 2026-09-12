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

namespace lmms
{
namespace control
{

namespace
{

using RC = ReversibilityClass;

//! A literal row: R(id, class, reversible, reason, mechanism, fallback).
#define R(id, cls, rev, reason, mechanism, fallback) \
	{ id, cls, reason, mechanism, fallback, rev }

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
	R("clip.move", RC::TrueInverse, true,
		"position is a Clip property and the Clip is a JournallingObject",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("clip.resize", RC::TrueInverse, true,
		"length is a Clip property",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("clip.split", RC::TrueInverse, true,
		"the split rewrites the clip list of one Track",
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
	R("mixer.set_volume", RC::TrueInverse, true,
		"the fader is a FloatModel, i.e. a JournallingObject",
		"ProjectJournal (MixerChannel volume model checkpoint)",
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
	R("plugin.bypass", RC::TrueInverse, true,
		"the On/Off control is the Effect's enabled model",
		"ProjectJournal (Effect enabled-model checkpoint)",
		""),
	R("plugin.param_set", RC::TrueInverse, true,
		"the parameter is an AutomatableModel, i.e. a JournallingObject",
		"ProjectJournal (parameter model checkpoint)",
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

	// =====================================================================
	// snapshot - no live object can be restored. The inverse is a bounded
	// recorded state: a container snapshot, a file revision, or a scalar.
	// reversible=false means the recorded state is a MANUAL fallback: an undo
	// attempt is refused, typed, and names the fallback rather than
	// pretending.
	// =====================================================================
	R("project.save", RC::Snapshot, true,
		"the engine writes the project file in place and keeps no previous "
		"revision by default. DISAGREEMENT with A16-STATUS-MEASURED.md, which "
		"records this as reversible:false with no file-level inverse",
		"file revision: before the write, the existing file is rotated into a "
		"named retention set (keep-3: <file>.rev0, .rev1, .rev2, each capped at "
		"8 MiB, 24 MiB per project). The recorded inverse is the command "
		"project.restore_revision, which control.undo dispatches. File-level "
		"commands are deliberately NOT put on the GUI undo stack - see "
		"docs/A16-REVERSIBILITY.md - because a file is not project state",
		""),
	R("plugin.load", RC::Snapshot, false,
		"two branches: appending an EFFECT has an exact inverse (the instance "
		"is new and carries only defaults), but REPLACING an instrument does "
		"not - the replaced instrument and its parameter values are gone",
		"the effect branch pushes an action checkpoint that unloads the device "
		"it created (plugin.load of an effect is therefore one undoable step). "
		"The instrument branch records the replaced plugin NAME only and is "
		"NOT reversible: replacing an instrument destroys its state",
		"the instrument branch: reload the previous instrument by dev-<n> "
		"(the transaction's before.plugin names it); its parameter values are "
		"NOT restored, so treat an instrument replacement as destructive"),
	R("mixer.remove_channel", RC::Snapshot, false,
		"the channel's full state is in the transaction's before-state, but "
		"there is no command that recreates a channel WITH state: "
		"mixer.add_channel always makes a default channel",
		"bounded container snapshot: before-state holds the removed channel's "
		"name, gain, mute/solo, sends and effects, so the state is not lost - "
		"only the automatic replay is missing. DISAGREEMENT with "
		"A16-STATUS-MEASURED.md in one respect: the doc lists this command "
		"under \"not exercised\", so its class was unmeasured; this row is the "
		"measurement",
		"recreate the channel with mixer.add_channel and re-apply name, gain, "
		"mute/solo and sends from before; the effects must be loaded again "
		"with plugin.load and their state XML re-applied"),
	R("plugin.state_save", RC::Snapshot, false,
		"the command writes a file OUTSIDE the project; the project is "
		"untouched, and the file's previous revision is recorded in "
		"before.previous_content (bounded)",
		"file-level snapshot: the replaced revision is in "
		"before.previous_content, bounded like every other state snapshot",
		"write before.previous_content back with plugin.state_save, or "
		"delete the file when before.replaced was false"),
	R("plugin.preset_save", RC::Snapshot, false,
		"the command writes a file OUTSIDE the project; the replaced revision "
		"is recorded in before.previous_content (bounded)",
		"file-level snapshot: the replaced revision is in "
		"before.previous_content",
		"write before.previous_content back with plugin.preset_save, or "
		"delete the file when before.replaced was false"),

	// =====================================================================
	// irreversible - no inverse exists in this engine, by nature of the
	// command. An undo attempt must FAIL with the typed 'irreversible' error
	// and name the fallback. Nothing here pretends.
	// =====================================================================
	R("project.open", RC::Irreversible, false,
		"loading a project replaces the whole session, and the engine keeps no "
		"pre-load snapshot: the previous document, including any UNSAVED "
		"edits, is gone",
		"none. The transaction records the previous file path and its sha256 so "
		"the caller can see what was displaced",
		"reopen the file named in before.previous_file; unsaved changes to the "
		"displaced session are LOST - save first (project.save keeps a "
		"revision) if they matter"),
	R("script.run", RC::Irreversible, false,
		"a Lua script mutates the engine through its own bindings; the "
		"registry sees one command and cannot know what the script wrote",
		"none. A script that wants to be undoable must take its own checkpoint "
		"(Lua addCheckPoint()), which ProjectJournal::undo DOES replay - so "
		"the record is honest about what it can and cannot cover",
		"run a script that takes its own checkpoint (Lua addCheckPoint()) "
		"before it edits; control.undo then replays that checkpoint"),
	R("plugin.unload", RC::Irreversible, false,
		"the removed Effect's state XML is captured, but recreating the "
		"instance would need plugin.load by catalogue id plus a state restore, "
		"and the instance id (fx-<n>) is position-derived - the inverse is not "
		"one operation the registry can run",
		"none. before holds the device's full state XML (bounded at 64 KiB) "
		"plus its plugin name and chain index",
		"write before.state_xml to a file, plugin.load the same dev-<n> onto "
		"the same target, then plugin.state_load that file. The chain ORDER "
		"is not restored"),

	// =====================================================================
	// writes nothing (or refuses every call) - there is no transaction, and
	// therefore nothing for control.undo to reverse or to be blocked by.
	// =====================================================================
	R("clip.select", RC::NotMutating, false,
		"selection is control-surface view state: it is not serialized, the "
		"GUI keeps its own copy in QGraphicsItem state, and no engine "
		"checkpoint can hold it",
		"nothing to inverse in the project. The registry records NO transaction "
		"for this command (mutating is false), so a select cannot block or "
		"shadow the undo of a real edit; the previous selection is reported in "
		"the result so a client can restore the view itself",
		""),
	R("note.select", RC::NotMutating, false,
		"same view state, per note",
		"same: no transaction, the previous selection is reported in the result",
		""),
	R("mixer.set_pan", RC::NotMutating, false,
		"declared mutating, but the handler REFUSES every call: this tree has "
		"no pan on a MixerChannel, and inventing one would change the mixer's "
		"serialization format",
		"no write happens, so no transaction is recorded and control.undo is "
		"not blocked by it",
		"none needed: the command is a typed refusal, use track panning "
		"(InstrumentTrack/SampleTrack panningModel) or per-note panning"),
	R("track.set_arm", RC::NotMutating, false,
		"declared mutating, but the handler REFUSES every call: arm state "
		"lives on the prototype MultiTrackRecorder, not on lmms::Track",
		"no write happens, so no transaction is recorded",
		"none needed: the command is a typed refusal"),
	R("automation.mode_set", RC::NotMutating, false,
		"declared mutating, but the handler REFUSES every call: this build has "
		"no automation modes (docs/KNOWN-LIMITATIONS.md:84)",
		"no write happens, so no transaction is recorded",
		"use automation.add_point to write a curve instead"),
	R("control.undo", RC::NotMutating, false,
		"it IS the inverse applier: it is the thing that reverses another "
		"command, so classifying it as a command to be reversed would recurse",
		"the engine's ProjectJournal",
		""),
	R("control.redo", RC::NotMutating, false,
		"the inverse of the undo; it re-applies a step, it is not project state",
		"the engine's ProjectJournal",
		""),
	R("control.quit", RC::NotMutating, false,
		"process lifecycle, not a project edit; the response reports whether "
		"unsaved changes were discarded so the caller is not surprised",
		"no project state is written",
		""),
	R("transport.play", RC::NotMutating, false,
		"the transport run state is engine state, not project state, and the "
		"registry has never recorded a transaction for it",
		"nothing to reverse: transport.stop is the operation a client calls, "
		"and it is available directly",
		""),
	R("transport.stop", RC::NotMutating, false,
		"same engine run state",
		"nothing to reverse: transport.play is the operation",
		""),
	R("render.render", RC::NotMutating, false,
		"it writes an OUTPUT ARTEFACT; the session it renders is not modified "
		"(it serialises to a temp file and removes it)",
		"the project is unchanged; the rendered file is an output, not project "
		"state, and overwriting it is the caller's decision",
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
	R("track.get_state", RC::NotMutating, false, "reads one track", "no write", ""),
	R("track.list", RC::NotMutating, false, "reads the track container", "no write", ""),
	R("transport.get_state", RC::NotMutating, false, "reads the transport", "no write", ""),
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
