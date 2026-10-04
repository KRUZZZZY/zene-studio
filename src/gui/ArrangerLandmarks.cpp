/*
 * ArrangerLandmarks.cpp - the song's landmarks (sections, markers, the loop) as the navigator names them
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

#include "ArrangerLandmarks.h"

#include <algorithm>
#include <cstdlib>

#include <QObject>

#include "ArrangementSections.h"
#include "Engine.h"
#include "MusicalTime.h"
#include "ProjectMarkers.h"
#include "Song.h"

namespace lmms::gui::landmarks
{

namespace
{

tick_t songEnd()
{
	return static_cast<tick_t>(std::max<bar_t>(1, Engine::getSong()->length())) * TimePos::ticksPerBar();
}

//! The first marker after @a tick, else the song's end (a bar on at least): where a marker's span ends.
tick_t nextMarkerAfter(tick_t tick)
{
	for (const Marker& marker : ProjectMarkers::instance().markers())  // ordered by tick
	{
		if (marker.tick > tick) { return marker.tick; }
	}
	return std::max(songEnd(), tick + TimePos::ticksPerBar());
}

//! The marker nearest @a tick within @a slack, spanning up to the next marker or the song's end.
Landmark markerAt(tick_t tick, tick_t slack)
{
	Landmark found;
	tick_t nearest = slack + 1;
	for (const Marker& marker : ProjectMarkers::instance().markers())
	{
		const tick_t distance = std::abs(marker.tick - tick);
		if (distance >= nearest) { continue; }
		nearest = distance;
		found = {Landmark::Kind::Marker, marker.name.isEmpty() ? QObject::tr("Marker") : marker.name, marker.tick, 0};
	}
	if (found.kind == Landmark::Kind::Marker) { found.end = nextMarkerAfter(found.start); }
	return found;
}

} // namespace

Landmark at(tick_t tick, tick_t slack)
{
	if (Landmark marker = markerAt(tick, slack); marker.kind != Landmark::Kind::None) { return marker; }
	if (const ArrangementSection* section = ArrangementSections::instance().at(tick))
	{
		return {Landmark::Kind::Section, section->name, section->start, section->end};
	}
	const Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
	const tick_t begin = timeline.loopBegin().getTicks();
	const tick_t end = timeline.loopEnd().getTicks();
	if (timeline.loopEnabled() && tick >= begin && tick < end) { return {Landmark::Kind::Loop, QObject::tr("Loop"), begin, end}; }
	return {};
}

QString describe(const Landmark& landmark)
{
	const QString from = musicaltime::counterText(landmark.start);
	const QString to = musicaltime::counterText(landmark.end);
	switch (landmark.kind)
	{
	case Landmark::Kind::Section: return QObject::tr("Section %1, %2 to %3").arg(landmark.name, from, to);
	case Landmark::Kind::Marker: return QObject::tr("Marker %1 at %2").arg(landmark.name, from);
	case Landmark::Kind::Loop: return QObject::tr("Loop, %1 to %2").arg(from, to);
	case Landmark::Kind::None: break;
	}
	return QString();
}

} // namespace lmms::gui::landmarks
