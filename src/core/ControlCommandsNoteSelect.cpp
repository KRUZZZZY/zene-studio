/*
 * ControlCommandsNoteSelect.cpp - the note.select command: the piano roll's
 *                                 note selection (SPEC A11-A16), split out of
 *                                 ControlCommandsNotes.cpp
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

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include "ClipLinks.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "MidiClip.h"
#include "Note.h"
#include "Song.h"
#include "TimelineSpeech.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

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
		speech::announceNoteSelection(clip, wanted.size());
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerNoteSelectCommands(ControlRegistry& registry)
{
	registerNoteSelect(registry);
}

} // namespace lmms
