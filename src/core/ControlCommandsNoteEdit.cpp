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
 * note's previous values. A batch that would leave every note as it is takes no checkpoint
 * and records no undo step.
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

#include <cmath>
#include <unordered_map>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>

#include "ClipLinks.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteConditions.h"
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
	bool fraction = false;  //!< only probability and velocity_spread take one; the rest are whole numbers, never truncated
};

constexpr Field kFields[] = {
	{"position", 0, MaxSongLength},
	{"key", 0, 127},
	{"length", 1, MaxSongLength},
	{"velocity", 0, 200},
	{"pan", -100, 100},
	{"probability", 0, 1, true},
	{"muted", 0, 1},              // 0.5.0 L07: 1 mutes (the note stays, unplayed)
	{"release_velocity", -1, 200}, // 0.5.0 L07: the note-off velocity, -1 none
	{"velocity_spread", 0, 1, true}, // 0.5.0 L07 (ED-127): the velocity varies by up to this share each playback
	{"repeats", 0, NoteConditions::MaxRepeats},       // ED-133: extra hits inside the note
	{"recur_length", 0, NoteConditions::MaxRecurLength}, // ED-133: the pass pattern's length, 0 every pass
	{"recur_mask", 0, 65535},      // ED-133: bit n - the note plays on pass n of the pattern
	{"articulation", 0, NoteConditions::MaxArticulations}, // ED-121: the track's articulation, 1-based, 0 none
	{"group", 0, 1 << 30},         // ED-069: the note's group, 0 none
	{"slide", 0, 1},               // 0.5.0 FL model: 1 makes it a slide (portamento) note - the properties box
	{"timbre", 0, 127},            // 0.5.0 FL model: the note's MPE timbre (CC74) - the properties box's Mod X
	{"pressure", 0, 127},          // 0.5.0 FL model: the note's MPE pressure - the properties box's Mod Y
	{"pitch", -4800, 4800},        // Q050-099: the note's MPE micro-pitch, in cents
};

//! One resolved edit: the note and the values it is to take.
struct Edit
{
	Note* note = nullptr;
	QJsonObject values;
};

//! Whether @a key is an editable field (or the entry's own note selector).
bool isEditField(const QString& key)
{
	if (key == QLatin1String("note")) { return true; }
	for (const Field& field : kFields)
	{
		if (key == QLatin1String(field.name)) { return true; }
	}
	return false;
}

//! Reject a key that names no editable field: a misspelled field beside a valid one used
//! to be silently dropped while the command reported success.
bool checkEditKeys(const QJsonObject& entry, int index, ControlResult* error)
{
	for (auto it = entry.begin(); it != entry.end(); ++it)
	{
		if (isEditField(it.key())) { continue; }
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("edits[%1].%2 is not an editable note field").arg(index).arg(it.key()));
		return false;
	}
	return true;
}

//! One field's value must be a number inside the field's closed range (a whole number
//! unless the field takes a fraction), never clamped.
bool checkFieldValue(const Field& field, const QJsonValue& value, int index, ControlResult* error)
{
	const double v = value.toDouble();
	if (value.isDouble() && v >= field.minimum && v <= field.maximum && (field.fraction || v == std::floor(v))) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("edits[%1].%2 must be a %5 in [%3, %4]")
			.arg(index).arg(QLatin1String(field.name)).arg(field.minimum).arg(field.maximum)
			.arg(field.fraction ? QStringLiteral("number") : QStringLiteral("whole number")));
	return false;
}

bool checkEditValues(const QJsonObject& entry, int index, ControlResult* error)
{
	if (!checkEditKeys(entry, index, error)) { return false; }
	int fields = 0;
	for (const Field& field : kFields)
	{
		const QJsonValue value = entry.value(QLatin1String(field.name));
		if (value.isUndefined()) { continue; }
		++fields;
		if (!checkFieldValue(field, value, index, error)) { return false; }
	}
	if (fields == 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("edits[%1] changes nothing: name at least one of position, key, length, "
				"velocity, pan, probability, muted, release_velocity, velocity_spread, repeats, recur_length, recur_mask, "
				"articulation, group, slide, timbre, pressure, pitch").arg(index));
		return false;
	}
	return true;
}

