/*
 * TakeLane.h - take lanes and the non-destructive composite (Zene Studio task
 *              #600, docs/CLIP-CAPTURE-DESIGN.md slices D/F).
 *
 * What this is. A "take" is a recorded or imported clip that carries a lane tag
 * (`Clip::laneIndex`, the `lane` attribute of the design's §2.6). A "lane" is a
 * child relationship of the track, NOT a second track type: the clips stay in
 * the track's own clip list and the lane is a number on them, exactly as
 * docs/CLIP-CAPTURE-DESIGN.md §2.2 decides.
 *
 * A "composite" (a "comp") is a VIEW: an ordered list of segments, each naming a
 * lane and a tick range, resolved to (lane, offset) at any timeline tick. Nothing
 * here copies, moves, rewrites or even opens a take's audio - the model stores
 * integers, and `resolveSource()` maps a tick back onto the take clip the engine
 * already has, through `Clip::sourceFrameAt()`, the mapping seam the clip wave
 * froze (#611, design §2.4). That is what makes "the source takes are never
 * destructively edited" (the design's acceptance (a)) a property of the type
 * rather than a promise.
 *
 * Invariants (design §2.3, each one a test in tests/src/core/TakeLaneCompTest.cpp):
 *   I6  a composite is ordered and gapless over its span, and every segment
 *       names a lane the track actually has;
 *   I7  lane indices are stable across save/load: a segment that names a lane
 *       the track does not have is dropped by a load, never silently re-pointed;
 *   I9  serialisation is additive: a track with no take lanes writes no element
 *       at all, so a project that never used comping saves byte for byte as it
 *       did before this feature existed.
 *
 * See docs/COMPING.md for the element shape, the group-name decision and the
 * precise list of what is NOT wired yet (nothing renders a composite on the
 * audio thread in this release).
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

#ifndef LMMS_TAKE_LANE_H
#define LMMS_TAKE_LANE_H

#include <vector>

#include <QString>

#include "LmmsTypes.h"
#include "lmms_export.h"

class QDomDocument;
class QDomElement;

namespace lmms
{

class Clip;

/*! One take lane of a track: a stable index and a display name.
 *
 *  The lane carries NO audio of its own and no mute flag: the clips tagged with
 *  its index are the takes, and per-lane audibility is the take clip's own
 *  `muted` (which already exists, is already serialised, and is already what the
 *  engine reads). A second mute flag here would be state nothing renders. */
struct TakeLane
{
	int index = 0;   //!< stable within the track, never renumbered by a removal
	QString name;    //!< display name; may be empty

	bool operator==(const TakeLane& other) const
	{
		return index == other.index && name == other.name;
	}
};

/*! One choice of the composite: over the timeline range `[beginTick, endTick)`
 *  the composite takes its audio from `laneIndex`, slipped by `sourceOffset`
 *  ticks into that lane's take (0 = the take's own start, which is what a plain
 *  "use this lane here" selection means).
 *
 *  The offset is RECORDED AND REPORTED, not yet applied: this release resolves a
 *  tick to the take by the clip's own mapping (see resolveSource), and no
 *  playback path consumes the composite yet - docs/COMPING.md says so plainly. */
struct TakeLaneSegment
{
	int beginTick = 0;
	int endTick = 0;
	int laneIndex = 0;
	int sourceOffset = 0;

	bool operator==(const TakeLaneSegment& other) const
	{
		return beginTick == other.beginTick && endTick == other.endTick
			&& laneIndex == other.laneIndex && sourceOffset == other.sourceOffset;
	}
	bool operator!=(const TakeLaneSegment& other) const { return !(*this == other); }
};

/*! The take lanes of one track and the composite they are comped into.
 *
 *  Plain value type, owned by `Track`, serialised by `Track::saveTrack`. It is
 *  deliberately NOT a QObject and NOT a JournallingObject of its own: the
 *  track's own ProjectJournal checkpoint (`Track::saveState`/`restoreState`)
 *  already carries it, which is what makes every comp.* mutation one undo step
 *  through the same mechanism every other track edit uses. */
