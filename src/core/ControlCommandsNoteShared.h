/*
 * ControlCommandsNoteShared.h - what the note.* edit commands share: scope, row, finish
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
#ifndef LMMS_CONTROL_COMMANDS_NOTE_SHARED_H
#define LMMS_CONTROL_COMMANDS_NOTE_SHARED_H

#include <QJsonObject>
#include <QVector>

#include "ClipLinks.h"
#include "ControlEdit.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MidiClip.h"
#include "Note.h"
#include "Song.h"

namespace lmms::control::notes
{

//! The notes @a scope names in @a clip: the selection ("selection") or every note.
inline QVector<Note*> scoped(const MidiClip* clip, const QString& scope)
{
	QVector<Note*> out;
	for (Note* note : clip->notes())
	{
		if (scope != QStringLiteral("selection") || note->selected()) { out.append(note); }
	}
	return out;
}

//! One note as note.list reports it.
inline QJsonObject noteJson(const Note* note)
{
	return {{QStringLiteral("note"), noteIdOf(note)}, {QStringLiteral("key"), note->key()},
		{QStringLiteral("position"), note->pos().getTicks()}, {QStringLiteral("length"), note->length().getTicks()},
		{QStringLiteral("velocity"), static_cast<int>(note->getVolume())}, {QStringLiteral("pan"), static_cast<int>(note->getPanning())},
		{QStringLiteral("probability"), static_cast<double>(note->probability())}, {QStringLiteral("selected"), note->selected()},
		{QStringLiteral("muted"), note->muted()}, {QStringLiteral("release_velocity"), note->releaseVelocity()},
		{QStringLiteral("quantised"), note->hasUnquantisedTiming()}};
}

//! After an edit of @a clip (checkpointed first): sort, size, mirror to linked copies, and the reply.
inline QJsonObject finish(const ClipRef& ref, MidiClip* clip, const QString& command, QJsonObject result)
{
	clip->rearrangeAllNotes();
	clip->updateLength();
	emit clip->dataChanged();
	if (clip->linkId() > 0) { ClipLinks::mirrorContent(clip); }
	Engine::getSong()->setModified();
	result.insert(QStringLiteral("clip"), clipId(ref.id));
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("clip"), clipId(ref.id)}}, command, {},
		true, QStringLiteral("MidiClip journal checkpoint: undo restores every note")));
	return result;
}

} // namespace lmms::control::notes

namespace lmms
{
class ControlRegistry;
//! note.remove_many, note.slice, note.copy / cut / paste / duplicate (ControlCommandsNoteClipboard.cpp); registered
//! with the note shape group (registerNoteShapeCommands), so the registration list needs no line of its own.
void registerNoteClipboardCommands(ControlRegistry& registry);
} // namespace lmms

#endif // LMMS_CONTROL_COMMANDS_NOTE_SHARED_H
