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

#include <QPair>

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

namespace
{

/*! Why \p peer cannot take an edge edit of (\p startDelta, \p endDelta), or empty. A peer is refused when the
 *  edit would empty it or push it before the song start, and when it would extend the peer before its own content
 *  (a start offset above 0, which the song editor never allows) while the edited clip stays inside its content:
 *  the peers of a group may differ in offset, and the edit must not leave one of them in a state its own drag
 *  would have refused. The edited clip's own offset is not constrained here (clip.trim accepts any). */
QString peerRefusal(const Clip* peer, tick_t startDelta, tick_t endDelta, tick_t sourceOffsetAfter)
{
	const tick_t start = peer->startPosition().getTicks() + startDelta;
	const tick_t end = peer->endPosition().getTicks() + endDelta;
	if (end <= start || start < 0)
	{
		return QStringLiteral("the edit would leave the linked clip at %1..%2 (empty or before the "
			"song start), so the whole group's edit is refused and nothing moved").arg(start).arg(end);
	}
	const tick_t offsetBefore = peer->startTimeOffset().getTicks();
	const tick_t offsetAfter = offsetBefore - startDelta;
	if (offsetAfter > 0 && offsetAfter > offsetBefore && sourceOffsetAfter <= 0)
	{
		return QStringLiteral("the edit would extend a linked clip %1 ticks before its own content, so the "
			"whole group's edit is refused and nothing moved").arg(offsetAfter);
	}
	return QString();
}

} // namespace

namespace
{

//! The clips linked to \p edited and not in it, each once, with the edited clip they follow.
QVector<QPair<Clip*, const Clip*>> followers(const QVector<Clip*>& edited)
{
	QVector<QPair<Clip*, const Clip*>> out;
	QVector<Clip*> seen;
	for (const Clip* source : edited)
	{
		for (Clip* peer : peersOf(source))
		{
			if (edited.contains(peer) || seen.contains(peer)) { continue; }
			seen.append(peer);
			out.append({peer, source});
		}
	}
	return out;
}

} // namespace

QString planEdgeEditFor(const QVector<Clip*>& edited, tick_t startDelta, tick_t endDelta)
{
	for (const auto& [peer, source] : followers(edited))
	{
		const tick_t sourceOffsetAfter = source->startTimeOffset().getTicks() - startDelta;
		const QString refusal = peerRefusal(peer, startDelta, endDelta, sourceOffsetAfter);
		if (!refusal.isEmpty()) { return refusal; }
	}
	return QString();
}

QString planEdgeEdit(const Clip* source, tick_t startDelta, tick_t endDelta)
{
	return planEdgeEditFor({const_cast<Clip*>(source)}, startDelta, endDelta);
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

int applyEdgeEditFor(const QVector<Clip*>& edited, tick_t startDelta, tick_t endDelta, bool checkpoint)
{
	const auto follow = followers(edited);
	if (checkpoint)
	{
		QVector<JournallingObject*> peers;
		for (const auto& entry : follow) { peers.append(entry.first); }
		checkpointObjects(peers);
	}
	int count = 0;
	for (const auto& [peer, source] : follow)
	{
		// The edited clip has already been edited: its offset is the one after.
		if (!peerRefusal(peer, startDelta, endDelta, source->startTimeOffset().getTicks()).isEmpty()) { continue; }
		const tick_t start = peer->startPosition().getTicks() + startDelta;
		const tick_t end = peer->endPosition().getTicks() + endDelta;
		const tick_t offset = peer->startTimeOffset().getTicks() - startDelta;  // clip.trim's head-trim rule
		peer->movePosition(TimePos(start));
		peer->changeLength(TimePos(end - start));
		peer->setStartTimeOffset(TimePos(offset));
		peer->setAutoResize(false);
		++count;
	}
	return count;
}

int applyEdgeEdit(Clip* source, tick_t startDelta, tick_t endDelta, bool checkpoint)
{
	return applyEdgeEditFor({source}, startDelta, endDelta, checkpoint);
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
		Clip* right = arrangement::splitClipAlone(member, member->startPosition().getTicks() + relative);
		if (right == nullptr) { continue; }
		right->setLinkId(report.rightGroup);
		report.lefts.append(member);
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
