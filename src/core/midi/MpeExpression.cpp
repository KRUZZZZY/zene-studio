/*
 * MpeExpression.cpp - per-note MPE (MIDI Polyphonic Expression) expression
 *
 * Copyright (c) 2026 Zene Studio developers
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

#include "MpeExpression.h"

#include <algorithm>

#include "Midi.h"
#include "MidiEvent.h"

namespace lmms
{

std::atomic_bool MpeExpression::s_enabled{ false };


bool MpeExpression::isEnabled()
{
	return s_enabled.load( std::memory_order_relaxed );
}




void MpeExpression::setEnabled( bool enabled )
{
	s_enabled.store( enabled, std::memory_order_relaxed );
}




int MpeNoteExpression::clampPitchCents( int cents )
{
	return std::clamp( cents, -MaxPitchCents, MaxPitchCents );
}




int MpeNoteExpression::clamp7Bit( int value )
{
	return std::clamp( value, 0, MidiMaxVelocity );
}




bool MpeNoteExpression::isNeutral() const
{
	return pitchCents == 0 && pressure == 0 && timbre == 0;
}




bool MpeNoteExpression::operator==( const MpeNoteExpression& other ) const
{
	return pitchCents == other.pitchCents && pressure == other.pressure
		&& timbre == other.timbre;
}




MpeExpression::MpeExpression() = default;




bool MpeExpression::validChannel( int channel )
{
	return channel >= 0 && channel < ChannelCount;
}




void MpeExpression::setMasterChannel( int channel )
{
	m_masterChannel = validChannel( channel ) ? channel : DefaultMasterChannel;
}




bool MpeExpression::isMemberChannel( int channel ) const
{
	return validChannel( channel ) && channel != m_masterChannel;
}




void MpeExpression::setBendRangeSemitones( int semitones )
{
	m_bendRangeSemitones = std::clamp( semitones, 1,
		MpeNoteExpression::MaxPitchCents / 100 );
}




int MpeExpression::pitchCentsForBend( int bend14, int bendRangeSemitones )
{
	const int halfSpan = ( MidiMaxPitchBend + 1 ) / 2;	// 8192
	const int range = std::clamp( bendRangeSemitones, 1,
		MpeNoteExpression::MaxPitchCents / 100 );
	const int offset = std::clamp( bend14, 0, MidiMaxPitchBend ) - halfSpan;
	const long cents = static_cast<long>( offset ) * range * 100 / halfSpan;
	return MpeNoteExpression::clampPitchCents( static_cast<int>( cents ) );
}




void MpeExpression::reset()
{
	m_channels = {};
}




void MpeExpression::addActiveNote( ChannelState& state, int key )
{
	for( int i = 0; i < state.keyCount; ++i )
	{
		if( state.keys[i] == key ) { return; }
	}
	if( state.keyCount < MaxActiveNotesPerChannel )
	{
		state.keys[state.keyCount++] = key;
	}
	// Over the cap: the note is not tracked, and its expression stays whatever
	// it was. Dropping it is the bounded-memory answer; a fixed cap must not
	// grow on the MIDI thread.
}




void MpeExpression::removeActiveNote( ChannelState& state, int key )
{
	for( int i = 0; i < state.keyCount; ++i )
	{
		if( state.keys[i] != key ) { continue; }
		for( int j = i + 1; j < state.keyCount; ++j )
		{
			state.keys[j - 1] = state.keys[j];
		}
		state.keys[--state.keyCount] = 0;
		return;
	}
}




void MpeExpression::noteOn( int channel, int key )
{
	if( !isMemberChannel( channel ) ) { return; }
	addActiveNote( channelState( channel ), key );
}




void MpeExpression::noteOff( int channel, int key )
{
	if( !isMemberChannel( channel ) ) { return; }
	removeActiveNote( channelState( channel ), key );
}




bool MpeExpression::handleExpressionEvent( const MidiEvent& event )
{
	if( !isMemberChannel( event.channel() ) ) { return false; }

	ChannelState& state = channelState( event.channel() );
	switch( event.type() )
	{
		case MidiPitchBend:
			state.bend14 = std::clamp<int>( event.pitchBend(), 0, MidiMaxPitchBend );
			return true;

		case MidiChannelPressure:
			state.pressure = MpeNoteExpression::clamp7Bit( event.channelPressure() );
			return true;

		case MidiControlChange:
			if( event.controllerNumber() != MpeTimbreController ) { return false; }
			state.timbre = MpeNoteExpression::clamp7Bit( event.controllerValue() );
			return true;

		default:
			return false;
	}
}




MpeNoteExpression MpeExpression::current( int channel ) const
{
	if( !validChannel( channel ) ) { return {}; }

	const ChannelState& state = channelState( channel );
	return MpeNoteExpression{
		pitchCentsForBend( state.bend14, m_bendRangeSemitones ),
		state.pressure,
		state.timbre,
	};
}




int MpeExpression::activeNoteCount( int channel ) const
{
	return isMemberChannel( channel ) ? channelState( channel ).keyCount : 0;
}




int MpeExpression::activeNote( int channel, int index ) const
{
	if( !isMemberChannel( channel ) || index < 0
		|| index >= channelState( channel ).keyCount )
	{
		return -1;
	}
	return channelState( channel ).keys[index];
}




} // namespace lmms
