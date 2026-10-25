/*
 * ControlCommandsNoteClipboard.cpp - note.remove_many, note.slice and the note clipboard
 *                                    (note.copy, note.cut, note.paste, note.duplicate)
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

/*! 0.5.0 (owner decision 2026-10-03: the note editor follows FL Studio's piano roll). The verbs a
 *  stroke or a key of that model needs as ONE edit: a right-drag deletes every note it crosses
 *  (note.remove_many), the slice tool cuts every note its line crosses (note.slice), and the
 *  clipboard keys copy, cut, paste and duplicate (Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+B). Each edit is one
 *  MidiClip journal checkpoint, so one undo step. The clipboard is this process's own: it lives
 *  for the life of the instance, outside the project, like the selection, and a paste works into
 *  any MIDI clip. */

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include <QJsonArray>

#include "ControlCommandDef.h"
#include "ControlCommandsNoteShared.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "MidiClip.h"
#include "Note.h"

namespace lmms
{

using namespace control;
using namespace control::notes;

namespace
{

//! The note clipboard: copies of the notes at the positions they were copied from.
std::vector<Note>& clipboard()
{
	static std::vector<Note> notes;
	return notes;
}

ControlResult refused(ControlErrorKind kind, const QString& message) { return ControlResult::failure(kind, message); }

//! The notes 'notes' names in @a clip, each once; false (with @a error) for an empty list or an id not in the clip.
bool resolveIds(MidiClip* clip, const QJsonArray& ids, std::vector<Note*>* out, ControlResult* error)
{
	if (ids.isEmpty())
	{
		*error = refused(ControlErrorKind::InvalidArgs, QStringLiteral("'notes' is empty"));
		return false;
	}
	for (const QJsonValue& id : ids)
	{
		int index = -1;
		Note* note = resolveNote(clip, id.toString(), &index, error);
		if (note == nullptr) { return false; }
		out->push_back(note);
	}
	std::sort(out->begin(), out->end());
	out->erase(std::unique(out->begin(), out->end()), out->end());
	return true;
}

//! @a notes' ids, read before anything deletes them.
QJsonArray idsOf(const std::vector<Note*>& notes)
{
	QJsonArray ids;
	for (const Note* note : notes) { ids.append(noteIdOf(note)); }
	return ids;
}

void removeAll(MidiClip* clip, const std::vector<Note*>& notes)
{
	for (Note* note : notes) { clip->removeNote(note); }
}

ControlResult removeMany(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	std::vector<Note*> notes;
	if (!resolveIds(clip, args.value(QStringLiteral("notes")).toArray(), &notes, &error)) { return error; }
	const QJsonArray ids = idsOf(notes);
	clip->addJournalCheckPoint();
	removeAll(clip, notes);
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.remove_many"),
		{{QStringLiteral("removed"), ids}, {QStringLiteral("count"), ids.size()},
		{QStringLiteral("note_count"), static_cast<int>(clip->notes().size())}}));
}

//! The notes to slice: 'notes' when given, else every note of the clip - only those @a at falls strictly inside.
bool sliceTargets(MidiClip* clip, const QJsonObject& args, tick_t at, std::vector<Note*>* out, ControlResult* error)
{
	std::vector<Note*> named;
	if (args.contains(QStringLiteral("notes")))
	{
		if (!resolveIds(clip, args.value(QStringLiteral("notes")).toArray(), &named, error)) { return false; }
	}
	else { named.assign(clip->notes().begin(), clip->notes().end()); }
	for (Note* note : named)
	{
		if (note->pos().getTicks() < at && note->endPos().getTicks() > at) { out->push_back(note); }
	}
	return true;
}

ControlResult sliceNotes(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	const auto at = static_cast<tick_t>(args.value(QStringLiteral("at")).toInt());
	std::vector<Note*> targets;
	if (!sliceTargets(clip, args, at, &targets, &error)) { return error; }
	if (targets.empty())  // the line crosses no note: nothing changes, so no undo step (note.restore_timing's rule)
	{
		return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("sliced"), 0},
			{QStringLiteral("notes"), QJsonArray()}});
	}
	clip->addJournalCheckPoint();
	QJsonArray made;
	for (Note* note : targets)
	{
		// The right half is a copy of the note from the cut on (velocity, pan, slide and the rest kept); the
		// left half is the note itself, shortened, so its id survives.
		Note half = *note;
		half.forgetTiming();  // as a ratchet's repeat: it has no timing of its own from before a quantise
		half.setSelected(false);
		half.setPos(TimePos(at));
		half.setLength(TimePos(note->endPos().getTicks() - at));
		note->setLength(TimePos(at - note->pos().getTicks()));
		made.append(noteIdOf(clip->addNote(half, false)));
	}
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.slice"),
		{{QStringLiteral("sliced"), static_cast<int>(targets.size())}, {QStringLiteral("notes"), made}}));
}

