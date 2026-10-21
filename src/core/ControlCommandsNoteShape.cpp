/*
 * ControlCommandsNoteShape.cpp - note.scale, note.ratchet, note.list, note.transform_where
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

/*! 0.5.0 L07: ED scale note lengths and timing (note.scale), step ratchets (note.ratchet - a note
 *  split into equal repeats), the MIDI event list's rows (note.list) and the condition-based
 *  transform (note.transform_where - filter on key / velocity / position / length, then select,
 *  remove, transpose or change velocity). Each edit is ONE MidiClip journal checkpoint. */

#include <algorithm>
#include <cmath>
#include <limits>

#include <QJsonArray>
#include <QStringList>

#include "ClipLinks.h"
#include "ControlCommandsNoteShared.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlNoteShared.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MidiClip.h"
#include "ControlScaleShared.h"
#include "Note.h"
#include "NoteTransform.h"
#include "Song.h"

namespace lmms
{

using namespace control;
using namespace control::notes;

namespace
{

constexpr int MaxRatchet = 16;

//! Whether scaling @a notes about @a pivot keeps each rounded note end inside the longest song;
//! bounds the doubles first so a huge factor cannot overflow the rounding.
bool scaledFits(const QVector<Note*>& notes, tick_t pivot, double time, double length)
{
	const auto limit = static_cast<double>(MaxSongLength);
	return std::all_of(notes.begin(), notes.end(), [=](const Note* note) {
		const double scaledPosition = pivot + (note->pos().getTicks() - pivot) * time;
		const double scaledLength = note->length().getTicks() * length;
		if (!(scaledPosition <= limit) || !(scaledLength <= limit) || !(scaledPosition + scaledLength <= limit))
		{
			return false;
		}
		const tick_t roundedPosition = static_cast<tick_t>(std::lround(scaledPosition));
		const tick_t roundedLength = std::max<tick_t>(1, static_cast<tick_t>(std::lround(scaledLength)));
		return roundedPosition + roundedLength <= MaxSongLength;
	});
}

//! The position and the length every note gets when the clip is scaled by @a time
//! and @a length about @a pivot - and whether that changes any note at all.
struct ScaledShape
{
	QVector<tick_t> positions;
	QVector<tick_t> lengths;
	bool changes = false;
};

//! The scaled shape of @a notes, computed BEFORE anything is changed: the undo
//! checkpoint has to be taken before the edit, and the edit has to know whether it
//! changes anything at all (a scale by 1 moves nothing, and a step that undoes
//! nothing would shadow the edit under it - the rule control::checkpointNoteEdit
//! holds, the same one note.transpose follows).
ScaledShape scaledShape(const QVector<Note*>& notes, tick_t pivot, double time, double length)
{
	ScaledShape shape;
	for (const Note* note : notes)
	{
		const tick_t position = static_cast<tick_t>(std::lround(pivot + (note->pos().getTicks() - pivot) * time));
		const tick_t scaledLength = std::max<tick_t>(1, static_cast<tick_t>(std::lround(note->length().getTicks() * length)));
		shape.positions.append(position);
		shape.lengths.append(scaledLength);
		if (position != note->pos().getTicks() || scaledLength != note->length().getTicks()) { shape.changes = true; }
	}
	return shape;
}

ControlResult scaleNotes(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	const QVector<Note*> notes = scoped(clip, args.value(QStringLiteral("scope")).toString());
	if (notes.isEmpty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no notes in that scope")); }
	const double time = args.value(QStringLiteral("time_factor")).toDouble(1.0);
	const double length = args.value(QStringLiteral("length_factor")).toDouble(time);
	if (!(time > 0.0) || !(length > 0.0))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("time_factor and length_factor must be > 0"));
	}
	tick_t pivot = notes.first()->pos().getTicks();
	for (const Note* note : notes) { pivot = std::min<tick_t>(pivot, note->pos().getTicks()); }
	if (!scaledFits(notes, pivot, time, length))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("those factors move notes past the longest song (%1 ticks)")
			.arg(MaxSongLength));
	}
	const ScaledShape shape = scaledShape(notes, pivot, time, length);
	control::checkpointNoteEdit(clip, shape.changes);
	for (int index = 0; index < notes.size(); ++index)
	{
		notes.at(index)->setPos(TimePos(shape.positions.at(index)));
		notes.at(index)->setLength(TimePos(shape.lengths.at(index)));
	}
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.scale"),
		{{QStringLiteral("scaled"), notes.size()}}, shape.changes));
}

