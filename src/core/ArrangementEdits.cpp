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
#include <cstdlib>

#include "Clip.h"
#include "ClipEdits.h"
#include "ConfigManager.h"
#include "ControlReversibility.h"
#include "ProjectMarkers.h"
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

Modes modes()
{
	ConfigManager* config = ConfigManager::inst();
	Modes out;
	out.ripple = static_cast<Ripple>(std::clamp(config->value(QStringLiteral("arrangement"), QStringLiteral("ripple"), QStringLiteral("0")).toInt(), 0, 2));
	out.overlap = static_cast<Overlap>(std::clamp(config->value(QStringLiteral("arrangement"), QStringLiteral("overlap"), QStringLiteral("0")).toInt(), 0, 2));
	out.snapToEdges = config->value(QStringLiteral("arrangement"), QStringLiteral("snapedges"), QStringLiteral("1")).toInt() != 0;
	return out;
}

void setModes(const Modes& modes)
{
	ConfigManager* config = ConfigManager::inst();
	config->setValue(QStringLiteral("arrangement"), QStringLiteral("ripple"), QString::number(static_cast<int>(modes.ripple)));
	config->setValue(QStringLiteral("arrangement"), QStringLiteral("overlap"), QString::number(static_cast<int>(modes.overlap)));
	config->setValue(QStringLiteral("arrangement"), QStringLiteral("snapedges"), modes.snapToEdges ? QStringLiteral("1") : QStringLiteral("0"));
}

QVector<Track*> rippleTracks(Track* track, Ripple mode)
{
	if (mode == Ripple::Off) { return {}; }
	if (mode == Ripple::Track) { return {track}; }
	QVector<Track*> all;
	for (Track* each : Engine::getSong()->tracks()) { all.append(each); }
	return all;
}

void checkpointForDelete(Track* track, Ripple mode)
{
	QVector<JournallingObject*> touched;
	for (Track* each : mode == Ripple::All ? rippleTracks(track, mode) : QVector<Track*>{track}) { touched.append(each); }
	control::addUndoStep(touched);
}

void afterUserMove(Clip* moved)
{
	const Overlap mode = modes().overlap;
	if (mode == Overlap::Free || moved == nullptr || moved->getTrack() == nullptr) { return; }
	moved->getTrack()->addJournalCheckPoint();
	applyOverlap(moved, mode);
}

int rippleAfterDelete(Track* track, tick_t start, tick_t length, Ripple mode)
{
	int moved = 0;
	for (Track* each : rippleTracks(track, mode)) { moved += shiftClipsFrom(each, start + length, -length); }
	return moved;
}

namespace
{

//! Crossfade @a earlier into @a later over their overlap: fade-out on one, fade-in on the other.
void crossfade(Clip* earlier, Clip* later)
{
	const tick_t overlap = earlier->endPosition().getTicks() - later->startPosition().getTicks();
	if (overlap <= 0) { return; }
	ClipEdits out = earlier->clipEdits();
	out.fadeOutTicks = overlap;
	out.fadeOutShape = FadeShape::EqualPower;
	earlier->setClipEdits(out);
	ClipEdits in = later->clipEdits();
	in.fadeInTicks = overlap;
	in.fadeInShape = FadeShape::EqualPower;
	later->setClipEdits(in);
}

} // namespace

int applyOverlap(Clip* moved, Overlap mode)
{
	if (mode == Overlap::Free) { return 0; }
	Track* track = moved->getTrack();
	const tick_t start = moved->startPosition().getTicks();
	const tick_t end = moved->endPosition().getTicks();
	int changed = 0;
	const auto others = track->getClips();
	for (Clip* other : others)
	{
		if (other == moved || other->endPosition().getTicks() <= start || other->startPosition().getTicks() >= end) { continue; }
		if (mode == Overlap::Crossfade)
		{
			if (other->startPosition().getTicks() < start) { crossfade(other, moved); } else { crossfade(moved, other); }
			++changed;
			continue;
		}
		// Trim: cut the other clip at the moved clip's edges and drop what lies under it.
		Clip* right = splitClipAt(other, end);
		Clip* inside = splitClipAt(other, start);
		Clip* under = inside != nullptr ? inside : other;
		if (under->startPosition().getTicks() >= start && under->endPosition().getTicks() <= end) { removeClip(track, under); }
		Q_UNUSED(right)
		++changed;
	}
	return changed;
}

tick_t snapToEdges(tick_t proposed, tick_t length, const Clip* moving, tick_t tolerance)
{
	tick_t best = proposed;
	tick_t bestDistance = tolerance + 1;
	const auto consider = [&](tick_t edge) {
		for (const tick_t candidate : {edge, edge - length})  // the start lands there, or the end does
		{
			const tick_t distance = std::abs(candidate - proposed);
			if (candidate >= 0 && distance < bestDistance) { best = candidate; bestDistance = distance; }
		}
	};
	for (Track* track : Engine::getSong()->tracks())
	{
		for (const Clip* clip : track->getClips())
		{
			if (clip == moving) { continue; }
			consider(clip->startPosition().getTicks());
			consider(clip->endPosition().getTicks());
		}
	}
	for (const Marker& marker : ProjectMarkers::instance().markers()) { consider(marker.tick); }
	return best;
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
