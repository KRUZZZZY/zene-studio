/*
 * ControlNoteRecord.cpp - a live recording take's held notes and its one undo step
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

#include "ControlNoteRecord.h"

#include <QHash>

namespace lmms::control
{

namespace
{

//! One clip's take: whether its checkpoint is taken, and the notes its held keys opened.
struct Take
{
	bool open = false;
	QVector<int> keys;               //!< in the order they went down, so a take closes in order
	QHash<int, QString> notes;       //!< key -> the note id that key is holding
};

/*! clip id -> its take.
 *
 *  A function-local static, so the map is built on the first recorded note and never during static
 *  initialisation. It is touched only from the control thread (the command handlers, the note
 *  editor and the transport's own `stopped` signal), which is why it needs no lock; see the header
 *  for why it is not project state.
 */
QHash<QString, Take>& takes()
{
	static QHash<QString, Take> store;
	return store;
}

} // namespace

bool recordTakeOpen(const QString& clipId)
{
	return takes().value(clipId).open;
}

void beginRecordTake(const QString& clipId)
{
	if (clipId.isEmpty()) { return; }
	takes()[clipId].open = true;
}

void endRecordTake(const QString& clipId)
{
	const auto it = takes().find(clipId);
	if (it == takes().end()) { return; }
	it->open = false;
	it->keys.clear();
	it->notes.clear();
}

void holdRecordNote(const QString& clipId, int key, const QString& noteId)
{
	if (clipId.isEmpty()) { return; }
	Take& take = takes()[clipId];
	take.open = true;
	if (!take.notes.contains(key)) { take.keys.append(key); }
	take.notes.insert(key, noteId);
}

QString heldRecordNote(const QString& clipId, int key)
{
	return takes().value(clipId).notes.value(key);
}

void releaseRecordNote(const QString& clipId, int key)
{
	const auto it = takes().find(clipId);
	if (it == takes().end()) { return; }
	it->notes.remove(key);
	it->keys.removeAll(key);
}

QVector<int> heldRecordKeys(const QString& clipId)
{
	return takes().value(clipId).keys;
}

void forgetRecordClip(const QString& clipId)
{
	takes().remove(clipId);
}

void endAllRecordTakes()
{
	for (auto it = takes().begin(); it != takes().end(); ++it)
	{
		it->open = false;
		it->keys.clear();
		it->notes.clear();
	}
}

} // namespace lmms::control
