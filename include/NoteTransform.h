/*
 * NoteTransform.h - search/transform operations over note events
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

#ifndef LMMS_NOTE_TRANSFORM_H
#define LMMS_NOTE_TRANSFORM_H

#include <limits>
#include <vector>

#include "Note.h"

namespace lmms
{


/*! Search and transform operations on note events.
 *
 *  Everything here is a plain function over a NoteVector, so it is testable
 *  headlessly with no Engine, no track and no GUI, and so the piano roll can
 *  call it for "select by predicate, then transform" without reimplementing
 *  the arithmetic. The GUI hook-up is not part of this change.
 */
namespace NoteTransform
{

//! How a scale takes part in a filter.
enum class ScaleMatch
{
	Ignore,		//!< the scale clauses are not applied
	InScale,	//!< match notes whose pitch class is in the scale
	OutOfScale	//!< match notes whose pitch class is NOT in the scale
};

//! Where a quantise moves a note that is off the grid.
enum class QuantizeMode
{
	Nearest,
	Floor,
	Ceil
};

/*! A note predicate. Only the clauses whose `use*` flag is set take part, so
 *  an all-default Filter matches every note. */
struct Filter
{
	bool useKeyRange = false;
	int minKey = 0;
	int maxKey = NumKeys - 1;

	bool useVelocityRange = false;
	volume_t minVelocity = MinVolume;
	volume_t maxVelocity = MaxVolume;

	bool usePositionRange = false;
	tick_t minPos = 0;
	tick_t maxPos = std::numeric_limits<tick_t>::max();

	ScaleMatch scaleMatch = ScaleMatch::Ignore;
	//! Pitch classes (0..11; a degree outside that range is taken modulo 12).
	std::vector<int> scaleDegrees;

	//! Select exactly the notes that do NOT match.
	bool invert = false;
};

/*! True when `note` matches every active clause of `f`, before `invert` is
 *  applied. An empty `scaleDegrees` makes a scale clause match nothing. */
bool matches( const Note& note, const Filter& f );

//! The matching notes, in the order they appear in `notes`.
NoteVector select( const NoteVector& notes, const Filter& f );

//! Moves every note's pitch by `semitones`, clamped to the MIDI range.
//! Returns the number of notes whose key actually changed.
int transpose( const NoteVector& notes, int semitones );

//! Adds `delta` to every note's velocity (0..200), clamped. Returns the number
//! of notes whose velocity actually changed.
int offsetVelocity( const NoteVector& notes, int delta );

//! Multiplies every note's velocity by `factor`, rounded and clamped.
//! Returns the number of notes whose velocity actually changed.
int scaleVelocity( const NoteVector& notes, float factor );

/*! Moves every note's position onto a `grid`-tick grid. A non-positive grid is
 *  a no-op. Returns the number of notes whose position actually changed. */
int quantizePositions( const NoteVector& notes, int grid, QuantizeMode mode );

/*! Moves every note that is off the scale to the nearest in-scale pitch (a tie
 *  resolves downward, i.e. to the lower key), clamped to the MIDI range. Notes
 *  already in the scale are left alone. An empty scale is a no-op. Returns the
 *  number of notes whose key actually changed. */
int snapToScale( const NoteVector& notes, const std::vector<int>& scaleDegrees );

//! Puts a vector back into the order MidiClip expects (position, then key
//! descending) after a transform moved keys or positions about.
void sortByPosition( NoteVector& notes );

//! The pitch classes a scale contains, sorted and deduplicated.
std::vector<int> pitchClasses( const std::vector<int>& scaleDegrees );

} // namespace NoteTransform


} // namespace lmms

#endif // LMMS_NOTE_TRANSFORM_H
