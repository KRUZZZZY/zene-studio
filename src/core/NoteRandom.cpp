/*
 * NoteRandom.cpp - seeded, stateless note randomisation for MIDI depth
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

#include "NoteRandom.h"

#include <algorithm>

namespace lmms
{

namespace NoteRandom
{

namespace
{

/*! Integer finaliser with full avalanche (the splitmix64 mix, 32-bit half).
 *  No state, no table, no allocation - safe on the audio thread. */
inline uint32_t mix( uint32_t x ) noexcept
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

constexpr uint32_t SeedSalt = 0x9e3779b9U;
constexpr float TwoPow32 = 4294967296.f;

} // namespace


uint32_t roll( uint32_t seed, int key, int pos, int length, uint32_t salt )
{
	uint32_t h = mix( seed ^ ( SeedSalt + salt ) );
	h = mix( h ^ ( static_cast<uint32_t>( key ) + 0x85ebca6bU ) );
	h = mix( h ^ ( static_cast<uint32_t>( pos ) + 0xc2b2ae35U ) );
	h = mix( h ^ ( static_cast<uint32_t>( length ) + 0x27d4eb2fU ) );
	return h;
}


float rollUnit( uint32_t seed, int key, int pos, int length, uint32_t salt )
{
	return roll( seed, key, pos, length, salt ) / TwoPow32;
}


bool passesProbability( float probability, uint32_t seed, int key, int pos, int length )
{
	// Fast paths first: a note that keeps the default probability takes
	// exactly the code path it took before MIDI depth existed.
	if( probability >= 1.f ) { return true; }
	if( probability <= 0.f ) { return false; }
	return rollUnit( seed, key, pos, length, 0 ) < probability;
}


float velocityFactor( float jitter, uint32_t seed, int key, int pos, int length )
{
	if( jitter <= 0.f ) { return 1.f; }
	const float j = std::min( jitter, 1.f );
	// Salt 1: an independent stream from the probability roll, so setting a
	// probability does not move the velocities of the notes that do play.
	const float u = rollUnit( seed, key, pos, length, 1 );
	return 1.f + j * ( 2.f * u - 1.f );
}


uint32_t readProjectSeed( const QDomElement& head )
{
	if( head.isNull() ) { return 0; }
	return head.attribute( QStringLiteral( "midiseed" ), QStringLiteral( "0" ) ).toUInt();
}


void writeProjectSeed( QDomElement& head, uint32_t seed )
{
	if( head.isNull() || seed == 0 ) { return; }
	head.setAttribute( QStringLiteral( "midiseed" ), QString::number( seed ) );
}

} // namespace NoteRandom

} // namespace lmms
