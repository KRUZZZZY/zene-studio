/*
 * ArrangementEdits.cpp - clip edits over a time range: split, shift, crop, the range itself
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

#include "ArrangementEdits.h"

#include <algorithm>

#include "Clip.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

namespace arrangement
{

namespace
{

//! clip.delete's recipe: detach from the track, then delete.
void removeClip(Track* track, Clip* clip)
{
	track->removeClip(clip);
	delete clip;
}

} // namespace

Clip* splitClipAt(Clip* clip, tick_t position)
{
	const tick_t start = clip->startPosition().getTicks();
	const tick_t end = clip->endPosition().getTicks();
	if (position <= start || position >= end) { return nullptr; }
	// ClipView::splitClip's recipe: clone, then resize both halves; the right half keeps its
	// content where it was by moving its start offset back by the left half's length.
	Track* track = clip->getTrack();
	track->saveJournallingState(false);
	Clip* right = clip->clone();
	clip->changeLength(TimePos(position - start));
	clip->setAutoResize(false);
	right->movePosition(TimePos(position));
	right->changeLength(TimePos(end - position));
	right->setStartTimeOffset(clip->startTimeOffset() - clip->length());
	right->setAutoResize(false);
	track->restoreJournallingState();
	return right;
}

int splitTrackAt(Track* track, tick_t position)
{
	int split = 0;
	const auto clips = track->getClips();  // a copy: splitting adds clips
	for (Clip* clip : clips)
	{
		if (splitClipAt(clip, position) != nullptr) { ++split; }
	}
	return split;
}

QVector<Clip*> clipsWithin(Track* track, tick_t start, tick_t end)
{
	QVector<Clip*> out;
	for (Clip* clip : track->getClips())
	{
		if (clip->startPosition().getTicks() >= start && clip->endPosition().getTicks() <= end) { out.append(clip); }
	}
	return out;
}

int shiftClipsFrom(Track* track, tick_t from, tick_t delta)
{
	int moved = 0;
	for (Clip* clip : track->getClips())
	{
		const tick_t start = clip->startPosition().getTicks();
		if (start < from) { continue; }
		clip->movePosition(TimePos(std::max<tick_t>(0, start + delta)));
		++moved;
	}
	return moved;
}

int deleteRange(Track* track, tick_t start, tick_t end, bool ripple)
{
	splitTrackAt(track, start);
	splitTrackAt(track, end);
	const QVector<Clip*> inside = clipsWithin(track, start, end);
	for (Clip* clip : inside) { removeClip(track, clip); }
	if (ripple) { shiftClipsFrom(track, end, start - end); }
	return static_cast<int>(inside.size());
}

int cropToRange(Track* track, tick_t start, tick_t end)
{
	splitTrackAt(track, start);
	splitTrackAt(track, end);
	int removed = 0;
	const auto clips = track->getClips();
	for (Clip* clip : clips)
	{
		if (clip->startPosition().getTicks() >= start && clip->endPosition().getTicks() <= end) { continue; }
		removeClip(track, clip);
		++removed;
	}
	return removed;
}

} // namespace arrangement

ArrangerRange& ArrangerRange::instance()
{
	static ArrangerRange* range = new ArrangerRange();
	return *range;
}

QVector<Track*> ArrangerRange::tracks() const
{
	QVector<Track*> out;
	for (Track* track : Engine::getSong()->tracks())
	{
		if (m_allTracks || m_trackIds.contains(track->id())) { out.append(track); }
	}
	return out;
}

void ArrangerRange::set(tick_t start, tick_t end, const QVector<int>& trackIds, bool allTracks)
{
	m_start = std::max<tick_t>(0, std::min(start, end));
	m_end = std::max<tick_t>(0, std::max(start, end));
	m_trackIds = trackIds;
	m_allTracks = allTracks || trackIds.isEmpty();
	emit changed();
}

void ArrangerRange::clear()
{
	m_start = m_end = 0;
	m_trackIds.clear();
	m_allTracks = true;
	emit changed();
}

} // namespace lmms
