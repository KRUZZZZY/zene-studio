/*
 * BounceRegion.h - the render side of a range bounce that replaces only its region
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

#ifndef LMMS_BOUNCE_REGION_H
#define LMMS_BOUNCE_REGION_H

#include <vector>

#include <QDomElement>
#include <QSet>
#include <QString>

#include "BounceInPlace.h"
#include "LmmsTypes.h"
#include "TimePos.h"
#include "lmms_export.h"

namespace lmms
{

class Track;

/*! A range bounce that mutes its sources afterwards (cluster 3971) replaces only the clip pieces inside
 *  [start,end). What it renders must therefore be exactly what those pieces play, plus whatever of that sound
 *  rings on past `end`, because the live mix loses both when the pieces are muted. Everything here works on the
 *  serialised copy the child renders or on the finished WAV, so a queued job never edits the live model before
 *  it completes and a refused render leaves nothing to roll back. */
namespace bouncefix
{

//! Longest tail a range bounce renders past its end: one bar, the tail the child render always adds past the song.
constexpr tick_t MaxTailTicks = DefaultTicksPerBar;

//! The edit a range bounce applies to the serialised session it renders.
struct RegionEdit
{
	bool active = false;
	tick_t start = 0;
	tick_t end = 0;
	QSet<int> frozenIds;  //!< sources that play a frozen take, whose clips the render does not use
};

/*! Cuts every unmuted clip of the audio source track element \a track at \a start and \a end (the cut
 *  arrangement::isolateRegion makes on the live model: the right piece keeps its content through its start offset)
 *  and mutes each clip that starts outside [start,end): those pieces stay audible in the song, so the render must
 *  not contain them. Clips with no timeline position (pattern clips) are left alone. */
LMMS_EXPORT void isolateRegionInTrackElement(QDomElement track, tick_t start, tick_t end);

/*! Ticks past \a range.end the render of \a tracks keeps for the sound of the in-range pieces that rings on: up to
 *  MaxTailTicks, and no further than the next unmuted clip a source has at or after the range's end (that clip
 *  stays audible in the song). A source that plays a frozen take is skipped (its tail is already in the take) and does
 *  not shorten or cancel the tail of the other sources; 0 only when no unfrozen audio source is left. Reads the
 *  live model, so it is called while the tracks are intact, before any cut. */
LMMS_EXPORT tick_t tailTicksPastRange(const std::vector<Track*>& tracks, const BounceInPlace::Range& range);

/*! How many frames of [\a from, \a from + \a maxFrames) of \a wav reach its last audible sample: the tail with its
 *  trailing silence cut off. 0 when the window is silent or the file cannot be read. */
LMMS_EXPORT qint64 audibleFramesIn(const QString& wav, qint64 from, qint64 maxFrames);

} // namespace bouncefix

} // namespace lmms

#endif // LMMS_BOUNCE_REGION_H
