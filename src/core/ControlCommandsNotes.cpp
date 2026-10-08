/*
 * ControlCommandsNotes.cpp - the note.* (piano roll) commands and roll.get_state
 *                            (SPEC A11-A16).
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

#include <QJsonArray>
#include <QJsonObject>

#include "ClipLinks.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "MidiClip.h"
#include "Note.h"
#include "Song.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

const QString MechanismClipCheckpoint = QStringLiteral("ProjectJournal (MidiClip checkpoint: "
	"MidiClip::loadSettings clears and re-loads the clip's note list, which is the mechanism the "
	"piano roll's own note edits reverse with)");

//! Index of \p note in its clip's note list, or -1 when it is not in it.
int indexOfNote(MidiClip* clip, const Note* note)
{
	const NoteVector& notes = clip->notes();
	for (int i = 0; i < static_cast<int>(notes.size()); ++i)
	{
		if (notes[i] == note) { return i; }
	}
	return -1;
}

/**
 * Snapshot the current selection as note IDS, which travel with the note objects
 * through a re-sort (Note::id(), SPEC-stable-ids.md slice 2).
 */
QVector<QString> selectedNoteIdsSnapshot(MidiClip* clip, const QString& clipText)
{
	QVector<QString> ids;
	for (int position : control::selectedNoteIndices(clipText))
	{
		if (position >= 0 && position < static_cast<int>(clip->notes().size()))
		{
			ids.append(control::noteIdOf(clip->notes()[position]));
		}
	}
	return ids;
}

/**
 * Re-point the selection STORE at the current positions of the notes named by
 * \a ids, after a reorder moved them. The store holds note-list positions
 * (control::selectNotes, src/core/ControlEditSupport.cpp:264-275), so a re-sort
 * that is not followed by this leaves a stored position naming a different note -
 * which is what note.remove already remaps for its own case.
 */
void remapSelectionAfterReorder(MidiClip* clip, const QString& clipText, const QVector<QString>& ids)
{
	if (ids.isEmpty()) { return; }
	QVector<int> positions;
	for (int i = 0; i < static_cast<int>(clip->notes().size()); ++i)
	{
		if (ids.contains(control::noteIdOf(clip->notes()[i]))) { positions.append(i); }
	}
	control::selectNotes(clipText, static_cast<int>(clip->notes().size()), positions);
}

QJsonObject noteArgsOf(const QJsonObject& args, bool withVelocity)
{
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("clip"), args.value(QStringLiteral("clip")).toString());
	inverseArgs.insert(QStringLiteral("key"), args.value(QStringLiteral("key")).toInt());
	inverseArgs.insert(QStringLiteral("position"), args.value(QStringLiteral("position")).toInt());
	inverseArgs.insert(QStringLiteral("length"), args.value(QStringLiteral("length")).toInt());
	if (withVelocity) { inverseArgs.insert(QStringLiteral("velocity"), args.value(QStringLiteral("velocity")).toInt(DefaultVolume)); }
	return inverseArgs;
}

QJsonObject noteAddSchema()
{
	return control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("key"), control::integerProperty(0, NumKeys - 1)},
		{QStringLiteral("position"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("length"), control::integerProperty(1, MaxSongLength)},
		{QStringLiteral("velocity"), control::integerProperty(MinVolume, MaxVolume)},
	}, {QStringLiteral("clip"), QStringLiteral("key"), QStringLiteral("position"),
		QStringLiteral("length")});
}

QJsonObject clipNoteSchema()
{
	return control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("note"), control::stringProperty()},
	}, {QStringLiteral("clip"), QStringLiteral("note")});
}