//! The notes 'scope' names (selection or clip), or a not_found refusal when there are none.
bool scopedNotes(MidiClip* clip, const QJsonObject& args, std::vector<Note*>* out, ControlResult* error)
{
	const QVector<Note*> notes = scoped(clip, args.value(QStringLiteral("scope")).toString());
	out->assign(notes.begin(), notes.end());
	if (!out->empty()) { return true; }
	*error = refused(ControlErrorKind::NotFound, QStringLiteral("no notes in that scope"));
	return false;
}

void fillClipboard(const std::vector<Note*>& notes)
{
	std::vector<Note>& board = clipboard();
	board.clear();
	for (const Note* note : notes)
	{
		board.push_back(*note);
		board.back().setSelected(false);
	}
}

//! Earliest start and latest end of @a notes (non-empty).
std::pair<std::int64_t, std::int64_t> spanOf(const std::vector<Note*>& notes)
{
	const Note* firstNote = notes.front();
	std::int64_t first = firstNote->pos().getTicks();
	std::int64_t last = first + firstNote->length().getTicks();
	for (const Note* note : notes)
	{
		const std::int64_t start = note->pos().getTicks();
		first = std::min(first, start);
		last = std::max(last, start + note->length().getTicks());
	}
	return {first, last};
}

ControlResult copyNotes(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	std::vector<Note*> notes;
	if (clip == nullptr || !scopedNotes(clip, args, &notes, &error)) { return error; }
	fillClipboard(notes);
	const auto [first, last] = spanOf(notes);
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("copied"), static_cast<int>(notes.size())},
		{QStringLiteral("start"), static_cast<qint64>(first)}, {QStringLiteral("end"), static_cast<qint64>(last)}});
}

ControlResult cutNotes(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	std::vector<Note*> notes;
	if (clip == nullptr || !scopedNotes(clip, args, &notes, &error)) { return error; }
	fillClipboard(notes);
	const QJsonArray ids = idsOf(notes);
	clip->addJournalCheckPoint();
	removeAll(clip, notes);
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.cut"), {{QStringLiteral("cut"), ids.size()},
		{QStringLiteral("removed"), ids}}));
}

//! Add @a copies to @a clip moved by @a offset ticks, selected, the clip's other notes deselected; the new ids.
QJsonArray addSelected(MidiClip* clip, const std::vector<Note>& copies, std::int64_t offset)
{
	for (Note* note : clip->notes()) { note->setSelected(false); }
	QJsonArray ids;
	for (Note copy : copies)
	{
		const std::int64_t position = static_cast<std::int64_t>(copy.pos().getTicks()) + offset;
		copy.setPos(TimePos(static_cast<tick_t>(position)));
		copy.setSelected(true);
		ids.append(noteIdOf(clip->addNote(copy, false)));
	}
	return ids;
}

//! Whether every copy moved by @a offset starts at 0 or later and ends inside the longest song.
bool fits(const std::vector<Note>& copies, std::int64_t offset)
{
	return std::all_of(copies.begin(), copies.end(), [offset](const Note& note) {
		const std::int64_t start = static_cast<std::int64_t>(note.pos().getTicks()) + offset;
		const std::int64_t end = start + note.length().getTicks();
		return start >= 0 && start <= std::numeric_limits<tick_t>::max() && end <= MaxSongLength;
	});
}

//! The paste's shift: 'position' (where the earliest note lands) when given, else 0 - where they were copied from.
std::int64_t pasteOffset(const QJsonObject& args)
{
	if (!args.contains(QStringLiteral("position"))) { return 0; }
	std::int64_t first = clipboard().front().pos().getTicks();
	for (const Note& note : clipboard()) { first = std::min(first, static_cast<std::int64_t>(note.pos().getTicks())); }
	return static_cast<std::int64_t>(args.value(QStringLiteral("position")).toInt()) - first;
}

ControlResult pasteNotes(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	if (clipboard().empty())
	{
		return refused(ControlErrorKind::NotFound, QStringLiteral("the note clipboard is empty: note.copy or note.cut fills it"));
	}
	const std::int64_t offset = pasteOffset(args);
	if (!fits(clipboard(), offset))
	{
		return refused(ControlErrorKind::InvalidArgs, QStringLiteral("that position puts notes outside 0..%1 ticks").arg(MaxSongLength));
	}
	clip->addJournalCheckPoint();
	const QJsonArray ids = addSelected(clip, clipboard(), offset);
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.paste"), {{QStringLiteral("pasted"), ids.size()},
		{QStringLiteral("notes"), ids}}));
}

