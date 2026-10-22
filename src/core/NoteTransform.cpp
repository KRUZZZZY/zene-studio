/*
 * NoteTransform.cpp - search/transform operations over note events
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

#include "NoteTransform.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "NoteRandom.h"

namespace lmms
{

namespace NoteTransform
{

/*! The two jitter streams of quantizeNotes()'s humanise amount. Distinct salts
 *  mean one note's timing jitter and its velocity jitter are independent draws
 *  from the same seed, which is NoteRandom's own convention for MIDI depth. */
constexpr uint32_t HumaniseTimingSalt = 0x9e3779b9u;
constexpr uint32_t HumaniseVelocitySalt = 0x85ebca6bu;

namespace
{

//! The pitch class of a MIDI key, 0..11.
inline int pitchClassOf( int key ) noexcept
{
	return ( ( key % KeysPerOctave ) + KeysPerOctave ) % KeysPerOctave;
}

inline bool isInScale( const std::vector<int>& classes, int key ) noexcept
{
	return std::binary_search( classes.begin(), classes.end(), pitchClassOf( key ) );
}

/*! Whether `key`'s pitch class is in the scale, without building the sorted
 *  pitch-class list: matches() is called once per note, so it must not
 *  allocate. A degree outside 0..11 is taken modulo 12. */
inline bool degreeInScale( const std::vector<int>& degrees, int key ) noexcept
{
	const int pc = pitchClassOf( key );
	for( int degree : degrees )
	{
		if( pitchClassOf( degree ) == pc ) { return true; }
	}
	return false;
}

//! The key, velocity, position and length clauses of `f`.
inline bool rangeAccepts( const Note& note, const Filter& f )
{
	const auto inside = []( bool use, auto value, auto low, auto high ) {
		return !use || ( value >= low && value <= high );
	};
	return inside( f.useKeyRange, note.key(), f.minKey, f.maxKey )
		&& inside( f.useVelocityRange, note.getVolume(), f.minVelocity, f.maxVelocity )
		&& inside( f.usePositionRange, static_cast<tick_t>( note.pos() ), f.minPos, f.maxPos )
		&& inside( f.useLengthRange, static_cast<tick_t>( note.length() ), f.minLength, f.maxLength );
}

/*! The pitch-class clause of `f`. An inactive clause accepts everything; an
 *  empty scale has no members and so accepts nothing in either direction. */
inline bool scaleAccepts( const Note& note, const Filter& f )
{
	if( f.scaleMatch == ScaleMatch::Ignore ) { return true; }
	if( f.scaleDegrees.empty() ) { return false; }
	const bool inScale = degreeInScale( f.scaleDegrees, note.key() );
	return f.scaleMatch == ScaleMatch::InScale ? inScale : !inScale;
}

/*! The key a transpose by `semitones` gives `key`: the ONE rule both the
 *  transpose and the count of what it would move follow, so the two cannot
 *  disagree. The shift is bounded first: key + INT_MAX would overflow before
 *  the clamp saw it. */
inline int transposedKey( int key, int semitones ) noexcept
{
	return std::clamp( key + std::clamp( semitones, -NumKeys, NumKeys ), 0, NumKeys - 1 );
}

/*! The volume a velocity offset by `delta` gives `volume`: the ONE rule both
 *  the offset and the count of what it would move follow. The delta is bounded
 *  first, so the sum cannot overflow. */
inline int offsetVolume( int volume, int delta ) noexcept
{
	const int bounded = std::clamp( delta, -static_cast<int>( MaxVolume ), static_cast<int>( MaxVolume ) );
	return std::clamp( volume + bounded, static_cast<int>( MinVolume ), static_cast<int>( MaxVolume ) );
}

/*! The nearest in-scale pitch to `key`, with a tie resolving downward. Returns
 *  `key` unchanged when it is already in the scale (or is unreachable, which a
 *  non-empty scale cannot make it). */
int nearestInScaleKey( int key, const std::vector<int>& classes )
{
	if( isInScale( classes, key ) ) { return key; }
	// As far as the MIDI range allows, not just half an octave: a scale may
	// hold a single pitch class, which can sit further from a boundary key
	// than any octave, and that key still has a nearest legal pitch.
	for( int distance = 1; distance < NumKeys; ++distance )
	{
		const int down = key - distance;
		if( down >= 0 && isInScale( classes, down ) ) { return down; }
		const int up = key + distance;
		if( up <= NumKeys - 1 && isInScale( classes, up ) ) { return up; }
	}
	return key;
}

} // namespace