void registerNoteAdd(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.add");
	cmd.group = QStringLiteral("note");
	cmd.verb = QStringLiteral("add");
	cmd.description = QStringLiteral("Add a note to a clip (key 0..127, ticks, velocity 0..200) and return "
		"its stable note-<n> id. Reversible through the ProjectJournal (MidiClip checkpoint).");
	cmd.argsSchema = noteAddSchema();
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("note"), control::stringProperty()},
		{QStringLiteral("key"), control::integerProperty(0, NumKeys - 1)},
		{QStringLiteral("position"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("length"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("id"), control::stringProperty()},
		{QStringLiteral("index"), control::numberProperty()},
		{QStringLiteral("midi_velocity"), control::numberProperty()},
		{QStringLiteral("note_count"), control::numberProperty()},
		{QStringLiteral("pan"), control::numberProperty()},
		{QStringLiteral("slide"), control::booleanProperty()},
		{QStringLiteral("velocity"), control::numberProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		control::ClipRef ref;
		MidiClip* clip = control::resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
		if (clip == nullptr) { return error; }
		const int before = static_cast<int>(clip->notes().size());
		QJsonObject beforeState;
		beforeState.insert(QStringLiteral("clip"), control::clipId(ref.id));
		beforeState.insert(QStringLiteral("note_count"), before);
		beforeState.insert(QStringLiteral("notes"), noteArgsOf(args, true));

		// The piano roll checkpoints the clip before adding a note (PianoRoll.cpp);
		// MidiClip::addNote itself never journals.
		clip->addJournalCheckPoint();
		Note fresh(TimePos(static_cast<tick_t>(args.value(QStringLiteral("length")).toDouble())),
			TimePos(static_cast<tick_t>(args.value(QStringLiteral("position")).toDouble())),
			args.value(QStringLiteral("key")).toInt(),
			static_cast<volume_t>(args.value(QStringLiteral("velocity")).toDouble(DefaultVolume)));
		// quant_pos = false: the caller's ticks are taken as given, not snapped to
		// the piano roll's current quantisation.
		Note* added = clip->addNote(fresh, false);
		added->setSelected(false);
		clip->dataChanged();

		const int index = indexOfNote(clip, added);
		if (index < 0)
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("the added note is not in the clip"));
		}
		QJsonObject result = control::noteState(added, index);
		result.insert(QStringLiteral("clip"), control::clipId(ref.id));
		// The added note's own id (Note::id(), SPEC-stable-ids.md slice 2):
		// `index` is where it landed in the sorted list, not its address - the
		// inverse below must survive the next re-sort, which is the whole point.
		result.insert(QStringLiteral("note"), control::noteIdOf(added));
		result.insert(QStringLiteral("note_count"), static_cast<int>(clip->notes().size()));
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(beforeState, QStringLiteral("note.remove"),
				QJsonObject{{QStringLiteral("clip"), control::clipId(ref.id)},
					{QStringLiteral("note"), control::noteIdOf(added)}},
				true, MechanismClipCheckpoint));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerNoteRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.remove");
	cmd.group = QStringLiteral("note");
	cmd.verb = QStringLiteral("remove");
	cmd.description = QStringLiteral("Delete a note from a clip. Reversible through the ProjectJournal "
		"(MidiClip checkpoint).");
	cmd.argsSchema = clipNoteSchema();
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("removed"), control::stringProperty()},
		{QStringLiteral("removed_note"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
		{QStringLiteral("note_count"), control::integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		control::ClipRef ref;
		MidiClip* clip = control::resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
		if (clip == nullptr) { return error; }
		int index = -1;
		Note* note = control::resolveNote(clip, args.value(QStringLiteral("note")).toString(), &index, &error);
		if (note == nullptr) { return error; }
		// The removed note's full state rides along in the before-payload, but note.add
		// could never be a faithful inverse of it: it carries no pan, probability or
		// expression, and it cannot restore the note's id. The clip checkpoint taken
		// below can restore all of them, so the advertised inverse is control.undo.
		QJsonObject state = control::noteState(note, index);
		// The id is read BEFORE the removeNote() below deletes the note
		// (MidiClip::removeNote does `delete note`). It is the note's own id
		// (Note::id(), SPEC-stable-ids.md slice 2), which is what the result
		// reports and what an `removed` id is meant to name.
		const QString id = control::noteIdOf(note);

		clip->addJournalCheckPoint();
		clip->removeNote(note);
		clip->dataChanged();

		// The selection store holds POSITIONS in the note list
		// (control::selectNotes, ControlEditSupport.cpp:264-275), and this remove
		// shifts every note after the removed one down by one - so a stored
		// position now names a different note, or (the removed note's own
		// position, and any position that has fallen off the end) no note at all
		// and is silently dropped by rollState. The positions are remapped here,
		// in the one place that moved them: a position above the removed one comes
		// down by one, the removed position itself goes, and everything else is
		// left exactly where it was. Re-writing the store through selectNotes()
		// also re-sorts and de-duplicates it, so this cannot widen it.
		if (!control::selectedNoteIndices(control::clipId(ref.id)).isEmpty())
		{
			QVector<int> kept;
			for (int position : control::selectedNoteIndices(control::clipId(ref.id)))
			{
				if (position == index) { continue; }
				kept.append(position > index ? position - 1 : position);
			}
			control::selectNotes(control::clipId(ref.id), static_cast<int>(clip->notes().size()), kept);
		}

		QJsonObject result;
		result.insert(QStringLiteral("removed"), id);
		result.insert(QStringLiteral("removed_note"), state);
		result.insert(QStringLiteral("note_count"), static_cast<int>(clip->notes().size()));
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(state, QStringLiteral("control.undo"), QJsonObject(),
				true, MechanismClipCheckpoint));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerNoteMove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.move");
	cmd.group = QStringLiteral("note");
	cmd.verb = QStringLiteral("move");
	cmd.description = QStringLiteral("Move a note to an absolute tick position inside its clip. The move "
		"re-sorts the clip's note list, so the result reports the note's new id.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("note"), control::stringProperty()},
		{QStringLiteral("position"), control::integerProperty(0, MaxSongLength)},
	}, {QStringLiteral("clip"), QStringLiteral("note"), QStringLiteral("position")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("note"), control::stringProperty()},
		{QStringLiteral("previous_id"), control::stringProperty()},
		{QStringLiteral("position"), control::integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		control::ClipRef ref;
		MidiClip* clip = control::resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
		if (clip == nullptr) { return error; }
		int index = -1;
		Note* note = control::resolveNote(clip, args.value(QStringLiteral("note")).toString(), &index, &error);
		if (note == nullptr) { return error; }
		// A note's id is its own from now on (Note::id(), SPEC-stable-ids.md
		// slice 2), so the re-sort below cannot change it: previous_id is the id
		// the note had before the move, which is the id it still has. The key
		// stays - the result schema declares it, and the transaction's
		// before-payload carries it.
		const QString previousId = control::noteIdOf(note);
		const tick_t previousPos = note->pos().getTicks();

		// The schema bounds the new START (0..MaxSongLength), but the note's END
		// must fit inside the song too: a positive-length note moved to MaxSongLength
		// would end past the limit. Refuse before anything changes. Note lengths are
		// NOT schema-bounded everywhere - Note::loadSettings reads them straight from
		// XML without a MaxSongLength clamp - so the end is computed in a wide type:
		// an int32 sum with a near-INT_MAX loaded length would overflow and bypass this.
		const tick_t position = static_cast<tick_t>(args.value(QStringLiteral("position")).toDouble());
		const qint64 end = static_cast<qint64>(position) + static_cast<qint64>(note->length().getTicks());
		if (end > MaxSongLength)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
			 QStringLiteral("the move would put the note's end past the song limit: position %1 + length %2 ends at %3 ticks, past the limit of %4")
				 .arg(position).arg(note->length().getTicks()).arg(end).arg(MaxSongLength));
		}

		const QString clipText = control::clipId(ref.id);
		const QVector<QString> selectionBeforeMove = selectedNoteIdsSnapshot(clip, clipText);

		clip->addJournalCheckPoint();
		note->setPos(TimePos(position));
		// Same follow-up calls as a piano-roll drag: re-sort, then let the clip
		// follow its notes when it is still auto-resizing.
		clip->rearrangeAllNotes();
		clip->updateLength();
		// The re-sort above moved notes around, and the selection store holds
		// POSITIONS in that list (control::selectNotes,
		// src/core/ControlEditSupport.cpp:264-275) - so a stored position now
		// names whichever note took its place, and a selection-scoped edit
		// after the move would target the wrong note. The positions are
		// remapped here, in the one place that moved them, exactly as
		// note.remove remaps them for its own shift. The note objects' own
		// selection flags travel with the notes and need no remapping.
		remapSelectionAfterReorder(clip, clipText, selectionBeforeMove);
		clip->dataChanged();
		// Row 6: a content edit to a linked clip is an edit to the whole group -
		// the members' note lists are rebuilt from this clip's, in the same step.
		if (clip->linkId() > 0) { ClipLinks::mirrorContent(clip); }

		const int newIndex = indexOfNote(clip, note);
		if (newIndex < 0)
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("the note is not in the clip after the move"));
		}
		QJsonObject result;
		result.insert(QStringLiteral("clip"), control::clipId(ref.id));
		// The note's own id, which the move did NOT change, and its new POSITION
		// reported alongside (the re-sort may have moved it in the list).
		result.insert(QStringLiteral("note"), control::noteIdOf(note));
		result.insert(QStringLiteral("previous_id"), previousId);
		result.insert(QStringLiteral("position"), note->pos().getTicks());
		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("clip"), control::clipId(ref.id));
		// The inverse is addressed by ID too, so undoing the move finds the note
		// wherever the re-sort left it.
		inverseArgs.insert(QStringLiteral("note"), control::noteIdOf(note));
		inverseArgs.insert(QStringLiteral("position"), previousPos);
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(
				QJsonObject{{QStringLiteral("clip"), control::clipId(ref.id)},
					{QStringLiteral("note"), previousId},
					{QStringLiteral("position"), previousPos}},
				QStringLiteral("note.move"), inverseArgs, true, MechanismClipCheckpoint));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerNoteResize(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.resize");
	cmd.group = QStringLiteral("note");
	cmd.verb = QStringLiteral("resize");
	cmd.description = QStringLiteral("Set a note's length in ticks. Reversible through the ProjectJournal "
		"(MidiClip checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("note"), control::stringProperty()},
		{QStringLiteral("length"), control::integerProperty(1, MaxSongLength)},
	}, {QStringLiteral("clip"), QStringLiteral("note"), QStringLiteral("length")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("note"), control::stringProperty()},
		{QStringLiteral("length"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("id"), control::stringProperty()},
		{QStringLiteral("index"), control::numberProperty()},
		{QStringLiteral("key"), control::numberProperty()},
		{QStringLiteral("midi_velocity"), control::numberProperty()},
		{QStringLiteral("pan"), control::numberProperty()},
		{QStringLiteral("position"), control::numberProperty()},
		{QStringLiteral("slide"), control::booleanProperty()},
		{QStringLiteral("velocity"), control::numberProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		control::ClipRef ref;
		MidiClip* clip = control::resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
		if (clip == nullptr) { return error; }
		int index = -1;
		Note* note = control::resolveNote(clip, args.value(QStringLiteral("note")).toString(), &index, &error);
		if (note == nullptr) { return error; }
		const tick_t requestedLength = static_cast<tick_t>(args.value(QStringLiteral("length")).toDouble());
		const qint64 requestedEnd = static_cast<qint64>(note->pos().getTicks()) + requestedLength;
		if (requestedEnd > static_cast<qint64>(MaxSongLength))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("resizing the note would end at %1, past the song limit of %2")
					.arg(requestedEnd).arg(static_cast<qint64>(MaxSongLength)));
		}
		const tick_t previous = note->length().getTicks();

		clip->addJournalCheckPoint();
		note->setLength(TimePos(requestedLength));
		clip->updateLength();
		clip->dataChanged();
		if (clip->linkId() > 0) { ClipLinks::mirrorContent(clip); }

		QJsonObject result = control::noteState(note, index);
		result.insert(QStringLiteral("clip"), control::clipId(ref.id));
		// The note's own id (Note::id(), SPEC-stable-ids.md slice 2): a resize
		// does not re-sort the list, but the id is still what addresses the note.
		result.insert(QStringLiteral("note"), control::noteIdOf(note));
		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("clip"), control::clipId(ref.id));
		inverseArgs.insert(QStringLiteral("note"), control::noteIdOf(note));
		inverseArgs.insert(QStringLiteral("length"), previous);
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(
				QJsonObject{{QStringLiteral("clip"), control::clipId(ref.id)},
					{QStringLiteral("note"), control::noteIdOf(note)},
					{QStringLiteral("length"), previous}},
				QStringLiteral("note.resize"), inverseArgs, true, MechanismClipCheckpoint));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerNoteVelocitySet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.velocity_set");
	cmd.group = QStringLiteral("note");
	cmd.verb = QStringLiteral("velocity_set");
	cmd.description = QStringLiteral("Set a note's velocity (0..200, the engine's note volume scale; the "
		"piano roll's own velocity dialog uses the same range).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("note"), control::stringProperty()},
		{QStringLiteral("velocity"), control::integerProperty(MinVolume, MaxVolume)},
	}, {QStringLiteral("clip"), QStringLiteral("note"), QStringLiteral("velocity")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("note"), control::stringProperty()},
		{QStringLiteral("velocity"), control::numberProperty()},
		{QStringLiteral("midi_velocity"), control::integerProperty(0, 127)},
		{QStringLiteral("id"), control::stringProperty()},
		{QStringLiteral("index"), control::numberProperty()},
		{QStringLiteral("key"), control::numberProperty()},
		{QStringLiteral("length"), control::numberProperty()},
		{QStringLiteral("pan"), control::numberProperty()},
		{QStringLiteral("position"), control::numberProperty()},
		{QStringLiteral("slide"), control::booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		control::ClipRef ref;
		MidiClip* clip = control::resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
		if (clip == nullptr) { return error; }
		int index = -1;
		Note* note = control::resolveNote(clip, args.value(QStringLiteral("note")).toString(), &index, &error);
		if (note == nullptr) { return error; }
		const volume_t previous = note->getVolume();

		clip->addJournalCheckPoint();
		note->setVolume(static_cast<volume_t>(args.value(QStringLiteral("velocity")).toDouble()));
		clip->dataChanged();
		if (clip->linkId() > 0) { ClipLinks::mirrorContent(clip); }

		QJsonObject result = control::noteState(note, index);
		result.insert(QStringLiteral("clip"), control::clipId(ref.id));
		// The note's own id (Note::id(), SPEC-stable-ids.md slice 2).
		result.insert(QStringLiteral("note"), control::noteIdOf(note));
		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("clip"), control::clipId(ref.id));
		inverseArgs.insert(QStringLiteral("note"), control::noteIdOf(note));
		inverseArgs.insert(QStringLiteral("velocity"), static_cast<double>(previous));
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(
				QJsonObject{{QStringLiteral("clip"), control::clipId(ref.id)},
					{QStringLiteral("note"), control::noteIdOf(note)},
					{QStringLiteral("velocity"), static_cast<double>(previous)}},
				QStringLiteral("note.velocity_set"), inverseArgs, true, MechanismClipCheckpoint));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

