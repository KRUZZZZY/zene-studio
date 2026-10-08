/*
 * LinkedClipEdits.cpp - trim and split of a linked clip apply to the whole group
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

#include "ArrangementEdits.h"
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

namespace
{

//! One journal step over \p objects (nothing when there are none).
void checkpointObjects(const QVector<JournallingObject*>& objects)
{
	ProjectJournal* journal = Engine::projectJournal();
	if (journal != nullptr && !objects.isEmpty()) { journal->addJournalCheckPoint(objects); }
}

//! \p track once in \p tracks.
void addTrackOnce(QVector<JournallingObject*>* tracks, Track* track)
{
	if (track != nullptr && !tracks->contains(track)) { tracks->append(track); }
}

} // namespace

QVector<Clip*> peersOf(const Clip* clip)
{
	QVector<Clip*> peers = groupOf(clip);
	peers.removeAll(const_cast<Clip*>(clip));
	return peers;
}

QString planEdgeEdit(const Clip* source, tick_t startDelta, tick_t endDelta)
{
	for (const Clip* peer : peersOf(source))
	{
		const tick_t start = peer->startPosition().getTicks() + startDelta;
		const tick_t end = peer->endPosition().getTicks() + endDelta;
		if (end > start && start >= 0) { continue; }
		return QStringLiteral("the trim would leave the linked clip at %1..%2 (empty or before the "
			"song start), so the whole group's trim is refused and nothing moved").arg(start).arg(end);
	}
	return QString();
}

void checkpointPeers(const Clip* source)
{
	QVector<JournallingObject*> peers;
	for (Clip* peer : peersOf(source)) { peers.append(peer); }
	checkpointObjects(peers);
}

void checkpointGroupTracks(const Clip* source)
{
	QVector<JournallingObject*> tracks;
	addTrackOnce(&tracks, source->getTrack());
	for (Clip* peer : peersOf(source)) { addTrackOnce(&tracks, peer->getTrack()); }
	checkpointObjects(tracks);
}

int applyEdgeEdit(Clip* source, tick_t startDelta, tick_t endDelta, bool checkpoint)
{
	if (checkpoint) { checkpointPeers(source); }
	int edited = 0;
	for (Clip* peer : peersOf(source))
	{
		const tick_t start = peer->startPosition().getTicks() + startDelta;
		const tick_t end = peer->endPosition().getTicks() + endDelta;
		if (end <= start || start < 0) { continue; }
		const tick_t offset = peer->startTimeOffset().getTicks() - startDelta;  // clip.trim's head-trim rule
		peer->movePosition(TimePos(start));
		peer->changeLength(TimePos(end - start));
		peer->setStartTimeOffset(TimePos(offset));
		peer->setAutoResize(false);
		++edited;
	}
	return edited;
}

namespace
{

//! The members of \p source's group (or \p source alone) that are long enough to be cut \p relative
//! ticks from their start; their tracks, each once, go to \p tracks.
QVector<Clip*> cuttableMembers(Clip* source, tick_t relative, QVector<JournallingObject*>* tracks)
{
	QVector<Clip*> members = groupOf(source);
	if (members.isEmpty()) { members.append(source); }
	QVector<Clip*> cut;
	for (Clip* member : members)
	{
		if (relative >= member->length().getTicks()) { continue; }  // too short: left whole
		cut.append(member);
		addTrackOnce(tracks, member->getTrack());
	}
	return cut;
}

} // namespace

GroupSplit splitGroupAt(Clip* source, tick_t position)
{
	GroupSplit report;
	const tick_t relative = position - source->startPosition().getTicks();
	if (relative <= 0 || position >= source->endPosition().getTicks()) { return report; }

	QVector<JournallingObject*> tracks;
	const QVector<Clip*> cut = cuttableMembers(source, relative, &tracks);
	checkpointObjects(tracks);

	// A group of one cut is not a group: the right half stays unlinked.
	const bool linked = source->linkId() > 0 && cut.size() > 1;
	report.rightGroup = linked ? allocateGroupId() : 0;
	for (Clip* member : cut)
	{
		Clip* right = arrangement::splitClipAt(member, member->startPosition().getTicks() + relative);
		if (right == nullptr) { continue; }
		right->setLinkId(report.rightGroup);
		++report.members;
		if (member == source) { report.right = right; }
	}
	return report;
}

int adoptTrimmedContent(MidiClip* trimmed, tick_t offsetShift)
{
	int written = 0;
	for (MidiClip* peer : contentMembersOf(trimmed))
	{
		peer->clearNotes();
		for (const Note* note : trimmed->notes())
		{
			if (note != nullptr) { peer->addNote(*note, false); }
		}
		peer->setStartTimeOffset(TimePos(peer->startTimeOffset().getTicks() + offsetShift));
		peer->dataChanged();
		++written;
	}
	return written;
}

} // namespace ClipLinks

} // namespace lmms