ControlResult ratchetNotes(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	const int count = args.value(QStringLiteral("count")).toInt(2);
	QVector<Note*> notes;
	for (Note* note : scoped(clip, args.value(QStringLiteral("scope")).toString()))
	{
		if (note->length().getTicks() >= count) { notes.append(note); }  // one tick per repeat at least
	}
	if (notes.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no note in that scope is long enough to split"));
	}
	clip->addJournalCheckPoint();
	int made = 0;
	for (Note* note : notes)
	{
		// Equal repeats; the last one takes the remainder, so the note's span is kept.
		const tick_t length = note->length().getTicks();
		const tick_t step = length / count;
		Note repeat = *note;
		repeat.forgetTiming();  // a repeat has no timing of its own from before a quantise to go back to
		note->setLength(TimePos(step));
		for (int i = 1; i < count; ++i)
		{
			repeat.setPos(TimePos(note->pos().getTicks() + i * step));
			repeat.setLength(TimePos(i == count - 1 ? length - i * step : step));
			clip->addNote(repeat, false);
			++made;
		}
	}
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.ratchet"), {{QStringLiteral("added"), made}}));
}

ControlResult listNotes(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	QJsonArray notes;
	for (const Note* note : clip->notes()) { notes.append(noteJson(note)); }
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("notes"), notes},
		{QStringLiteral("count"), notes.size()}});
}

//! The new note's END, which is the sum note.edit's checkEndPosition() also sums in
//! qint64 (src/core/ControlCommandsNoteEdit.cpp): position and length are each a legal
//! int on their own, so their sum is where the signed 32-bit tick range can be left.
bool checkNewEndPosition(int position, int length, int index, ControlResult* error)
{
	const qint64 end = static_cast<qint64>(position) + static_cast<qint64>(length);
	if (end <= static_cast<qint64>(MaxSongLength)) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("notes[%1] ends at %2, past the song limit of %3")
			.arg(index).arg(end).arg(static_cast<qint64>(MaxSongLength)));
	return false;
}

//! One entry of note.add_many checked: key 0..127, position >= 0, length >= 1, velocity 0..200.
bool readNewNote(const QJsonObject& entry, int index, Note* out, ControlResult* error)
{
	const int key = entry.value(QStringLiteral("key")).toInt(-1);
	const int position = entry.value(QStringLiteral("position")).toInt(-1);
	const int length = entry.value(QStringLiteral("length")).toInt(0);
	// Present but not a whole number reads as -1 and is refused, not taken for the default.
	const int velocity = entry.contains(QStringLiteral("velocity")) ? entry.value(QStringLiteral("velocity")).toInt(-1) : DefaultVolume;
	if (key < 0 || key >= NumKeys || position < 0 || length < 1 || velocity < MinVolume || velocity > MaxVolume)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("notes[%1] needs key 0..%2, position >= 0, "
			"length >= 1 and velocity %3..%4").arg(index).arg(NumKeys - 1).arg(MinVolume).arg(MaxVolume));
		return false;
	}
	if (!checkNewEndPosition(position, length, index, error)) { return false; }
	*out = Note(TimePos(length), TimePos(position), key, static_cast<volume_t>(velocity));
	return true;
}

ControlResult addNotes(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	const QJsonArray entries = args.value(QStringLiteral("notes")).toArray();
	if (entries.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'notes' is empty")); }
	QVector<Note> notes(entries.size());
	for (int i = 0; i < entries.size(); ++i)
	{
		if (!readNewNote(entries.at(i).toObject(), i, &notes[i], &error)) { return error; }
	}
	clip->addJournalCheckPoint();
	QJsonArray ids;
	for (const Note& note : notes) { ids.append(noteIdOf(clip->addNote(note, false))); }
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.add_many"),
		{{QStringLiteral("added"), ids.size()}, {QStringLiteral("notes"), ids}}));
}

