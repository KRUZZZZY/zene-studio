/*
 * ArrangementEdits.h - clip edits over a time range: split, shift, crop, the range itself
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

#ifndef LMMS_ARRANGEMENT_EDITS_H
#define LMMS_ARRANGEMENT_EDITS_H

#include <QObject>
#include <QVector>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class Clip;
class Track;

/*! The arrangement's edits that work over time rather than one clip - the base of range
 *  editing (ED-011/012), ripple (ED-043) and overlap modes (ED-044). None of these journals:
 *  the caller checkpoints the tracks it touches first (one undo step per command). */
namespace arrangement
{

//! Split @a clip at @a position (strictly inside it): the left part stays, the right part is a
//! new clip, returned. nullptr when @a position is not inside. The GUI's knife and clip.split.
LMMS_EXPORT Clip* splitClipAt(Clip* clip, tick_t position);

//! Split every clip of @a track that crosses @a position; how many were split.
LMMS_EXPORT int splitTrackAt(Track* track, tick_t position);

//! Every clip of @a track that lies entirely inside [@a start, @a end).
LMMS_EXPORT QVector<Clip*> clipsWithin(Track* track, tick_t start, tick_t end);

//! Move every clip of @a track that starts at or after @a from by @a delta ticks (ripple).
LMMS_EXPORT int shiftClipsFrom(Track* track, tick_t from, tick_t delta);

//! Remove [@a start, @a end) from @a track: split at both edges, delete what is inside, and with
//! @a ripple close the gap. How many clips were removed.
LMMS_EXPORT int deleteRange(Track* track, tick_t start, tick_t end, bool ripple);

//! Keep only [@a start, @a end) of @a track: split at both edges and delete what is outside.
LMMS_EXPORT int cropToRange(Track* track, tick_t start, tick_t end);

} // namespace arrangement

/*! ED-011: the arranger's time-range selection - a start and an end on the timeline over a set
 *  of tracks (or all of them). Interface state the range.* commands act on and the arranger
 *  draws; not saved with the project. */
class LMMS_EXPORT ArrangerRange : public QObject
{
	Q_OBJECT
public:
	static ArrangerRange& instance();

	bool active() const { return m_end > m_start; }
	tick_t start() const { return m_start; }
	tick_t end() const { return m_end; }
	bool allTracks() const { return m_allTracks; }
	//! The tracks it covers (every song track when allTracks()).
	QVector<Track*> tracks() const;
	QVector<int> trackIds() const { return m_trackIds; }

	void set(tick_t start, tick_t end, const QVector<int>& trackIds, bool allTracks);
	void clear();

signals:
	void changed();

private:
	ArrangerRange() = default;
	tick_t m_start = 0;
	tick_t m_end = 0;
	QVector<int> m_trackIds;  //!< Track::id()s
	bool m_allTracks = true;
};

} // namespace lmms

#endif // LMMS_ARRANGEMENT_EDITS_H
