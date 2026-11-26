/*
 * ControlCommandsNoteRecord.cpp - live MIDI recording in the note editor: the key's edges
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

/*! Live recording: what a MIDI keyboard plays is written into the clip as it is played.
 *
 *  TWO EDGES, THREE COMMANDS, ONE TAKE. `note.record_on` opens one note where the playhead is
 *  (snapped down to the step) and `note.record_off` closes it where the playhead is when the key
 *  comes up; `note.record_end` ends the take and keeps whatever is still held as one step long. This
 *  is the pair the classic piano roll makes in PianoRoll::startRecordNote / finishRecordNote
 *  (src/gui/editors/PianoRoll.cpp:4516, :4557) - except that the classic roll writes the note only on
 *  the key-up, so a take it never finishes is lost, while a note here exists from the key's note-on
 *  and is only lengthened by the note-off.
 *
 *  ONE UNDO STEP PER TAKE. The first note of a take takes the clip's MidiClip journal checkpoint (the
 *  same rule the roll follows: one checkpoint per pass, PianoRoll.cpp:4410) and the rest of the take
 *  writes inside it, so `control.undo` takes the whole take back rather than its last note. An undo
 *  or redo ends the take (include/ControlNoteRecord.h), so the next note starts a new step, and the
 *  transport's stop ends it too (followTransportStops below).
 *
 *  THREADS: every handler runs on the GUI/control thread, and the editor's MIDI edges arrive there
 *  over its queued connection. Nothing here runs on the audio thread, allocates there, locks, waits
 *  or emits a signal from there.
 */

#include <algorithm>

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>

#include "ControlCommandsNoteShared.h"
#include "ControlEdit.h"
#include "ControlNoteRecord.h"
#include "ControlNoteShared.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MidiClip.h"
#include "Note.h"
#include "Song.h"  // MaxSongLength

namespace lmms
{

using namespace control;

namespace
{

//! A sixteenth in the engine's 192-ticks-per-bar time: the step a call that names none takes, the
//! same default note.step_input uses (and the note editor's own default grid).
constexpr int DefaultStepTicks = 12;

//! The song's limit in ticks, as the schema's integers carry it.
constexpr int MaxTicks = static_cast<int>(MaxSongLength);

//! @a ticks snapped down to @a step - the playhead is recorded on the grid the editor shows.
int snapDown(int ticks, int step)
{
	if (step < 1) { return ticks; }
	return (ticks / step) * step;
}

//! Where the playhead is inside @a clip, in ticks from the clip's start: what a call that names no
//! `position` records at, and closes at. Clamped at 0 for a playhead before the clip.
int recordHead(const MidiClip* clip, int step)
{
	const int song = static_cast<int>(Engine::getSong()->getPlayPos(Song::PlayMode::Song).getTicks());
	const int inClip = song - static_cast<int>(clip->startPosition().getTicks());
	return std::max(0, snapDown(inClip, step));
}

//! The note of @a clip whose id is @a id, or nullptr (an undo may have taken the take back).
Note* noteById(MidiClip* clip, const QString& id)
{
	if (id.isEmpty()) { return nullptr; }
	for (Note* note : clip->notes())
	{
		if (noteIdOf(note) == id) { return note; }
	}
	return nullptr;
}

//! The step a call advances by: `step` (a sixteenth when absent), checked before anything is written.
bool readStep(const QJsonObject& args, int* step, ControlResult* error)
{
	*step = args.value(QStringLiteral("step")).toInt(DefaultStepTicks);
	if (*step < 1 || *step > MaxTicks)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a step is 1..%1 ticks").arg(MaxTicks));
		return false;
	}
	return true;
}

/*! The transport's stop ends any open take: the notes it wrote stay (a key still held keeps the step
 *  it was given) and the next note starts a new undo step. Installed with the first recorded note,
 *  once per song, so the editor and an agent on the socket obey one rule rather than each carrying
 *  its own copy. `Song::stopped` is emitted from Song::stop() on the GUI thread
 *  (src/core/Song.cpp:1331), where every handler of this file runs, so nothing here touches the audio
 *  thread.
 */
void followTransportStops()
{
	static QPointer<Song> listening;
	Song* song = Engine::getSong();
	if (song == nullptr || listening == song) { return; }
	listening = song;
	QObject::connect(song, &Song::stopped, song, []() { endAllRecordTakes(); });
}

/*! Declare that @a finished's undo step is the TAKE's, not this call's: point its inverse at the
 *  journal step an earlier call of the same take took (src/core/ControlRegistry.cpp records a step only
 *  for the call that produced it, and would otherwise record this one as having no inverse at all).
 *  `control.undo` then unwinds the take, which is what one undo step per take means.
 */
QJsonObject declareTakeStep(QJsonObject finished)
{
	QJsonObject tx = finished.value(QStringLiteral("__transaction")).toObject();
	if (tx.isEmpty()) { return finished; }
	QJsonObject inverse = tx.value(QStringLiteral("inverse")).toObject();
	inverse.insert(QStringLiteral("applies"), QStringLiteral("journal_later"));
	tx.insert(QStringLiteral("inverse"), inverse);
	tx.insert(QStringLiteral("mechanism"), QStringLiteral("the take's MidiClip journal checkpoint, taken by the "
		"take's first note.record_on: one control.undo unwinds the whole take"));
	finished.insert(QStringLiteral("__transaction"), tx);
	return finished;
}

//! The notes this clip is holding, as ids, in the order the keys went down.
QJsonArray heldJson(const QString& clipText)
{
	QStringList held;
	for (const int key : heldRecordKeys(clipText)) { held.append(heldRecordNote(clipText, key)); }
	return QJsonArray::fromStringList(held);
}

//! The note's `key`: one MIDI pitch, or a failure that names the range.
bool readKey(const QJsonObject& args, int* pitch, ControlResult* error)
{
	const QJsonValue key = args.value(QStringLiteral("key"));
	if (!key.isDouble() || key.toInt(-1) < 0 || key.toInt(-1) >= NumKeys)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'key' is a MIDI pitch 0..%1").arg(NumKeys - 1));
		return false;
	}
	*pitch = key.toInt();
	return true;
}

