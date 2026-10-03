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
#include <type_traits>

#include <QJsonArray>

#include "ClipLinks.h"
#include "ControlCommandsNoteShared.h"
#include "ControlEdit.h"
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
	clip->addJournalCheckPoint();
	for (Note* note : notes)
	{
		note->setPos(TimePos(static_cast<tick_t>(std::lround(pivot + (note->pos().getTicks() - pivot) * time))));
		note->setLength(TimePos(std::max<tick_t>(1, static_cast<tick_t>(std::lround(note->length().getTicks() * length)))));
	}
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.scale"), {{QStringLiteral("scaled"), notes.size()}}));
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

//! One entry of note.add_many checked: key 0..127, position >= 0, length >= 1, velocity 0..200.
bool readNewNote(const QJsonObject& entry, int index, Note* out, ControlResult* error)
{
	const int key = entry.value(QStringLiteral("key")).toInt(-1);
	const int position = entry.value(QStringLiteral("position")).toInt(-1);
	const int length = entry.value(QStringLiteral("length")).toInt(0);
	const int velocity = entry.value(QStringLiteral("velocity")).toInt(DefaultVolume);
	if (key < 0 || key >= NumKeys || position < 0 || length < 1 || velocity < MinVolume || velocity > MaxVolume)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("notes[%1] needs key 0..%2, position >= 0, "
			"length >= 1 and velocity %3..%4").arg(index).arg(NumKeys - 1).arg(MinVolume).arg(MaxVolume));
		return false;
	}
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
	clip->addJournalCheckPoint();
	int count = 0;
	for (Note* note : scoped(clip, args.value(QStringLiteral("scope")).toString()))
	{
		if (!note->hasUnquantisedTiming()) { continue; }
		if (freeze) { note->forgetTiming(); }
		else { note->restoreTiming(); }
		++count;
	}
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.restore_timing"),
		{{freeze ? QStringLiteral("frozen") : QStringLiteral("restored"), count}}));
}

//! The `filter` object as a NoteTransform::Filter: {key,velocity,position,length}_{min,max} (an absent
//! bound is open), `scale` "in" / "out" (the scale context's pitch classes) and `invert`.
bool filterFrom(const QJsonObject& json, NoteTransform::Filter* f, ControlResult* error)
{
	const auto range = [&json](const char* field, bool* use, auto* low, auto* high) {
		const QJsonValue lo = json.value(QLatin1String(field) + QStringLiteral("_min"));
		const QJsonValue hi = json.value(QLatin1String(field) + QStringLiteral("_max"));
		*use = lo.isDouble() || hi.isDouble();
		if (lo.isDouble()) { *low = static_cast<std::remove_pointer_t<decltype(low)>>(lo.toInt()); }
		if (hi.isDouble()) { *high = static_cast<std::remove_pointer_t<decltype(high)>>(hi.toInt()); }
	};
	range("key", &f->useKeyRange, &f->minKey, &f->maxKey);
	range("velocity", &f->useVelocityRange, &f->minVelocity, &f->maxVelocity);
	range("position", &f->usePositionRange, &f->minPos, &f->maxPos);
	range("length", &f->useLengthRange, &f->minLength, &f->maxLength);
	f->invert = json.value(QStringLiteral("invert")).toBool();
	const QString scale = json.value(QStringLiteral("scale")).toString();
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
	QJsonArray previous;
	for (Note* note : clip->notes())
	{
		if (note->selected()) { previous.append(noteIdOf(note)); }
		if (!additive) { note->setSelected(false); }
	}
	const NoteVector hits = NoteTransform::select(clip->notes(), filter);
	QJsonArray selected;
	for (Note* note : hits)
	{
		note->setSelected(true);
		selected.append(noteIdOf(note));
	}
	emit clip->dataChanged();
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("matched"), selected.size()},
		{QStringLiteral("selected"), selected}, {QStringLiteral("previous"), previous}});
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
	const NoteVector hits = NoteTransform::select(clip->notes(), filter);
	clip->addJournalCheckPoint();
	const NoteVector doomed = act(hits, action, args.value(QStringLiteral("amount")).toInt());
	for (Note* note : doomed) { clip->removeNote(note); }
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.transform_where"),
		{{QStringLiteral("matched"), static_cast<int>(hits.size())}, {QStringLiteral("removed"), static_cast<int>(doomed.size())}}));
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd;
	cmd.group = QStringLiteral("note");
	cmd.verb = QLatin1String(verb);
	cmd.id = cmd.group + QLatin1Char('.') + cmd.verb;
	cmd.description = description;
	cmd.argsSchema = args;
	cmd.resultSchema = objectProperty();
	cmd.mutating = mutating;
	cmd.handler = handler;
	registry.registerCommand(cmd);
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
}

} // namespace lmms
