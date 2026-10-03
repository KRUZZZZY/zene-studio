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
//! clip.delete's recipe: detach @a clip from @a track, then delete it.
LMMS_EXPORT void deleteClip(Track* track, Clip* clip);

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

//! ED-043: how a deletion closes its gap - not at all, on the clip's track, or on every track.
enum class Ripple { Off, Track, All };
//! ED-044: what a clip moved onto others does to them - nothing, trims them away under it, or
//! crossfades with them over the overlap.
enum class Overlap { Free, Trim, Crossfade };

/*! The arranger's editing modes (interface preferences, kept in the config file): the ripple and
 *  overlap modes, and whether moving or resizing a clip snaps to other clips' edges and to markers
 *  (ED-020) as well as to the grid. Read by the arranger's own gestures and by the clip.* / range.*
 *  commands, so the mouse and an agent edit the same way. */
struct Modes
{
	Ripple ripple = Ripple::Off;
	Overlap overlap = Overlap::Free;
	bool snapToEdges = true;
	//! ED-190: a clip's move carries the points of the automation lanes following its track.
	bool automationFollowsClips = true;
	//! ED-177 (0.5.0 L05): a short fade every sample clip gets at both ends, in ms (0 off).
	int autoFadeMs = 0;
	//! ED-150: sample clips mark where their audio has its transients.
	bool showTransients = false;
};
constexpr int MaxAutoFadeMs = 50;
LMMS_EXPORT Modes modes();
LMMS_EXPORT void setModes(const Modes& modes);
//! The automatic fade, for the audio thread: a relaxed atomic, set by setModes and loadModes.
LMMS_EXPORT int autoFadeMs();
//! Read the modes from the config into what the audio thread reads (once, at start-up).
LMMS_EXPORT void loadModes();

//! The tracks a ripple from @a track touches under @a mode (none, the one, or every song track).
LMMS_EXPORT QVector<Track*> rippleTracks(Track* track, Ripple mode);

//! The undo checkpoint a deletion from @a track takes under @a mode: the track's, or every
//! track's at once (Ripple::All) - one step either way. clip.delete and the arranger's delete.
LMMS_EXPORT void checkpointForDelete(Track* track, Ripple mode);

//! What the arranger does after the user drops @a moved: the overlap mode, as one more undo step
//! (the track's checkpoint), when it is not Free.
LMMS_EXPORT void afterUserMove(Clip* moved, tick_t oldStart = -1);

/*! ED-190: move the points of every automation lane following @a track that lie in
 *  [@a oldStart, @a oldStart + @a length) by @a delta (when the mode says so). The lanes' clips
 *  are checkpointed first when @a checkpoint. How many following lanes it visited (a lane with
 *  no point in the range counts too; its checkpoint folds into the same undo step). */
LMMS_EXPORT int moveFollowingAutomation(Track* track, tick_t oldStart, tick_t length, tick_t delta, bool checkpoint);

//! The tolerance edge snapping uses, in pixels (converted to ticks at the current zoom).
constexpr int SnapEdgePixels = 8;

//! Close the gap a deletion of [@a start, @a start + @a length) left, per @a mode.
LMMS_EXPORT int rippleAfterDelete(Track* track, tick_t start, tick_t length, Ripple mode);
//! ED-043 (0.5.0 L05): checkpoint the tracks other than @a track that a ripple from it touches
//! (the caller checkpoints @a track), so a rippled edit is one undo step.
LMMS_EXPORT void checkpointRipple(Track* track);
//! With ripple on: every clip on the ripple tracks of @a track starting at or after @a from moves by
//! @a delta, except @a keep - an insert or a move pushing (or pulling) what follows it.
LMMS_EXPORT int rippleShift(Track* track, tick_t from, tick_t delta, const Clip* keep = nullptr);
//! The ripple of a clip moved from @a oldStart (a no-op unless @a ripple).
LMMS_EXPORT void rippleAfterMove(Clip* moved, tick_t oldStart, bool ripple);

//! Apply @a mode to the clips under @a moved on its track: trim them away under it, or crossfade
//! with the ones it overlaps (fades over the overlap). How many clips were changed.
LMMS_EXPORT int applyOverlap(Clip* moved, Overlap mode);

/*! ED-020: @a proposed (the grid-snapped start of a clip of @a length being moved) moved onto
 *  the nearest other clip edge or marker within @a tolerance ticks - its start or its end may land
 *  there; @a proposed unchanged when nothing is that close. @a moving is left out. */
LMMS_EXPORT tick_t snapToEdges(tick_t proposed, tick_t length, const Clip* moving, tick_t tolerance);

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