ControlResult restoreTiming(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	const bool freeze = args.value(QStringLiteral("freeze")).toBool();
	const QVector<Note*> notes = scoped(clip, args.value(QStringLiteral("scope")).toString());
	if (std::none_of(notes.begin(), notes.end(), [](const Note* note) { return note->hasUnquantisedTiming(); }))
	{
		// No note keeps an original timing: nothing changes, so no undo step and the project stays as it was.
		return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {freeze ? QStringLiteral("frozen") : QStringLiteral("restored"), 0}});
	}
	clip->addJournalCheckPoint();
	int count = 0;
	for (Note* note : notes)
	{
		if (!note->hasUnquantisedTiming()) { continue; }
		if (freeze) { note->forgetTiming(); }
		else { note->restoreTiming(); }
		++count;
	}
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.restore_timing"),
		{{freeze ? QStringLiteral("frozen") : QStringLiteral("restored"), count}}));
}

//! One filter bound into @a out, held to what its type carries; false for a value that is not a whole number
//! (QJsonValue::toInt would read 60.5 as 0, an open bound where a tight one was meant) - a string "60" too.
template <typename T>
bool readBound(const QJsonValue& value, T* out)
{
	if (value.isUndefined() || value.isNull()) { return true; }  // absent: the bound is open
	if (!value.isDouble()) { return false; }
	const double v = value.toDouble();
	if (v != std::floor(v)) { return false; }
	*out = static_cast<T>(std::clamp(v, static_cast<double>(std::numeric_limits<T>::lowest()), static_cast<double>(std::numeric_limits<T>::max())));
	return true;
}

//! The filter's {key,velocity,position,length}_{min,max} bounds (an absent bound is open); false for a fractional one.
bool readRanges(const QJsonObject& json, NoteTransform::Filter* f)
{
	const auto range = [&json](const char* field, bool* use, auto* low, auto* high) {
		const QJsonValue lo = json.value(QLatin1String(field) + QStringLiteral("_min"));
		const QJsonValue hi = json.value(QLatin1String(field) + QStringLiteral("_max"));
		*use = lo.isDouble() || hi.isDouble();
		return readBound(lo, low) && readBound(hi, high);
	};
	return range("key", &f->useKeyRange, &f->minKey, &f->maxKey) && range("velocity", &f->useVelocityRange, &f->minVelocity, &f->maxVelocity)
		&& range("position", &f->usePositionRange, &f->minPos, &f->maxPos) && range("length", &f->useLengthRange, &f->minLength, &f->maxLength);
}

//! filter.invert, held to being a BOOLEAN: toBool() used to read a value of the wrong
//! type as "do not invert", so a filter the caller meant to narrow silently stopped
//! narrowing at all.
bool readInvert(const QJsonValue& value, NoteTransform::Filter* f, ControlResult* error)
{
	if (value.isUndefined() || value.isNull()) { return true; }  // absent: no inversion
	if (!value.isBool())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("filter.invert is true or false, not a value of another type"));
		return false;
	}
	f->invert = value.toBool();
	return true;
}

//! filter.scale as NoteTransform::Filter's scale channel: the string "in" / "out" (the scale
//! context's pitch classes). A value of the wrong type used to become an EMPTY string through
//! toString(), which the code below reads as "no scale filter" - so a malformed filter matched
//! every note, and 'action: remove' then removed all of them.
bool readScale(const QJsonValue& value, NoteTransform::Filter* f, ControlResult* error)
{
	if (value.isUndefined() || value.isNull()) { return true; }  // absent: no scale channel
	if (!value.isString())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("filter.scale is 'in' or 'out' (a string), not a value of another type"));
		return false;
	}
	const QString scale = value.toString();
	if (scale.isEmpty()) { return true; }
	ResolvedScale resolved;
	if (!resolveScale({}, &resolved, error)) { return false; }
	if ((scale != QStringLiteral("in") && scale != QStringLiteral("out")) || resolved.classes.empty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("filter.scale is 'in' or 'out' and needs a scale set (scale.set)"));
		return false;
	}
	f->scaleMatch = scale == QStringLiteral("in") ? NoteTransform::ScaleMatch::InScale : NoteTransform::ScaleMatch::OutOfScale;
	f->scaleDegrees = resolved.classes;
	return true;
}

//! Filter keys are limited to the documented bounds, scale, and invert fields.
bool readFilterKeys(const QJsonObject& json, ControlResult* error)
{
	static const QStringList recognized{
		QStringLiteral("key_min"), QStringLiteral("key_max"), QStringLiteral("velocity_min"), QStringLiteral("velocity_max"),
		QStringLiteral("position_min"), QStringLiteral("position_max"), QStringLiteral("length_min"), QStringLiteral("length_max"),
		QStringLiteral("scale"), QStringLiteral("invert")};
	for (auto it = json.constBegin(); it != json.constEnd(); ++it)
	{
		const QString& key = it.key();
		if (recognized.contains(key)) { continue; }
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("unknown filter field '%1'").arg(key));
		return false;
	}
	return true;
}