//! The note's END must stay inside the supported range too: position and length are each
//! checked on their own, so without their sum a note could be written past MaxSongLength.
//! The addition itself is done in a wider type than tick_t.
bool checkEndPosition(const QJsonObject& entry, const Note* note, int index, ControlResult* error)
{
	const qint64 end = static_cast<qint64>(entry.value(QStringLiteral("position")).toInt(note->pos().getTicks()))
		+ static_cast<qint64>(entry.value(QStringLiteral("length")).toInt(note->length().getTicks()));
	if (end <= static_cast<qint64>(MaxSongLength)) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("edits[%1] ends at %2, past the song limit of %3")
			.arg(index).arg(end).arg(static_cast<qint64>(MaxSongLength)));
	return false;
}

struct FinalNoteShape
{
	qint64 position;
	qint64 length;
	int index;
};

using FinalNoteShapes = std::unordered_map<const Note*, FinalNoteShape>;

//! Accumulate the position and length that all rows for one note will leave behind.
void recordFinalShape(const QJsonObject& entry, const Note* note, int index, FinalNoteShapes* shapes)
{
	FinalNoteShape& shape = shapes->try_emplace(note,
		FinalNoteShape{note->pos().getTicks(), note->length().getTicks(), index}).first->second;
	if (entry.contains(QStringLiteral("position")))
	{
		shape.position = entry.value(QStringLiteral("position")).toInt();
		shape.index = index;
	}
	if (entry.contains(QStringLiteral("length")))
	{
		shape.length = entry.value(QStringLiteral("length")).toInt();
		shape.index = index;
	}
}

//! Check the final shape after combining every row that targets the same note.
bool checkFinalShapes(const FinalNoteShapes& shapes, ControlResult* error)
{
	const FinalNoteShape* firstInvalid = nullptr;
	for (const auto& entry : shapes)
	{
		const FinalNoteShape& shape = entry.second;
		if (shape.position + shape.length > static_cast<qint64>(MaxSongLength)
			&& (firstInvalid == nullptr || shape.index < firstInvalid->index))
		{
			firstInvalid = &shape;
		}
	}
	if (firstInvalid == nullptr) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("edits[%1] ends at %2, past the song limit of %3")
			.arg(firstInvalid->index).arg(firstInvalid->position + firstInvalid->length)
			.arg(static_cast<qint64>(MaxSongLength)));
	return false;
}

//! A pass mask means nothing without a pattern length - the note's own, or one given beside it.
bool checkRecurPattern(const QJsonObject& entry, const Note* note, int index, ControlResult* error)
{
	if (entry.value(QStringLiteral("recur_mask")).toInt() == 0
		|| entry.value(QStringLiteral("recur_length")).toInt(note->conditions().recurLength) != 0)
	{
		return true;
	}
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("edits[%1].recur_mask needs a recur_length").arg(index));
	return false;
}

//! Resolve and validate the whole batch; nothing is written unless every entry passes.
bool resolveEdits(MidiClip* clip, const QJsonArray& edits, std::vector<Edit>* out, ControlResult* error)
{
	if (edits.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'edits' is empty"));
		return false;
	}
	FinalNoteShapes finalShapes;
	for (int i = 0; i < edits.size(); ++i)
	{
		QJsonObject entry = edits.at(i).toObject();
		// 'muted' may be a boolean too; it is stored and checked as 0 / 1 like every other field.
		for (const QString& flag : {QStringLiteral("muted"), QStringLiteral("slide")})
		{
			if (entry.value(flag).isBool()) { entry.insert(flag, entry.value(flag).toBool() ? 1 : 0); }
		}
		int index = -1;
		Note* note = resolveNote(clip, entry.value(QStringLiteral("note")).toString(), &index, error);
		if (note == nullptr || !checkEditValues(entry, i, error)) { return false; }
		// The note's END must stay inside the supported range too: position and length
		// are each checked on their own, so without their sum a note could be written
		// past MaxSongLength. A wider type than tick_t for the addition itself.
		if (!checkEndPosition(entry, note, i, error)) { return false; }
		if (!checkRecurPattern(entry, note, i, error)) { return false; }
		recordFinalShape(entry, note, i, &finalShapes);
		out->push_back({note, entry});
	}
	return checkFinalShapes(finalShapes, error);
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
	keep("velocity_spread", static_cast<double>(note->velocityJitter()));
	keep("slide", note->slide() ? 1 : 0);
	keep("timbre", note->mpeTimbre());
	keep("pressure", note->mpePressure());
	keep("pitch", note->mpePitchCents());
	const NoteConditions& c = note->conditions();
	keep("repeats", c.repeats);
	keep("recur_length", c.recurLength);
	// A new recur_length cuts the mask to its pattern (applyConditions), so the mask comes back with the length.
	if (values.contains(QStringLiteral("recur_mask")) || values.contains(QStringLiteral("recur_length")))
	{
		before.insert(QStringLiteral("recur_mask"), c.recurMask);
	}
	keep("articulation", c.articulation);
	keep("group", c.group);
	return before;
}