//! The duplicate's shift: the scope's span from its earliest start to its latest end, both rounded out to 'grid'.
std::int64_t duplicateOffset(const std::vector<Note*>& notes, int grid)
{
	const auto [first, last] = spanOf(notes);
	const std::int64_t step = std::max(1, grid);
	const std::int64_t from = first / step * step;
	const std::int64_t to = (last + step - 1) / step * step;
	return std::max<std::int64_t>(1, to - from);
}

ControlResult duplicateNotes(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	std::vector<Note*> notes;
	if (clip == nullptr || !scopedNotes(clip, args, &notes, &error)) { return error; }
	const std::int64_t offset = duplicateOffset(notes, args.value(QStringLiteral("grid")).toInt(1));
	std::vector<Note> copies;
	for (const Note* note : notes) { copies.push_back(*note); }
	if (!fits(copies, offset))
	{
		return refused(ControlErrorKind::InvalidArgs, QStringLiteral("the copies would end past the longest song (%1 ticks)").arg(MaxSongLength));
	}
	clip->addJournalCheckPoint();
	const QJsonArray ids = addSelected(clip, copies, offset);
	return ControlResult::success(finish(ref, clip, QStringLiteral("note.duplicate"), {{QStringLiteral("duplicated"), ids.size()},
		{QStringLiteral("offset"), static_cast<qint64>(offset)}, {QStringLiteral("notes"), ids}}));
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QStringLiteral("note.") + QLatin1String(verb), description, args, mutating, handler);
}

} // namespace

void registerNoteClipboardCommands(ControlRegistry& registry)
{
	const QJsonObject clip = stringProperty();
	const QJsonObject scope = enumProperty({QStringLiteral("clip"), QStringLiteral("selection")});
	const QString step = QStringLiteral(" One undo step (MidiClip checkpoint).");
	add(registry, "remove_many", QStringLiteral("Delete several notes of one clip as ONE edit: 'notes' is a list of note ids - "
		"what the note editor's right-drag, Delete tool stroke and Delete key send. The whole list is checked first; one "
		"unknown id refuses it and removes nothing (an id named twice is removed once).") + step,
		objectSchema({{QStringLiteral("clip"), clip}, {QStringLiteral("notes"), arrayProperty()}},
		{QStringLiteral("clip"), QStringLiteral("notes")}), true, removeMany);
	add(registry, "slice", QStringLiteral("Cut notes in two at tick 'at' (inside the clip): every note in 'notes' (ids; default "
		"every note of the clip) that 'at' falls strictly inside keeps its id and ends at 'at', and a copy of it starts there - "
		"the slice tool's line. A line that crosses no note changes nothing (no undo step).") + step,
		objectSchema({{QStringLiteral("clip"), clip}, {QStringLiteral("at"), integerProperty(1, MaxSongLength)},
		{QStringLiteral("notes"), arrayProperty()}}, {QStringLiteral("clip"), QStringLiteral("at")}), true, sliceNotes);
	add(registry, "copy", QStringLiteral("Copy the notes in 'scope' (selection, or the whole clip) to the note clipboard, "
		"replacing what it held. The clipboard is this instance's, not the project's: nothing is written and there is no "
		"undo step."), objectSchema({{QStringLiteral("clip"), clip}, {QStringLiteral("scope"), scope}},
		{QStringLiteral("clip")}), false, copyNotes);
	add(registry, "cut", QStringLiteral("Copy the notes in 'scope' (selection, or the whole clip) to the note clipboard and "
		"delete them.") + step, objectSchema({{QStringLiteral("clip"), clip}, {QStringLiteral("scope"), scope}},
		{QStringLiteral("clip")}), true, cutNotes);
	add(registry, "paste", QStringLiteral("Add the note clipboard's notes to a clip (any MIDI clip): where they were copied from, "
		"or with the earliest at tick 'position'. The pasted notes become the selection.") + step,
		objectSchema({{QStringLiteral("clip"), clip}, {QStringLiteral("position"), integerProperty(0, MaxSongLength)}},
		{QStringLiteral("clip")}), true, pasteNotes);
	add(registry, "duplicate", QStringLiteral("Copy the notes in 'scope' (selection, or the whole clip) to just after "
		"themselves: shifted by their span, from the earliest start to the latest end, each rounded out to 'grid' ticks "
		"(default 1, the exact span). The copies become the selection, so a second duplicate continues the run.") + step,
		objectSchema({{QStringLiteral("clip"), clip}, {QStringLiteral("scope"), scope},
		{QStringLiteral("grid"), integerProperty(1, MaxSongLength)}}, {QStringLiteral("clip")}), true, duplicateNotes);
}

} // namespace lmms