//! The notes' own selection flags, set to exactly @a positions (BUGS_FOUND 11.33).
void markSelected(MidiClip* clip, const QVector<int>& positions)
{
	for (Note* note : clip->notes()) { note->setSelected(false); }
	for (int position : positions) { clip->notes()[position]->setSelected(true); }
}

//! The ids of the notes whose own selection flag is set. The selection STORE holds
//! note-list positions, which a reorder (note.move, note.split, ...) invalidates, so a
//! previous selection reported from it names the wrong note; the flags travel with the
//! note objects.
QJsonArray selectedNoteIds(const MidiClip* clip)
{
	QJsonArray ids;
	for (const Note* note : clip->notes())
	{
		if (note->selected()) { ids.append(control::noteIdOf(note)); }
	}
	return ids;
}

//! 0.5.0 L07 (ED-069): @a positions with every note that shares a group with one of them.
void withGroupMates(const MidiClip* clip, QVector<int>* positions)
{
	QVector<int> groups;
	for (int position : *positions) { if (const int g = clip->notes()[position]->conditions().group; g > 0) { groups.append(g); } }
	if (groups.isEmpty()) { return; }
	for (int i = 0; i < static_cast<int>(clip->notes().size()); ++i)
	{
		if (groups.contains(clip->notes()[i]->conditions().group) && !positions->contains(i)) { positions->append(i); }
	}
}