bool filterFrom(const QJsonObject& json, NoteTransform::Filter* f, ControlResult* error)
{
	if (!readFilterKeys(json, error)) { return false; }
	if (!readRanges(json, f))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a filter bound is a whole number (a JSON number)"));
		return false;
	}
	return readInvert(json.value(QStringLiteral("invert")), f, error)
		&& readScale(json.value(QStringLiteral("scale")), f, error);
}

//! Whether any of @a hits carries a velocity other than @a target.
bool anyVolumeDiffers(const NoteVector& hits, int target)
{
	return std::any_of(hits.cbegin(), hits.cend(), [target](const Note* note)
	{
		return note != nullptr && static_cast<int>(note->getVolume()) != target;
	});
}

//! Whether any of @a hits carries a mute flag other than @a muted.
bool anyMuteDiffers(const NoteVector& hits, bool muted)
{
	return std::any_of(hits.cbegin(), hits.cend(), [muted](const Note* note)
	{
		return note != nullptr && note->muted() != muted;
	});
}

/*! Whether @a action (with @a amount) would change any of @a hits - the question
 *  the undo checkpoint asks BEFORE the edit, because the checkpoint captures the state
 *  the edit is about to replace and so has to be taken first.
 *
 *  A transpose of zero semitones, a velocity offset of zero, a velocity the note
 *  already has and a mute flag the note already carries all change nothing, and an
 *  edit that changes nothing takes no step (control::checkpointNoteEdit). The two
 *  engine-backed actions are counted by the engine's own rule - the same rule
 *  act() applies - so the answer and the edit cannot drift apart.
 */
bool actionChanges(const NoteVector& hits, const QString& action, int amount)
{
	if (action == QStringLiteral("remove")) { return !hits.empty(); }
	if (action == QStringLiteral("transpose")) { return NoteTransform::countTransposed(hits, amount) > 0; }
	if (action == QStringLiteral("velocity_offset")) { return NoteTransform::countOffsetVelocity(hits, amount) > 0; }
	if (action == QStringLiteral("velocity_set"))
	{
		return anyVolumeDiffers(hits, std::clamp(amount, static_cast<int>(MinVolume), static_cast<int>(MaxVolume)));
	}
	if (action == QStringLiteral("mute") || action == QStringLiteral("unmute"))
	{
		return anyMuteDiffers(hits, action == QStringLiteral("mute"));
	}
	return false;
}

//! Apply @a action (with @a amount) to the matched @a hits; the notes to remove are returned.
NoteVector act(const NoteVector& hits, const QString& action, int amount)
{
	if (action == QStringLiteral("remove")) { return hits; }
	if (action == QStringLiteral("transpose")) { NoteTransform::transpose(hits, amount); }
	else if (action == QStringLiteral("velocity_offset")) { NoteTransform::offsetVelocity(hits, amount); }
	else if (action == QStringLiteral("mute") || action == QStringLiteral("unmute"))
	{
		for (Note* note : hits) { note->setMuted(action == QStringLiteral("mute")); }
	}
	else if (action == QStringLiteral("velocity_set"))
	{
		for (Note* note : hits) { note->setVolume(static_cast<volume_t>(std::clamp(amount, int{MinVolume}, int{MaxVolume}))); }
	}
	return {};
}

//! Keep filter-hit positions separate from their group-expanded selection.
QVector<int> matchedPositions(const NoteVector& noteList, const NoteVector& hits)
{
	QVector<int> positions;
	for (int index = 0; index < static_cast<int>(noteList.size()); ++index)
	{
		Note* note = noteList.at(index);
		if (std::find(hits.cbegin(), hits.cend(), note) == hits.cend()) { continue; }
		positions.append(index);
	}
	return positions;
}