class LMMS_EXPORT TakeLaneModel
{
public:
	// ---- lanes ----------------------------------------------------------
	/*! Adds a lane and returns its index: the lowest index the track does not
	 *  already use. An index is never reused while a lane still holds it, and a
	 *  removal never renumbers the survivors (invariant I7). */
	int addLane(const QString& name = QString());
	//! Removes the lane; every segment naming it falls back to the base lane.
	bool removeLane(int index);
	bool hasLane(int index) const;
	int laneCount() const { return static_cast<int>(m_lanes.size()); }
	const std::vector<TakeLane>& lanes() const { return m_lanes; }
	//! The lowest lane index this track has, or -1 when it has none.
	int baseLane() const;

	// ---- the composite (a view) -----------------------------------------
	/*! Chooses lane \p laneIndex over `[beginTick, endTick)`, slipped by
	 *  \p sourceOffset ticks. Any earlier choice over the same range is
	 *  overwritten (a selection paints over what was there), so this is the one
	 *  operation that changes the composite. False = rejected, nothing written:
	 *  an empty or reversed range, a negative offset, or a lane the track does
	 *  not have (a refusal, never a silent clamp - the design's I4 rule). */
	bool selectSegment(int beginTick, int endTick, int laneIndex, int sourceOffset = 0);

	/*! Whether `selectSegment()` would accept these arguments, without writing.
	 *  The single definition of that rule lives here, so a command can refuse a
	 *  state the model refuses BEFORE it takes a journal checkpoint (a checkpoint
	 *  taken for a write that never happens would leave an undo step behind). */
	bool canSelect(int beginTick, int endTick, int laneIndex, int sourceOffset = 0) const;

	/*! Normalises the composite: sorts it, merges segments that are contiguous
	 *  in the same lane at a continuous offset, and - when a span is given
	 *  (`spanEnd > spanBegin >= 0`) - clamps the composite to exactly that span,
	 *  filling every gap with the base lane. Returns the number of segments the
	 *  composite now has; 0 with an empty lane set. */
	int rebuild(int spanBegin = -1, int spanEnd = -1);

	//! The composite, ordered over its span and gapless inside it (I6).
	const std::vector<TakeLaneSegment>& segments() const { return m_segments; }
	//! The timeline range the composite covers; false when it is empty.
	bool span(int* beginTick, int* endTick) const;
	//! The lane and offset that supply \p tick; false when the composite does
	//! not cover it (there is no "default lane" answer: an uncovered tick is
	//! simply not comped).
	bool resolve(int tick, int* laneIndex, int* sourceOffset) const;

	/*! The take the composite would read at \p tick: the clip on the resolved
	 *  lane whose own range covers \p tick, plus its source frame from
	 *  `Clip::sourceFrameAt()`. False means the composite names a lane with no
	 *  covering take there - reported as unresolved, never guessed at. Reads
	 *  only; nothing is written and no buffer is touched. */
	bool resolveSource(const std::vector<Clip*>& clips, int tick,
		f_cnt_t* sourceFrame, int* laneIndex) const;

	/*! The take clip itself at \p tick, or nullptr when the composite does not
	 *  cover the tick or names a lane with no covering take. `resolveSource()`
	 *  and the comp.* result builder share this one lookup, so the two cannot
	 *  disagree about which clip supplies a tick. */
	Clip* takeAt(const std::vector<Clip*>& clips, int tick, int* laneIndex = nullptr) const;

	void clear();
	bool isEmpty() const { return m_lanes.empty() && m_segments.empty(); }

	// ---- project file ---------------------------------------------------
	/*! Writes `<takelanes>`'s CHILDREN onto \p element (the caller creates and
	 *  places the element, so an empty model writes nothing at all - I9). */
	void saveSettings(QDomDocument& doc, QDomElement& element) const;
	/*! Loads from `element`'s children. It CLEARS first: a track element with no
	 *  `<takelanes>` child must reset the model, or a checkpoint taken before
	 *  this feature's first edit could never take it back (the trap the
	 *  zene-control-command-group skill records). */
	void loadSettings(const QDomElement& element);

private:
	//! Splits/removes \p seg against `[beginTick, endTick)`, appending what is
	//! left of it to \p out.
	static void subtractRange(const TakeLaneSegment& seg, int beginTick, int endTick,
		std::vector<TakeLaneSegment>* out);
	void sortSegments();
	//! Merges neighbouring segments that are the same lane at a continuous offset.
	void mergeSegments();
	int fillGaps(int spanBegin, int spanEnd);

	std::vector<TakeLane> m_lanes;
	std::vector<TakeLaneSegment> m_segments;
};

} // namespace lmms

#endif // LMMS_TAKE_LANE_H