void registerNoteSelect(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.select");
	cmd.group = QStringLiteral("note");
	cmd.verb = QStringLiteral("select");
	cmd.description = QStringLiteral("Select notes in a clip (an empty list clears the selection); a note of a group "
		"(note.group) brings the rest of its group. Control-surface state, not project state.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("notes"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
			{QStringLiteral("items"), control::stringProperty()}}},
	}, {QStringLiteral("clip"), QStringLiteral("notes")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("selected_notes"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("previous_notes"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
	});
	// SPEC A16: piano-roll selection is VIEW state (Note::setSelected), not
	// project state, so this command records no transaction (see clip.select).
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		control::ClipRef ref;
		MidiClip* clip = control::resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
		if (clip == nullptr) { return error; }
		const QString clipText = control::clipId(ref.id);
		// Read BEFORE the new selection is marked, and from the notes' own flags: the
		// store holds positions, which a reorder invalidates.
		const QJsonArray previous = selectedNoteIds(clip);
		QVector<int> wanted;
		for (const QJsonValue& value : args.value(QStringLiteral("notes")).toArray())
		{
			// The argument names the OBJECT by its id (Note::id(),
			// SPEC-stable-ids.md slice 2), exactly as note.remove/note.move
			// resolve theirs: resolveNote looks the id up in the clip and fills
			// `position` with where the note currently sits. That position is
			// what the selection store holds - it is view state (SPEC A16) and
			// carries no ids of its own - and it is reported back as ids below.
			int position = -1;
			Note* note = control::resolveNote(clip, value.toString(), &position, &error);
			if (note == nullptr) { return error; }
			wanted.append(position);
		}
		withGroupMates(clip, &wanted);
		control::selectNotes(clipText, static_cast<int>(clip->notes().size()), wanted);
		// The view state the comment above names: the notes' own selection flags, which the
		// note editor and the classic piano roll draw and act on. Before 0.5.0 only the index
		// store above was written, so a selection made over the socket never reached the
		// editors (BUGS_FOUND 11.33). The flags travel with the note objects through a re-sort.
		markSelected(clip, wanted);
		clip->dataChanged();

		// The selection is reported as the IDS of the notes at the stored
		// positions (rollState reports the same way). A position the clip's list
		// no longer holds is dropped rather than reported as a dangling id.
		const NoteVector& noteList = clip->notes();
		QJsonArray selected;
		for (int position : control::selectedNoteIndices(clipText))
		{
			if (position >= 0 && position < static_cast<int>(noteList.size()))
			{
				selected.append(control::noteIdOf(noteList[position]));
			}
		}
		QJsonObject result;
		result.insert(QStringLiteral("clip"), clipText);
		result.insert(QStringLiteral("selected_notes"), selected);
		result.insert(QStringLiteral("previous_notes"), previous);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerRollGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("roll.get_state");
	cmd.group = QStringLiteral("roll");
	cmd.verb = QStringLiteral("get_state");
	cmd.description = QStringLiteral("The piano-roll view of a clip: the clip and every note with its "
		"position, length, key and velocity. Without 'clip', the selected clip is used.");
	cmd.argsSchema = control::objectSchema({{QStringLiteral("clip"), control::stringProperty()}});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("notes"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("note_count"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("auto_resize"), control::booleanProperty()},
		{QStringLiteral("fade_in"), control::numberProperty()},
		{QStringLiteral("fade_in_shape"), control::stringProperty()},
		{QStringLiteral("fade_out"), control::numberProperty()},
		{QStringLiteral("fade_out_shape"), control::stringProperty()},
		{QStringLiteral("gain_db"), control::numberProperty()},
		{QStringLiteral("id"), control::stringProperty()},
		{QStringLiteral("index_in_track"), control::numberProperty()},
		{QStringLiteral("length"), control::numberProperty()},
		{QStringLiteral("muted"), control::booleanProperty()},
		{QStringLiteral("name"), control::stringProperty()},
		{QStringLiteral("position"), control::numberProperty()},
		{QStringLiteral("selected"), control::booleanProperty()},
		{QStringLiteral("selected_notes"), control::arrayProperty()},
	});
	cmd.handler = [](const QJsonObject& args) {
		QString id = args.value(QStringLiteral("clip")).toString();
		if (id.isEmpty()) { id = control::selectedClipId(); }
		if (id.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("no 'clip' given and no clip is selected (clip.select sets one)"));
		}
		ControlResult error;
		control::ClipRef ref;
		MidiClip* clip = control::resolveMidiClip(id, &ref, &error);
		if (clip == nullptr) { return error; }
		return ControlResult::success(control::rollState(ref));
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerNoteCommands(ControlRegistry& registry)
{
	registerNoteAdd(registry);
	registerNoteRemove(registry);
	registerNoteMove(registry);
	registerNoteResize(registry);
	registerNoteVelocitySet(registry);
	registerNoteSelect(registry);
	registerRollGetState(registry);
}

} // namespace lmms