//! Include each matched note's group in the resulting positions.
void withGroupMates(const NoteVector& noteList, QVector<int>* positions)
{
	QVector<int> groups;
	for (int index : *positions)
	{
		const int group = noteList.at(index)->conditions().group;
		if (group > 0 && !groups.contains(group)) { groups.append(group); }
	}
	if (groups.isEmpty()) { return; }
	for (int index = 0; index < static_cast<int>(noteList.size()); ++index)
	{
		if (groups.contains(noteList.at(index)->conditions().group) && !positions->contains(index)) { positions->append(index); }
	}
}

//! Selection is view state (note.select's rule): no checkpoint, the previous selection in the reply.
ControlResult selectWhere(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	NoteTransform::Filter filter;
	if (!filterFrom(args.value(QStringLiteral("filter")).toObject(), &filter, &error)) { return error; }
	const bool additive = args.value(QStringLiteral("additive")).toBool();
	const NoteVector& noteList = clip->notes();
	QJsonArray previous;
	QVector<int> wanted;
	for (int index = 0; index < static_cast<int>(noteList.size()); ++index)
	{
		Note* note = noteList.at(index);
		if (note->selected())
		{
			previous.append(noteIdOf(note));
			// 'additive' KEEPS what was selected, so those notes are part of the
			// resulting selection the store has to hold as well.
			if (additive) { wanted.append(index); }
		}
		if (!additive) { note->setSelected(false); }
	}
	const NoteVector hits = NoteTransform::select(noteList, filter);
	QVector<int> matched = matchedPositions(noteList, hits);
	const int matchedCount = static_cast<int>(matched.size());
	withGroupMates(noteList, &matched);
	std::sort(matched.begin(), matched.end());
	QJsonArray selected;
	for (int index : matched)
	{
		selected.append(noteIdOf(noteList.at(index)));
		noteList.at(index)->setSelected(true);
		if (!wanted.contains(index)) { wanted.append(index); }
	}
	// The flags above are the piano-roll half. The control selection store keeps the
	// selected notes' ids (control::selectNotes), so its selection follows those
	// notes if the clip's list is later re-sorted or a note is removed.
	control::selectNotes(clipId(ref.id), *clip, wanted);
	emit clip->dataChanged();
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("matched"), matchedCount},
		{QStringLiteral("selected"), selected}, {QStringLiteral("previous"), previous}});
}

//! The actions that use 'amount': without it, velocity_set would set every match to velocity 0.
bool needsAmount(const QString& action)
{
	return action == QLatin1String("transpose") || action == QLatin1String("velocity_set") || action == QLatin1String("velocity_offset");
}

//! 'amount' as the int the actions work in. registerNoteShapeCommands() declares it as an
//! unbounded integer (integerProperty(), src/core/ControlVocabulary.cpp:85), so a value past
//! the schema integer is still a whole JSON number and reaches the handler; toInt() would
//! read it as 0, and a transpose or velocity offset of 0 is a success that moved nothing.
//! A value the handler cannot hold is refused instead of silently becoming a no-op.
bool readAmount(const QJsonObject& args, int* out, ControlResult* error)
{
	const QJsonValue value = args.value(QStringLiteral("amount"));
	const double amount = value.toDouble();
	if (!value.isDouble() || amount != std::floor(amount)
		|| amount < static_cast<double>(std::numeric_limits<int>::lowest())
		|| amount > static_cast<double>(std::numeric_limits<int>::max()))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'amount' is a whole number the handler can hold (%1..%2), not %3")
				.arg(std::numeric_limits<int>::lowest()).arg(std::numeric_limits<int>::max())
				.arg(value.isDouble() ? QString::number(amount) : QStringLiteral("a value of another type")));
		return false;
	}
	*out = static_cast<int>(amount);
	return true;
}

ControlResult transformWhere(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	NoteTransform::Filter filter;
	if (!filterFrom(args.value(QStringLiteral("filter")).toObject(), &filter, &error)) { return error; }
	const QString action = args.value(QStringLiteral("action")).toString();
	if (needsAmount(action) && !args.contains(QStringLiteral("amount")))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1' needs an 'amount'").arg(action));
	}
	int amount = 0;
	if (args.contains(QStringLiteral("amount")) && !readAmount(args, &amount, &error)) { return error; }
	const NoteVector hits = NoteTransform::select(clip->notes(), filter);
	if (hits.empty())  // nothing to change: no undo step, the project not modified
	{
		return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("matched"), 0}, {QStringLiteral("removed"), 0}});
	}
	// The action may change nothing (a transpose of zero, a velocity the notes
	// already have, a mute flag they already carry) - then it takes no undo step
	// and leaves the project clean.
	const bool changes = actionChanges(hits, action, amount);
	control::checkpointNoteEdit(clip, changes);
	const NoteVector doomed = act(hits, action, amount);
	for (Note* note : doomed) { clip->removeNote(note); }
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.transform_where"),
		{{QStringLiteral("matched"), static_cast<int>(hits.size())}, {QStringLiteral("removed"), static_cast<int>(doomed.size())}},
		changes));
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QStringLiteral("note.") + QLatin1String(verb), description, args, mutating, handler);
}

} // namespace