//! The note's `velocity` (default: the velocity the editor draws with), inside the model's own range.
bool readVelocity(const QJsonObject& args, int* velocity, ControlResult* error)
{
	*velocity = args.value(QStringLiteral("velocity")).toInt(DefaultVolume);
	if (*velocity < MinVolume || *velocity > MaxVolume)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("velocity is %1..%2").arg(int{MinVolume}).arg(int{MaxVolume}));
		return false;
	}
	return true;
}

//! Where the note starts: the `position` argument, or the playhead inside the clip snapped down to the
//! step. False (with @a error) when the note would run past the song's limit.
bool readStart(const QJsonObject& args, MidiClip* clip, int step, int* position, ControlResult* error)
{
	*position = args.contains(QStringLiteral("position"))
		? args.value(QStringLiteral("position")).toInt(0) : recordHead(clip, step);
	if (*position < 0 || *position + step > MaxTicks)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the note would end at %1, past the song limit of %2").arg(*position + step).arg(MaxTicks));
		return false;
	}
	return true;
}

//! The note-on: one note where the playhead is, held until `note.record_off` closes it.
ControlResult recordOn(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	const QString clipText = clipId(ref.id);
	int step = DefaultStepTicks;
	int pitch = -1;
	int velocity = DefaultVolume;
	int position = 0;
	if (!readStep(args, &step, &error) || !readKey(args, &pitch, &error)
		|| !readVelocity(args, &velocity, &error))
	{
		return error;
	}
	if (!heldRecordNote(clipText, pitch).isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("key %1 is already held in this clip: close it with note.record_off first").arg(pitch));
	}
	if (!readStart(args, clip, step, &position, &error)) { return error; }

	const bool first = !recordTakeOpen(clipText);
	if (first)
	{
		followTransportStops();
		clip->addJournalCheckPoint();  // ONE step for the whole take
	}
	Note* note = clip->addNote(Note(TimePos(step), TimePos(position), pitch,
		static_cast<volume_t>(velocity)), false);
	holdRecordNote(clipText, pitch, noteIdOf(note));
	if (args.value(QStringLiteral("select")).toBool(false))
	{
		for (Note* other : clip->notes()) { other->setSelected(false); }
		note->setSelected(true);
	}
	QJsonObject placed{
		{QStringLiteral("note"), noteIdOf(note)},
		{QStringLiteral("key"), pitch},
		{QStringLiteral("position"), position},
		{QStringLiteral("length"), step},
		{QStringLiteral("velocity"), velocity},
		{QStringLiteral("take_started"), first},
		{QStringLiteral("held"), heldJson(clipText)},
	};
	QJsonObject finished = notes::finish(ref, clip, QStringLiteral("note.record_on"), placed);
	return ControlResult::success(first ? finished : declareTakeStep(finished));
}

//! The note-off: close the note @a key is holding where the playhead is, so its length is what was played.
ControlResult recordOff(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	const QString clipText = clipId(ref.id);
	int step = DefaultStepTicks;
	if (!readStep(args, &step, &error)) { return error; }
	const int pitch = args.value(QStringLiteral("key")).toInt(-1);
	Note* note = noteById(clip, heldRecordNote(clipText, pitch));
	if (note == nullptr)
	{
		// A key-up with nothing open - a stray release, or a stop or undo ended the take - writes nothing.
		releaseRecordNote(clipText, pitch);
		return ControlResult::success(QJsonObject{{QStringLiteral("clip"), clipId(ref.id)},
			{QStringLiteral("note"), QString()}, {QStringLiteral("key"), pitch},
			{QStringLiteral("length"), 0}, {QStringLiteral("closed"), false}});
	}
	const int end = args.contains(QStringLiteral("position"))
		? args.value(QStringLiteral("position")).toInt(0) : recordHead(clip, step);
	const int length = std::max(1, end - static_cast<int>(note->pos().getTicks()));
	note->setLength(TimePos(length));
	releaseRecordNote(clipText, pitch);
	QJsonObject placed{
		{QStringLiteral("note"), noteIdOf(note)},
		{QStringLiteral("key"), pitch},
		{QStringLiteral("length"), length},
		{QStringLiteral("closed"), true},
	};
	// The take's checkpoint was taken by its first note.record_on: this call writes inside it.
	return ControlResult::success(declareTakeStep(notes::finish(ref, clip, QStringLiteral("note.record_off"), placed)));
}

