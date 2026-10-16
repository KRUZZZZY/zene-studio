/*
 * MpeExpressionStreamPlayback.cpp - scheduled playback of a note's MPE
 *                                   expression event streams (Q050-016)
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

/*! Q050-016, the playback half of the MPE expression event streams.
 *
 *  A note's stream (MpeExpressionStream, three axes of (tick, value) points) is
 *  read here, once per period, and each point whose tick falls inside the
 *  period is sent to the instrument at its own frame offset:
 *
 *    - pitch   -> MidiPitchBend, on the note's own channel, at the point's
 *                 offset, and a frequency update for the built-in synthesisers
 *                 (who never see MIDI and are driven by a note's frequency);
 *    - pressure-> MidiChannelPressure (MPE "Z");
 *    - timbre  -> MidiControlChange CC74 (MPE "Y").
 *
 *  The value a stream axis holds BEFORE its first point is the note's scalar
 *  snapshot (mpePitchCents/mpePressure/mpeTimbre), so a stream composes with a
 *  captured expression instead of erasing it, and a note with no stream takes
 *  the unchanged scalar path (sendMpeExpressionMidi). This file adds no state
 *  to the handle: it recomputes the current window from m_totalFramesPlayed, so
 *  a point is sent exactly once (the windows are half-open, [startTick,
 *  endTick)).
 *
 *  Realtime contract (absolute): the audio thread reads the note's own vectors
 *  (no allocation), takes no lock, does no I/O and emits no Qt signal. The only
 *  work is a handful of integer/fp comparisons and the MidiEvent sends that the
 *  note-on already uses. TSan does not run on this box (runtime crash at thread
 *  creation), so the race argument is by design: the stream is written on the
 *  UI/control thread only, and a playing handle owns its own deep copy (Note's
 *  copy constructor), so the audio thread never observes a half-written vector.
 */

#include <cmath>

#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiEvent.h"
#include "MpeExpression.h"
#include "NotePlayHandle.h"
#include "TimePos.h"