std::vector<int> pitchClasses( const std::vector<int>& scaleDegrees )
{
	std::vector<int> classes;
	classes.reserve( scaleDegrees.size() );
	for( int degree : scaleDegrees )
	{
		classes.push_back( pitchClassOf( degree ) );
	}
	std::sort( classes.begin(), classes.end() );
	classes.erase( std::unique( classes.begin(), classes.end() ), classes.end() );
	return classes;
}


bool matches( const Note& note, const Filter& f )
{
	return rangeAccepts( note, f ) && scaleAccepts( note, f );
}


NoteVector select( const NoteVector& notes, const Filter& f )
{
	NoteVector selected;
	for( Note* note : notes )
	{
		if( note != nullptr && ( matches( *note, f ) != f.invert ) )
		{
			selected.push_back( note );
		}
	}
	return selected;
}


int transpose( const NoteVector& notes, int semitones )
{
	int changed = 0;
	if( semitones == 0 ) { return 0; }
	for( Note* note : notes )
	{
		if( note == nullptr ) { continue; }
		const int target = transposedKey( note->key(), semitones );
		if( target != note->key() )
		{
			note->setKey( target );
			++changed;
		}
	}
	return changed;
}


int countTransposed( const NoteVector& notes, int semitones )
{
	int count = 0;
	for( const Note* note : notes )
	{
		if( note == nullptr ) { continue; }
		if( transposedKey( note->key(), semitones ) != note->key() ) { ++count; }
	}
	return count;
}


int offsetVelocity( const NoteVector& notes, int delta )
{
	int changed = 0;
	for( Note* note : notes )
	{
		if( note == nullptr ) { continue; }
		const int current = static_cast<int>( note->getVolume() );
		const int target = offsetVolume( current, delta );
		if( target != current )
		{
			note->setVolume( static_cast<volume_t>( target ) );
			++changed;
		}
	}
	return changed;
}


int countOffsetVelocity( const NoteVector& notes, int delta )
{
	int count = 0;
	for( const Note* note : notes )
	{
		if( note == nullptr ) { continue; }
		if( offsetVolume( static_cast<int>( note->getVolume() ), delta ) != static_cast<int>( note->getVolume() ) )
		{
			++count;
		}
	}
	return count;
}


int scaleVelocity( const NoteVector& notes, float factor )
{
	// A factor that is not a number is not a velocity to scale BY: std::clamp
	// hands a NaN straight back and std::lround of one is a domain error, so
	// every note would be moved by an amount nothing asked for. Nothing moves.
	if( !std::isfinite( factor ) ) { return 0; }
	const float railLow = static_cast<float>( MinVolume );
	const float railHigh = static_cast<float>( MaxVolume );
	int changed = 0;
	for( Note* note : notes )
	{
		if( note == nullptr ) { continue; }
		const int current = static_cast<int>( note->getVolume() );
		// Bounded BEFORE the rounding: a large factor overflows the product to
		// infinity, which is outside lround's domain just as a NaN is.
		const float scaled = std::clamp( current * factor, railLow, railHigh );
		const int target = static_cast<int>( std::lround( scaled ) );
		if( target != current )
		{
			note->setVolume( static_cast<volume_t>( target ) );
			++changed;
		}
	}
	return changed;
}


int quantizePositions( const NoteVector& notes, int grid, QuantizeMode mode )
{
	if( grid <= 0 ) { return 0; }
	int changed = 0;
	for( Note* note : notes )
	{
		if( note == nullptr ) { continue; }
		const int pos = static_cast<int>( note->pos() );
		int target = pos;
		switch( mode )
		{
			case QuantizeMode::Nearest:
				target = ( ( pos + grid / 2 ) / grid ) * grid;
				break;
			case QuantizeMode::Floor:
				target = ( pos / grid ) * grid;
				break;
			case QuantizeMode::Ceil:
				target = ( ( pos + grid - 1 ) / grid ) * grid;
				break;
		}
		if( target != pos )
		{
			note->rememberTiming();  // what note.restore_timing puts back (0.5.0 L07)
			note->setPos( TimePos( target ) );
			++changed;
		}
	}
	return changed;
}


