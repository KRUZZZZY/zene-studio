/*
 * ControlCommandsNoteStepInput.cpp - note.step_input: one step of step entry (a note or a chord)
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
 *
 */

/*! Step entry: press a key, a note (or a chord) lands at the step cursor, the cursor moves on by
 *  the grid step. It is the note editor's Step input mode (docs/NOTE-EDITOR.md §3a), and an agent
 *  on the socket makes exactly the same call.
 *
 *  ONE COMMAND FOR BOTH ENTRY SOURCES. The editor's MIDI input (the instrument track's
 *  midiNoteOn) and its computer keys (Q Z S X D ... in the piano-view letters) both call this
 *  command with the editor's own grid and note length, so the two cannot use different settings
 *  and an agent's call cannot drift from the editor's.
 *
 *  ONE UNDO STEP PER STEP. The notes a call adds are added under one MidiClip journal checkpoint
 *  (the mechanism note.add and note.add_many use), so one control.undo takes the whole step back -
 *  a chord included. The step cursor is edit state, not project state: undo does not move it
 *  (include/ControlStepInput.h says why).
 */

#include <algorithm>
#include <utility>

#include <QJsonArray>
#include <QJsonObject>

#include "ControlCommandsNoteShared.h"
#include "ControlEdit.h"
#include "ControlNoteShared.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlStepInput.h"
#include "ControlVocabulary.h"
#include "MidiClip.h"
#include "Note.h"
#include "Song.h"  // MaxSongLength

namespace lmms
{

using namespace control;

namespace
{

//! A sixteenth note in the engine's 192-ticks-per-bar time: the step and the note length a call
//! that names neither takes (the note editor's own default grid is the same sixteenth).
constexpr int DefaultStepTicks = 12;
//! A step's chord: generous for two hands, and one journal step either way.
constexpr int MaxStepKeys = 32;

//! A step read out of the command's arguments and checked before anything is written.
struct Step
{
	QVector<int> keys;
	int position = 0;
	int length = DefaultStepTicks;
	int stepTicks = DefaultStepTicks;
	int velocity = DefaultVolume;
	bool select = true;
};

//! `key` (one pitch) or `keys` (a chord), never both: two names for one argument would let a
//! caller send both and have one silently ignored. Pitches are sorted and must be distinct -
//! two notes of one key in one step is a mistake, not a chord.
bool readKeys(const QJsonObject& args, QVector<int>* keys, ControlResult* error)
{
	const QJsonValue single = args.value(QStringLiteral("key"));
	const QJsonValue many = args.value(QStringLiteral("keys"));
	if (single.isUndefined() == many.isUndefined())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("give 'key' (one pitch) or 'keys' (a chord of up to %1), not both").arg(MaxStepKeys));
		return false;
	}
	QVector<int> read;
	if (!single.isUndefined()) { read.append(single.toInt(-1)); }
	else
	{
		for (const QJsonValue& value : many.toArray()) { read.append(value.toInt(-1)); }
	}
	if (read.isEmpty() || read.size() > MaxStepKeys)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a step is 1 to %1 pitches").arg(MaxStepKeys));
		return false;
	}
	for (const int key : read)
	{
		if (key < 0 || key >= NumKeys)
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("a step's pitches are MIDI keys 0..%1").arg(NumKeys - 1));
			return false;
		}
	}
	std::sort(read.begin(), read.end());
	if (std::adjacent_find(read.begin(), read.end()) != read.end())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'keys' names a pitch twice: a step holds one note per pitch"));
		return false;
	}
	*keys = read;
	return true;
}

//! The whole step: its pitches, where it starts (the clip's cursor unless `position` says
//! otherwise), its note length, how far the cursor moves on, its velocity and whether it selects
//! what it wrote.
bool readStep(const QJsonObject& args, const QString& clipId, Step* step, ControlResult* error)
{
	if (!readKeys(args, &step->keys, error)) { return false; }
	step->stepTicks = args.value(QStringLiteral("step")).toInt(DefaultStepTicks);
	step->length = args.value(QStringLiteral("length")).toInt(step->stepTicks);
	step->position = args.contains(QStringLiteral("position")) ? args.value(QStringLiteral("position")).toInt(-1)
		: stepInputCursor(clipId);
	step->velocity = args.value(QStringLiteral("velocity")).toInt(DefaultVolume);
	step->select = args.value(QStringLiteral("select")).toBool(true);
	if (step->stepTicks < 1 || step->length < 1 || step->position < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a step needs position >= 0, a length >= 1 and a step >= 1"));
		return false;
	}
	if (step->velocity < MinVolume || step->velocity > MaxVolume)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("velocity is %1..%2").arg(int{MinVolume}).arg(int{MaxVolume}));
		return false;
	}
	// position and length are each inside the song on their own; their sum is where the signed
	// 32-bit tick range (and the song's own length) can be left - the check note.add_many makes.
	const qint64 end = static_cast<qint64>(step->position) + static_cast<qint64>(step->length);
	if (end > static_cast<qint64>(MaxSongLength))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the step ends at %1, past the song limit of %2").arg(end).arg(static_cast<qint64>(MaxSongLength)));
		return false;
	}
	return true;
}

