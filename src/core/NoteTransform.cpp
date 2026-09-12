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

namespace lmms
{

namespace NoteTransform
{

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

//! The key, velocity and position clauses of `f`.
inline bool rangeAccepts( const Note& note, const Filter& f )
{
	if( f.useKeyRange && ( note.key() < f.minKey || note.key() > f.maxKey ) ) { return false; }
	if( f.useVelocityRange
		&& ( note.getVolume() < f.minVelocity || note.getVolume() > f.maxVelocity ) )
	{
		return false;
	}
	if( f.usePositionRange && ( note.pos() < f.minPos || note.pos() > f.maxPos ) ) { return false; }
	return true;
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

/*! The nearest in-scale pitch to `key`, with a tie resolving downward. Returns
 *  `key` unchanged when it is already in the scale (or is unreachable, which a
 *  non-empty scale cannot make it). */
int nearestInScaleKey( int key, const std::vector<int>& classes )
{
	if( isInScale( classes, key ) ) { return key; }
	for( int distance = 1; distance <= KeysPerOctave / 2; ++distance )
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
		const int target = std::clamp( note->key() + semitones, 0, NumKeys - 1 );
		if( target != note->key() )
		{
			note->setKey( target );
			++changed;
		}
	}
	return changed;
}


int offsetVelocity( const NoteVector& notes, int delta )
{
	int changed = 0;
	for( Note* note : notes )
	{
		if( note == nullptr ) { continue; }
		const int current = static_cast<int>( note->getVolume() );
		const int target = std::clamp( current + delta, static_cast<int>( MinVolume ), static_cast<int>( MaxVolume ) );
		if( target != current )
		{
			note->setVolume( static_cast<volume_t>( target ) );
			++changed;
		}
	}
	return changed;
}


int scaleVelocity( const NoteVector& notes, float factor )
{
	int changed = 0;
	for( Note* note : notes )
	{
		if( note == nullptr ) { continue; }
		const int current = static_cast<int>( note->getVolume() );
		const int target = std::clamp( static_cast<int>( std::lround( current * factor ) ),
			static_cast<int>( MinVolume ), static_cast<int>( MaxVolume ) );
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
			note->setPos( TimePos( target ) );
			++changed;
		}
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


void sortByPosition( NoteVector& notes )
{
	std::sort( notes.begin(), notes.end(), Note::lessThan );
}

} // namespace NoteTransform

} // namespace lmms