//! The conditions @a c has once the W3 fields of one edit (repeats, recurrence, articulation, group)
//! are applied; a mask is cut to its pattern.
NoteConditions conditionsAfter(NoteConditions c, const QJsonObject& v)
{
	const auto take = [&v](const char* name, int* field) {
		if (v.contains(QLatin1String(name))) { *field = v.value(QLatin1String(name)).toInt(); }
	};
	take("repeats", &c.repeats);
	take("recur_length", &c.recurLength);
	take("recur_mask", &c.recurMask);
	take("articulation", &c.articulation);
	take("group", &c.group);
	c.recurMask = c.recurLength > 0 ? c.recurMask & ((1 << c.recurLength) - 1) : 0;
	return c;
}

void applyConditions(Note* note, const QJsonObject& v)
{
	note->setConditions(conditionsAfter(note->conditions(), v));
}

//! The FL model's properties-box fields: slide, and the MPE timbre and pressure (Mod X / Mod Y).
void applyFlModel(Note* note, const QJsonObject& v)
{
	if (v.contains(QStringLiteral("slide"))) { note->setSlide(v.value(QStringLiteral("slide")).toInt() != 0); }
	if (v.contains(QStringLiteral("timbre"))) { note->setMpeTimbre(v.value(QStringLiteral("timbre")).toInt()); }
	if (v.contains(QStringLiteral("pressure"))) { note->setMpePressure(v.value(QStringLiteral("pressure")).toInt()); }
	if (v.contains(QStringLiteral("pitch"))) { note->setMpePitchCents(v.value(QStringLiteral("pitch")).toInt()); }
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
	if (has("velocity_spread")) { note->setVelocityJitter(static_cast<float>(num("velocity_spread"))); }
	applyConditions(note, v);
	applyFlModel(note, v);
}

//! The state of one note as far as a batch can change it: what applyEdit writes, read back from the note.
struct NoteState
{
	tick_t position;
	int key;
	tick_t length;
	volume_t volume;
	panning_t panning;
	float probability;
	bool muted;
	int releaseVelocity;
	float velocityJitter;
	bool slide;
	int timbre;
	int pressure;
	int pitchCents;    //!< Q050-099: note.edit's MPE micro-pitch field
	bool mpeCaptured;  //!< a timbre, pressure or pitch write captures the note's expression, even at 0
	NoteConditions conditions;

	bool operator==(const NoteState&) const = default;
};

NoteState stateOf(const Note* note)
{
	return NoteState{note->pos().getTicks(), note->key(), note->length().getTicks(), note->getVolume(),
		note->getPanning(), note->probability(), note->muted(), note->releaseVelocity(),
		note->velocityJitter(), note->slide(), note->mpeTimbre(), note->mpePressure(),
		note->mpePitchCents(), note->hasMpeExpression(), note->conditions()};
}