namespace lmms
{

namespace
{

//! The stream axes, in the order the scheduler walks them.
enum StreamAxis
{
	PitchAxis = 0,
	PressureAxis = 1,
	TimbreAxis = 2,
	AxisCount = 3,
};

} // namespace


tick_t NotePlayHandle::mpeStreamTick( f_cnt_t frameWithinNote ) const
{
	const float framesPerTick = Engine::framesPerTick();
	if( framesPerTick <= 0.f )
	{
		return 0;
	}
	return static_cast<tick_t>( static_cast<double>( frameWithinNote ) / framesPerTick );
}




int NotePlayHandle::effectiveMpePitchCents() const
{
	if( !hasMpeStream() || mpeStream().pitch().empty() )
	{
		return mpePitchCents();
	}
	// The scalar snapshot is the fallback: a stream's value before its first
	// point, so a streamed note keeps the bend a capture stored.
	return mpeStream().pitchCentsAt( mpeStreamTick( m_totalFramesPlayed ), mpePitchCents() );
}




void NotePlayHandle::sendMpeExpressionAxisEvent( int axis, int value, f_cnt_t offset )
{
	const auto time = TimePos::fromFrames( offset, Engine::framesPerTick() );
	switch( axis )
	{
		case PitchAxis:
			// A bend point is stored in cents like the scalar snapshot; the
			// instrument hears the same bend the frequency path applies. MPE's
			// default member-channel range is 48 semitones, and the scalar
			// snapshot uses the same range (MpeNoteExpression::MaxPitchCents).
			m_instrumentTrack->processOutEvent(
				MidiEvent( MidiPitchBend, midiChannel(),
					MpeExpression::bendForPitchCents( value, MpeExpression::DefaultBendRangeSemitones ) ),
				time, offset );
			break;

		case PressureAxis:
			m_instrumentTrack->processOutEvent(
				MidiEvent( MidiChannelPressure, midiChannel(), value ), time, offset );
			break;

		case TimbreAxis:
			m_instrumentTrack->processOutEvent(
				MidiEvent( MidiControlChange, midiChannel(), MpeTimbreController, value ),
				time, offset );
			break;

		default:
			break;
	}
}




namespace
{

//! The "this point is not in the period" answer of mpeStreamPointOffset().
constexpr f_cnt_t kNoStreamOffset = static_cast<f_cnt_t>( -1 );

} // namespace


f_cnt_t NotePlayHandle::mpeStreamPointOffset( tick_t tick, f_cnt_t startFrame,
	f_cnt_t framesThisPeriod, f_cnt_t periodStart, float framesPerTick ) const
{
	long delta = std::lround( static_cast<double>( tick ) * framesPerTick )
		- static_cast<long>( startFrame );
	// A tick is assigned to a period by the integer tick window, but the frame
	// it rounds to can sit a hair before the period's first frame; clamp it to
	// the period start so a point on a boundary is emitted here, not dropped
	// between two periods.
	if( delta < 0 )
	{
		delta = 0;
	}
	if( delta >= static_cast<long>( framesThisPeriod ) )
	{
		return kNoStreamOffset;
	}
	return periodStart + static_cast<f_cnt_t>( delta );
}




void NotePlayHandle::scheduleMpeStreamAxis( const std::vector<MpeExpressionPoint>* points,
	int axis, int base, bool firstPeriod, f_cnt_t startFrame, tick_t startTick, tick_t endTick,
	f_cnt_t framesThisPeriod, f_cnt_t periodStart, float framesPerTick )
{
	if( points->empty() )
	{
		return;
	}

	// Establish the value the axis holds at tick 0 when the first point is
	// later: the scalar snapshot. When the first point sits at 0 the loop below
	// sends it, so no duplicate is emitted.
	if( firstPeriod && points->front().tick > 0 )
	{
		sendMpeExpressionAxisEvent( axis, base, periodStart );
	}

	for( const MpeExpressionPoint& point : *points )
	{
		if( point.tick < startTick )
		{
			continue;
		}
		if( point.tick >= endTick )
		{
			break;	// sorted: every later point is later still
		}
		const f_cnt_t at = mpeStreamPointOffset( point.tick, startFrame, framesThisPeriod,
			periodStart, framesPerTick );
		if( at == kNoStreamOffset )
		{
			continue;
		}
		sendMpeExpressionAxisEvent( axis, point.value, at );
	}
}




void NotePlayHandle::scheduleMpeExpressionStream( f_cnt_t framesThisPeriod )
{
	if( !hasMpeStream() || framesThisPeriod == 0 )
	{
		return;
	}

	const float framesPerTick = Engine::framesPerTick();
	if( framesPerTick <= 0.f )
	{
		return;
	}

	const MpeExpressionStream& stream = mpeStream();
	const bool firstPeriod = ( m_totalFramesPlayed == 0 );
	const f_cnt_t startFrame = m_totalFramesPlayed;
	// The note starts `offset()` frames into its first period and at frame 0 of
	// every later one, exactly like the note-on's own offset.
	const f_cnt_t periodStart = firstPeriod ? offset() : 0;
	// Half-open tick window: a point on the boundary belongs to the later
	// period only, so no point is emitted twice.
	const tick_t startTick = mpeStreamTick( startFrame );
	const tick_t endTick = mpeStreamTick( startFrame + framesThisPeriod );

	scheduleMpeStreamAxis( &stream.pitch(), PitchAxis, mpePitchCents(), firstPeriod, startFrame,
		startTick, endTick, framesThisPeriod, periodStart, framesPerTick );
	scheduleMpeStreamAxis( &stream.pressure(), PressureAxis, mpePressure(), firstPeriod, startFrame,
		startTick, endTick, framesThisPeriod, periodStart, framesPerTick );
	scheduleMpeStreamAxis( &stream.timbre(), TimbreAxis, mpeTimbre(), firstPeriod, startFrame,
		startTick, endTick, framesThisPeriod, periodStart, framesPerTick );

	// Built-in synthesisers are tuned from m_frequency per period, so a
	// streamed pitch curve retunes them from the next period's update. The
	// sample-accurate part is the bend event above, which a MIDI consumer sees.
	if( stream.hasPitch() )
	{
		setFrequencyUpdate();
	}
}

} // namespace lmms