//! The end of the take: keep whatever is still held (one step long) and close the take's undo step.
ControlResult recordEnd(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	const QString clipText = clipId(ref.id);
	QJsonArray closed;
	for (const int pitch : heldRecordKeys(clipText))
	{
		Note* note = noteById(clip, heldRecordNote(clipText, pitch));
		if (note != nullptr) { closed.append(noteIdOf(note)); }
		releaseRecordNote(clipText, pitch);
	}
	endRecordTake(clipText);
	QJsonObject ended{
		{QStringLiteral("closed"), closed},
		{QStringLiteral("count"), static_cast<int>(closed.size())},
		{QStringLiteral("take_ended"), true},
	};
	// It changes no project data - the held notes keep the length they already have - so it takes no undo
	// step of its own: the take's checkpoint already covers everything the take wrote.
	return ControlResult::success(notes::finish(ref, clip, QStringLiteral("note.record_end"), ended, false));
}

//! One command struct with the boilerplate every note verb shares.
ControlCommand command(const QString& verb)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.") + verb;
	cmd.group = QStringLiteral("note");
	cmd.verb = verb;
	cmd.mutating = true;
	return cmd;
}

} // namespace

void registerNoteRecordCommands(ControlRegistry& registry)
{
	ControlCommand on = command(QStringLiteral("record_on"));
	on.description = QStringLiteral("Live recording's note-on: put one note at the playhead (snapped down to "
		"`step`, a sixteenth by default) and hold it until `note.record_off` closes it, so its length is what was "
		"played. The first note of a take takes the clip's MidiClip journal checkpoint and the rest of the take "
		"writes inside it - ONE undo step per take, not per note. The note editor's Record button sends this for "
		"every MIDI key it hears; an agent records the same way by sending the two edges. Reversible through the "
		"ProjectJournal (one MidiClip checkpoint per take).");
	on.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("key"), integerProperty(0, NumKeys - 1)},
		{QStringLiteral("position"), integerProperty(0, MaxTicks)},
		{QStringLiteral("step"), integerProperty(1, MaxTicks)},
		{QStringLiteral("velocity"), integerProperty(MinVolume, MaxVolume)},
		{QStringLiteral("select"), booleanProperty()},
	}, {QStringLiteral("clip"), QStringLiteral("key")});
	on.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("note"), stringProperty()},
		{QStringLiteral("key"), integerProperty(0, NumKeys - 1)},
		{QStringLiteral("position"), integerProperty(0, MaxTicks)},
		{QStringLiteral("length"), integerProperty(1, MaxTicks)},
		{QStringLiteral("velocity"), integerProperty(MinVolume, MaxVolume)},
		{QStringLiteral("take_started"), booleanProperty()},
		{QStringLiteral("held"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
			{QStringLiteral("items"), stringProperty()}}},
	});
	on.handler = recordOn;
	registry.registerCommand(on);

	ControlCommand off = command(QStringLiteral("record_off"));
	off.description = QStringLiteral("Live recording's note-off: close the note `key` is holding where the playhead "
		"is (snapped down to `step`), so its length is the time between the two edges. It writes inside the take's "
		"MidiClip checkpoint - no new undo step - so `control.undo` still takes the whole take back. A key-up with "
		"nothing open (a stray release, or a stop or undo that ended the take) writes nothing and answers closed: false.");
	off.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("key"), integerProperty(0, NumKeys - 1)},
		{QStringLiteral("position"), integerProperty(0, MaxTicks)},
		{QStringLiteral("step"), integerProperty(1, MaxTicks)},
	}, {QStringLiteral("clip"), QStringLiteral("key")});
	off.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("note"), stringProperty()},
		{QStringLiteral("key"), integerProperty(0, NumKeys - 1)},
		{QStringLiteral("length"), integerProperty(0, MaxTicks)},
		{QStringLiteral("closed"), booleanProperty()},
	});
	off.handler = recordOff;
	registry.registerCommand(off);

	ControlCommand end = command(QStringLiteral("record_end"));
	end.description = QStringLiteral("End a live recording take: every note still held is kept (one step long, so "
		"the key you pressed is not lost) and the take's undo step is closed, so the next note starts a new one. "
		"The note editor calls it when the transport stops, when Record is switched off and when another clip is "
		"opened; an agent calls it to end a take it started. Writes inside the take's MidiClip checkpoint.");
	end.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
	}, {QStringLiteral("clip")});
	end.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("closed"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
			{QStringLiteral("items"), stringProperty()}}},
		{QStringLiteral("count"), integerProperty(0, 1 << 16)},
		{QStringLiteral("take_ended"), booleanProperty()},
	});
	end.mutating = false;  // it ends the take; what the take wrote is in the take's own step
	end.handler = recordEnd;
	registry.registerCommand(end);
}

} // namespace lmms