//! What applyEdit writes for the core fields of @a v, on a copy. The casts are applyEdit's own, and the
//! values are validated before, so the setters' clamps never move one.
void applyCoreTo(NoteState* s, const QJsonObject& v)
{
	const auto has = [&v](const char* name) { return v.contains(QLatin1String(name)); };
	const auto num = [&v](const char* name) { return v.value(QLatin1String(name)).toDouble(); };
	if (has("position")) { s->position = static_cast<tick_t>(num("position")); }
	if (has("key")) { s->key = static_cast<int>(num("key")); }
	if (has("length")) { s->length = static_cast<tick_t>(num("length")); }
	if (has("velocity")) { s->volume = static_cast<volume_t>(num("velocity")); }
	if (has("pan")) { s->panning = static_cast<panning_t>(num("pan")); }
	if (has("probability")) { s->probability = static_cast<float>(num("probability")); }
	if (has("muted")) { s->muted = num("muted") != 0; }
	if (has("release_velocity")) { s->releaseVelocity = static_cast<int>(num("release_velocity")); }
	if (has("velocity_spread")) { s->velocityJitter = static_cast<float>(num("velocity_spread")); }
}

//! What applyFlModel writes for the FL model's fields of @a v, on a copy.
void applyFlModelTo(NoteState* s, const QJsonObject& v)
{
	if (v.contains(QStringLiteral("slide"))) { s->slide = v.value(QStringLiteral("slide")).toInt() != 0; }
	if (v.contains(QStringLiteral("timbre")))
	{
		s->timbre = v.value(QStringLiteral("timbre")).toInt();
		s->mpeCaptured = true;
	}
	if (v.contains(QStringLiteral("pressure")))
	{
		s->pressure = v.value(QStringLiteral("pressure")).toInt();
		s->mpeCaptured = true;
	}
	if (v.contains(QStringLiteral("pitch")))
	{
		s->pitchCents = v.value(QStringLiteral("pitch")).toInt();
		s->mpeCaptured = true;
	}
}

//! Whether the batch leaves some note as it was. Each note's state is carried through the batch's entries in
//! order, so a value set and then set back inside one batch counts as no change, as it does on the note.
bool editChangesAnyNote(const std::vector<Edit>& edits)
{
	std::unordered_map<const Note*, NoteState> after;
	for (const Edit& edit : edits)
	{
		auto it = after.find(edit.note);
		if (it == after.end()) { it = after.emplace(edit.note, stateOf(edit.note)).first; }
		applyCoreTo(&it->second, edit.values);
		it->second.conditions = conditionsAfter(it->second.conditions, edit.values);
		applyFlModelTo(&it->second, edit.values);
	}
	for (const auto& [note, state] : after)
	{
		if (!(state == stateOf(note))) { return true; }
	}
	return false;
}

ControlResult editNotes(const QJsonObject& args)
{
	ControlResult error;
	ClipRef ref;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	std::vector<Edit> edits;
	if (!resolveEdits(clip, args.value(QStringLiteral("edits")).toArray(), &edits, &error)) { return error; }
	// BUG 12 (cluster new-p15-22): a batch that leaves every note as it is takes no checkpoint, so it leaves no undo
	// step for control.undo to consume. Its reply reports that: edited 0, no notes, and no transaction.
	if (!editChangesAnyNote(edits))
	{
		return ControlResult::success(QJsonObject{{QStringLiteral("clip"), clipId(ref.id)},
			{QStringLiteral("edited"), 0}, {QStringLiteral("notes"), QJsonArray{}}});
	}

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
		"note-off velocity on the volume scale; -1 none, a note-off of 0), velocity_spread 0..1 (the velocity varies "
		"by up to that share each playback), repeats 0..16 (extra hits evenly inside the note), recur_length 0..16 and "
		"recur_mask (over that many passes of the loop the note plays on the passes whose bit is set: length 2, mask 1 "
		"is the first of every two), articulation (the track's articulation, track.articulations, 1-based; 0 none), "
		"group (0 none; note.group makes one), slide 0/1 (a slide note), timbre and pressure 0..127 (the note's MPE "
		"timbre and pressure - the note properties box's Mod X and Mod Y), pitch -4800..4800 cents (its MPE "
		"micro-pitch); a field left out keeps its value. The whole batch is validated first - one bad entry "
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
