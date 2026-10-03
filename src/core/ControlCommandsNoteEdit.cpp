/*
 * ControlCommandsNoteEdit.cpp - note.edit: several notes' position, key, length, velocity,
 *                               pan and chance changed as ONE edit and ONE undo step
 *
 * The note editor's gestures (a drag of a selection, a resize of several notes, a velocity
 * ramp drawn across a lane) change many notes at once. Issued as one note.move / note.resize /
 * note.velocity_set per note, a single drag became dozens of journal entries and dozens of
 * undo steps, and a key change had no command at all (note.transpose moves a whole scope by a
 * fixed amount, not a note to a key). note.edit is the batch: every edit is validated before
 * anything is written (a batch with one bad entry changes nothing), then ONE MidiClip
 * checkpoint is taken and every edit applied, and the inverse is the same verb with each
 * note's previous values.
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

#include <vector>

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

using namespace control;

namespace
{

const QString MechanismClipCheckpoint = QStringLiteral("ProjectJournal (one MidiClip checkpoint for "
	"the whole batch: MidiClip::loadSettings clears and re-loads the clip's note list, so the "
	"checkpoint taken before the first write restores every note of the batch at once)");

//! The writable fields and their closed ranges (the engine's own: Note.h, the volume and
//! panning types). A value outside is refused, never clamped (SPEC A11).
struct Field
{
	const char* name;
	double minimum;
	double maximum;
};

constexpr Field kFields[] = {
	{"position", 0, MaxSongLength},
	{"key", 0, 127},
	{"length", 1, MaxSongLength},
	{"velocity", 0, 200},
	{"pan", -100, 100},
	{"probability", 0, 1},
	{"muted", 0, 1},              // 0.5.0 L07: 1 mutes (the note stays, unplayed)
	{"release_velocity", -1, 200}, // 0.5.0 L07: the note-off velocity, -1 none
};

//! One resolved edit: the note and the values it is to take.
struct Edit
{
	Note* note = nullptr;
	QJsonObject values;
};

bool checkEditValues(const QJsonObject& entry, int index, ControlResult* error)
{
	int fields = 0;
	for (const Field& field : kFields)
	{
		const QJsonValue value = entry.value(QLatin1String(field.name));
		if (value.isUndefined()) { continue; }
		++fields;
		const double v = value.toDouble();
		if (!value.isDouble() || v < field.minimum || v > field.maximum)
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("edits[%1].%2 must be a number in [%3, %4]")
					.arg(index).arg(QLatin1String(field.name)).arg(field.minimum).arg(field.maximum));
			return false;
		}
	}
	if (fields == 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("edits[%1] changes nothing: name at least one of position, key, length, "
				"velocity, pan, probability, muted, release_velocity").arg(index));
		return false;
	}
	return true;
}

//! Resolve and validate the whole batch; nothing is written unless every entry passes.
bool resolveEdits(MidiClip* clip, const QJsonArray& edits, std::vector<Edit>* out, ControlResult* error)
{
	if (edits.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'edits' is empty"));
		return false;
	}
	for (int i = 0; i < edits.size(); ++i)
	{
		QJsonObject entry = edits.at(i).toObject();
		// 'muted' may be a boolean too; it is stored and checked as 0 / 1 like every other field.
		if (entry.value(QStringLiteral("muted")).isBool()) { entry.insert(QStringLiteral("muted"), entry.value(QStringLiteral("muted")).toBool() ? 1 : 0); }
		int index = -1;
		Note* note = resolveNote(clip, entry.value(QStringLiteral("note")).toString(), &index, error);
		if (note == nullptr || !checkEditValues(entry, i, error)) { return false; }
		out->push_back({note, entry});
	}
	return true;
}

QJsonObject previousValues(const Note* note, const QJsonObject& values)
{
	QJsonObject before{{QStringLiteral("note"), noteIdOf(note)}};
	const auto keep = [&](const char* name, double v) {
		if (values.contains(QLatin1String(name))) { before.insert(QLatin1String(name), v); }
	};
	keep("position", note->pos().getTicks());
	keep("key", note->key());
	keep("length", note->length().getTicks());
	keep("velocity", note->getVolume());
	keep("pan", note->getPanning());
	keep("probability", static_cast<double>(note->probability()));
	keep("muted", note->muted() ? 1 : 0);
	keep("release_velocity", note->releaseVelocity());
	return before;
}

void applyEdit(Note* note, const QJsonObject& v)
{
	const auto has = [&v](const char* name) { return v.contains(QLatin1String(name)); };
	const auto num = [&v](const char* name) { return v.value(QLatin1String(name)).toDouble(); };
	if (has("position")) { note->setPos(TimePos(static_cast<tick_t>(num("position")))); }
	if (has("key")) { note->setKey(static_cast<int>(num("key"))); }
	if (has("length")) { note->setLength(TimePos(static_cast<tick_t>(num("length")))); }
	if (has("velocity")) { note->setVolume(static_cast<volume_t>(num("velocity"))); }
	if (has("pan")) { note->setPanning(static_cast<panning_t>(num("pan"))); }
	if (has("probability")) { note->setProbability(static_cast<float>(num("probability"))); }
	if (has("muted")) { note->setMuted(num("muted") != 0); }
	if (has("release_velocity")) { note->setReleaseVelocity(static_cast<int>(num("release_velocity"))); }
}

ControlResult editNotes(const QJsonObject& args)
{
	ControlResult error;
	ClipRef ref;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	std::vector<Edit> edits;
	if (!resolveEdits(clip, args.value(QStringLiteral("edits")).toArray(), &edits, &error)) { return error; }

	QJsonArray inverseEdits;
	QJsonArray ids;
	for (const Edit& edit : edits) { inverseEdits.append(previousValues(edit.note, edit.values)); }
	clip->addJournalCheckPoint();
	for (const Edit& edit : edits)
	{
		applyEdit(edit.note, edit.values);
		ids.append(noteIdOf(edit.note));
	}
	// The follow-up a piano-roll drag makes: re-sort, follow the notes' length, repaint, and
	// mirror the content to the clip's link group in the same step.
	clip->rearrangeAllNotes();
	clip->updateLength();
	clip->dataChanged();
	if (clip->linkId() > 0) { ClipLinks::mirrorContent(clip); }

	QJsonObject result{{QStringLiteral("clip"), clipId(ref.id)},
		{QStringLiteral("edited"), static_cast<int>(edits.size())}, {QStringLiteral("notes"), ids}};
	const QJsonObject inverseArgs{{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("edits"), inverseEdits}};
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(inverseArgs, QStringLiteral("note.edit"), inverseArgs, true, MechanismClipCheckpoint));
	return ControlResult::success(result);
}

} // namespace

void registerNoteEditCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.edit");
	cmd.group = QStringLiteral("note");
	cmd.verb = QStringLiteral("edit");
	cmd.description = QStringLiteral("Change several notes of one clip as ONE edit and ONE undo step - "
		"what the note editor sends for a drag, a multi-note resize or a lane gesture. 'edits' is a "
		"list of {note, position?, key?, length?, velocity?, pan?, probability?, muted?, release_velocity?}: "
		"position and length in ticks, key 0..127, velocity 0..200 (the engine's note volume), pan -100..100, "
		"probability 0..1, muted 0/1 (a muted note stays in the clip, unplayed), release_velocity -1..200 (the "
		"note-off velocity on the volume scale; -1 none, a note-off of 0); a field left out keeps its value. The whole batch is validated first - one bad entry "
		"(an unknown note, a value out of range, an entry naming no field) refuses it and changes "
		"nothing. The note ids do not change. Reversible through the ProjectJournal (one MidiClip "
		"checkpoint); the inverse is note.edit with the previous values.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("edits"), arrayProperty()},
	}, {QStringLiteral("clip"), QStringLiteral("edits")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("edited"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("notes"), arrayProperty()},
	});
	cmd.mutating = true;
	cmd.handler = editNotes;
	registry.registerCommand(cmd);
}

} // namespace lmms