void registerNoteShapeCommands(ControlRegistry& registry)
{
	const QJsonObject scope = enumProperty({QStringLiteral("clip"), QStringLiteral("selection")});
	const QString step = QStringLiteral(" One undo step (MidiClip checkpoint).");
	add(registry, "scale", QStringLiteral("Scale the notes in 'scope' (clip or selection) in time: positions by "
		"'time_factor' about the first of them, lengths by 'length_factor' (default the same factor) - x2 doubles, "
		"x0.5 halves.") + step, objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("scope"), scope},
		{QStringLiteral("time_factor"), numberProperty()}, {QStringLiteral("length_factor"), numberProperty()}},
		{QStringLiteral("clip")}), true, scaleNotes);
	add(registry, "ratchet", QStringLiteral("Split each note in 'scope' into 'count' (2..16) equal repeats of the same key "
		"and velocity - a ratchet; the last repeat takes any remainder. Refused not_found when no note in scope is "
		"'count' ticks long.") + step, objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("scope"), scope},
		{QStringLiteral("count"), integerProperty(2, MaxRatchet)}}, {QStringLiteral("clip")}), true, ratchetNotes);
	add(registry, "add_many", QStringLiteral("Add several notes to a clip as ONE edit: 'notes' is a list of {key 0..127, "
		"position (ticks), length (ticks, >= 1), velocity 0..200 (default 100)} - what the note editor's paint tool sends. "
		"The whole list is checked first; one bad entry refuses it and adds nothing.") + step,
		objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("notes"), arrayProperty()}},
		{QStringLiteral("clip"), QStringLiteral("notes")}), true, addNotes);
	add(registry, "select_where", QStringLiteral("Select the notes 'filter' matches (the filter note.transform_where "
		"takes) - and deselect the others unless 'additive'. View state, as note.select: no undo step; the previous "
		"selection is in the reply."), objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("filter"),
		objectProperty()}, {QStringLiteral("additive"), booleanProperty()}}, {QStringLiteral("clip")}), false, selectWhere);
	add(registry, "restore_timing", QStringLiteral("Put back the timing the notes in 'scope' had before their first "
		"quantise (every quantise keeps it, and it survives other edits and a save) - or, with 'freeze', keep the "
		"quantised timing and forget the original.") + step, objectSchema({{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("scope"), scope}, {QStringLiteral("freeze"), booleanProperty()}}, {QStringLiteral("clip")}), true, restoreTiming);
	add(registry, "list", QStringLiteral("Every note of a clip as a row: id, key, position, length, velocity (0..200), "
		"pan, probability, selected, muted, release_velocity (-1 none), quantised (an original timing is kept) - the MIDI "
		"event list."), objectSchema({{QStringLiteral("clip"), stringProperty()}},
		{QStringLiteral("clip")}), false, listNotes);
	add(registry, "transform_where", QStringLiteral("Find and change: the notes whose key / velocity / position / length lie "
		"inside 'filter's bounds ({key_min, key_max, velocity_min, ..., length_max}; absent bounds pass; 'scale' in / out "
		"of the scale context; 'invert') are removed, transposed by 'amount', or given velocity 'amount' (velocity_set) or +'amount' "
		"(velocity_offset), or muted / unmuted.") + step, objectSchema({{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("filter"), objectProperty()}, {QStringLiteral("action"), enumProperty({
			QStringLiteral("remove"), QStringLiteral("transpose"), QStringLiteral("velocity_set"), QStringLiteral("velocity_offset"),
			QStringLiteral("mute"), QStringLiteral("unmute")})},
		{QStringLiteral("amount"), integerProperty()}}, {QStringLiteral("clip"), QStringLiteral("action")}), true, transformWhere);
	registerNoteClipboardCommands(registry);
}

} // namespace lmms