//! @a note's index in @a clip's own list, or -1 when it is not in it.
int indexOf(const MidiClip* clip, const Note* note)
{
	const NoteVector& notes = clip->notes();
	for (int i = 0; i < static_cast<int>(notes.size()); ++i)
	{
		if (notes[i] == note) { return i; }
	}
	return -1;
}

ControlResult stepInput(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	const QString clipText = clipId(ref.id);
	Step step;
	if (!readStep(args, clipText, &step, &error)) { return error; }

	clip->addJournalCheckPoint();  // ONE step for the whole chord (note.add's own mechanism)
	QVector<Note*> added;
	QJsonArray ids;
	for (const int key : step.keys)
	{
		Note* note = clip->addNote(Note(TimePos(step.length), TimePos(step.position), key,
			static_cast<volume_t>(step.velocity)), false);
		added.append(note);
		ids.append(noteIdOf(note));
	}
	// The step's own notes are the selection, as a click's note is: the next note.edit acts on
	// what was just entered, and the editor draws them selected. Selection is view state
	// (note.select), so it is not part of what undo restores, and `select: false` leaves the
	// selection alone.
	if (step.select)
	{
		// note.select's own rule: the flags of the notes it does not name go off, so the step's
		// notes are the whole selection and not one more note on top of the last step's.
		for (Note* note : clip->notes()) { note->setSelected(false); }
		QVector<int> indices;
		for (const Note* note : added) { indices.append(indexOf(clip, note)); }
		selectNotes(clipText, *clip, indices);
		for (Note* note : added) { note->setSelected(true); }
	}

	// The cursor moves on by the grid step, and is the clip's cursor whatever the call asked
	// for: a caller that passed `position` puts it there, so the next step continues from it.
	setStepInputCursor(clipText, step.position + step.stepTicks);
	const int cursor = stepInputCursor(clipText);

	QJsonObject result{
		{QStringLiteral("notes"), ids},
		{QStringLiteral("position"), step.position},
		{QStringLiteral("length"), step.length},
		{QStringLiteral("step"), step.stepTicks},
		{QStringLiteral("cursor"), cursor},
		{QStringLiteral("velocity"), step.velocity},
		{QStringLiteral("selected"), step.select},
		{QStringLiteral("note_count"), static_cast<int>(clip->notes().size())},
	};
	return ControlResult::success(notes::finish(ref, clip, QStringLiteral("note.step_input"), result));
}

} // namespace

void registerNoteStepInputCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.step_input");
	cmd.group = QStringLiteral("note");
	cmd.verb = QStringLiteral("step_input");
	cmd.description = QStringLiteral("Step entry: put one note - or a chord, with `keys` - at the step cursor "
		"and move the cursor on by the grid step, so a phrase is entered one key at a time. `key` is one MIDI "
		"pitch (0..127) and `keys` a chord of up to 32 distinct pitches (one of the two, never both); `position` "
		"(ticks) starts the step somewhere else than the cursor and becomes the cursor; `length` is the note's "
		"length (default: the step); `step` is how far the cursor advances (default 12, a sixteenth note); "
		"`velocity` 0..200; `select` (default true) makes the new notes the selection. The cursor is edit state, "
		"not project state: it is not saved and `control.undo` does not move it, it takes the step's notes back. "
		"The note editor's Step input mode calls this command for a MIDI key and for a computer key alike, with "
		"its own grid and note length. Reversible through the ProjectJournal (one MidiClip checkpoint per step).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("key"), integerProperty(0, NumKeys - 1)},
		{QStringLiteral("keys"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
			{QStringLiteral("items"), integerProperty(0, NumKeys - 1)}}},
		{QStringLiteral("position"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("length"), integerProperty(1, MaxSongLength)},
		{QStringLiteral("step"), integerProperty(1, MaxSongLength)},
		{QStringLiteral("velocity"), integerProperty(MinVolume, MaxVolume)},
		{QStringLiteral("select"), booleanProperty()},
	}, {QStringLiteral("clip")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("notes"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
			{QStringLiteral("items"), stringProperty()}}},
		{QStringLiteral("position"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("length"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("step"), integerProperty(1, MaxSongLength)},
		{QStringLiteral("cursor"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("velocity"), integerProperty(MinVolume, MaxVolume)},
		{QStringLiteral("selected"), booleanProperty()},
		{QStringLiteral("note_count"), integerProperty(0, 1 << 24)},
	});
	cmd.mutating = true;
	cmd.handler = stepInput;
	registry.registerCommand(cmd);
}

} // namespace lmms
