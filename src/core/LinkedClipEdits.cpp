/*
 * LinkedClipEdits.cpp - shared content edits for linked clips
 *                       (include/LinkedClipEdits.h).
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

#include "LinkedClipEdits.h"

#include "Clip.h"
#include "ClipLinks.h"
#include "Engine.h"
#include "MidiClip.h"
#include "Note.h"
#include "ProjectJournal.h"
#include "Track.h"

namespace lmms
{

namespace ClipLinks
{

void checkpointGroupTracks(const Clip* source)
{
	QVector<JournallingObject*> tracks;
	auto addTrack = [&tracks](Track* track) {
		if (track != nullptr && !tracks.contains(track)) { tracks.append(track); }
	};
	addTrack(source->getTrack());
	for (Clip* member : contentMembersOf(source)) { addTrack(member->getTrack()); }
	if (ProjectJournal* journal = Engine::projectJournal(); journal != nullptr && !tracks.isEmpty())
	{
		journal->addJournalCheckPoint(tracks);
	}
}

int adoptTrimmedContent(MidiClip* trimmed)
{
	int written = 0;
	for (MidiClip* peer : contentMembersOf(trimmed))
	{
		const bool autoResize = peer->getAutoResize();
		peer->setAutoResize(false);
		peer->clearNotes();
		for (const Note* note : trimmed->notes())
		{
			if (note != nullptr) { peer->addNote(*note, false); }
		}
		peer->setAutoResize(autoResize);
		peer->dataChanged();
		++written;
	}
	return written;
}

} // namespace ClipLinks

} // namespace lmms