namespace
{

//! The grid step a note at `pos` is taken to, by the mode's own rule.
int gridTarget( int pos, int grid, QuantizeMode mode )
{
	switch( mode )
	{
		case QuantizeMode::Nearest: return ( ( pos + grid / 2 ) / grid ) * grid;
		case QuantizeMode::Floor:   return ( pos / grid ) * grid;
		case QuantizeMode::Ceil:    return ( ( pos + grid - 1 ) / grid ) * grid;
	}
	return pos;
}

/*! The humanise jitter for one note, in [-span, span], drawn from the caller's
 *  seed and the note's OWN identity (NoteRandom's rule): its pitch, its LENGTH
 *  and the grid SLOT it is being taken to - deliberately not its current
 *  position, which is what makes the call a fixed point. A jitter drawn from
 *  the position would re-roll on every repeat (the position has changed), so
 *  the same call on the same clip would wander inside the bound instead of
 *  reproducing the take it produced a moment ago. `salt` separates the timing
 *  stream from the velocity one, exactly as MIDI depth's two streams are
 *  separated. */
int jitterFor( int span, uint32_t seed, const Note& note, int slotTick, uint32_t salt )
{
	if( span <= 0 ) { return 0; }
	const float unit = NoteRandom::rollUnit( seed, note.key(), slotTick,
		static_cast<int>( note.length().getTicks() ), salt );
	return static_cast<int>( std::lround( unit * ( 2.0f * span ) - span ) );
}

} // namespace


int quantizeNotes( const NoteVector& notes, const QuantizeOptions& options )
{
	if( options.grid <= 0 ) { return 0; }
	// std::clamp keeps a NaN, which std::lround cannot round: NaN moves nothing.
	const float strength = std::isnan( options.strength ) ? 0.0f : std::clamp( options.strength, 0.0f, 1.0f );

	int changed = 0;
	for( Note* note : notes )
	{
		if( note == nullptr ) { continue; }
		const int pos = static_cast<int>( note->pos().getTicks() );
		const int velocity = static_cast<int>( note->getVolume() );
		const int target = gridTarget( pos, static_cast<int>( options.grid ), options.mode );

		int moved = pos + static_cast<int>( std::lround( strength * ( target - pos ) ) );
		moved = std::max( 0, moved + jitterFor( static_cast<int>( options.humaniseTicks ),
			options.seed, *note, target, HumaniseTimingSalt ) );
		const int newVelocity = std::clamp( velocity + jitterFor( options.humaniseVelocity,
			options.seed, *note, target, HumaniseVelocitySalt ), static_cast<int>( MinVolume ),
			static_cast<int>( MaxVolume ) );

		if( moved != pos )
		{
			note->rememberTiming();  // what note.restore_timing puts back (0.5.0 L07)
			note->setPos( TimePos( moved ) );
		}
		if( newVelocity != velocity ) { note->setVolume( static_cast<volume_t>( newVelocity ) ); }
		if( moved != pos || newVelocity != velocity ) { ++changed; }
	}
	return changed;
}


int snapToScale( const NoteVector& notes, const std::vector<int>& scaleDegrees )
{
	const std::vector<int> classes = pitchClasses( scaleDegrees );
	if( classes.empty() ) { return 0; }

	int changed = 0;
	for( Note* note : notes )
	{
		if( note == nullptr ) { continue; }
		const int key = note->key();
		const int best = nearestInScaleKey( key, classes );
		if( best != key )
		{
			note->setKey( best );
			++changed;
		}
	}
	return changed;
}


int countOffScale( const NoteVector& notes, const std::vector<int>& scaleDegrees )
{
	const std::vector<int> classes = pitchClasses( scaleDegrees );
	if( classes.empty() ) { return 0; }

	int count = 0;
	for( const Note* note : notes )
	{
		if( note == nullptr ) { continue; }
		if( nearestInScaleKey( note->key(), classes ) != note->key() ) { ++count; }
	}
	return count;
}


void sortByPosition( NoteVector& notes )
{
	std::sort( notes.begin(), notes.end(), Note::lessThan );
}

} // namespace NoteTransform

} // namespace lmms
