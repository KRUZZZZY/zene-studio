/*
 * MpeSynthExpression.cpp - per-note MPE pressure and timbre applied to the
 *                          built-in (per-note) synthesisers (Q050-311)
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

#include "MpeSynthExpression.h"

#include <algorithm>
#include <cmath>

#include "AudioEngine.h"
#include "Engine.h"
#include "NotePlayHandle.h"
#include "SampleFrame.h"

namespace lmms
{

namespace
{

float axisFraction( int value )
{
	return static_cast<float>( std::clamp( value, 0, 127 ) ) / MpeSynthExpression::FullScale;
}

} // namespace


float MpeSynthExpression::pressureGain( int pressure )
{
	return 1.f + ( MaxPressureGain - 1.f ) * axisFraction( pressure );
}


float MpeSynthExpression::brightness( int timbre )
{
	return MaxBrightness * axisFraction( timbre );
}


void MpeSynthExpression::process( State& state, SampleFrame* buf, f_cnt_t frames,
	int pressure, int timbre, float sampleRate )
{
	if( buf == nullptr || frames <= 0 )
	{
		return;
	}

	const float targetGain = pressureGain( pressure );
	const float targetBrightness = brightness( timbre );

	if( !state.primed )
	{
		// The note's first block starts AT its values (a note-start expression
		// is not a fade from neutral), and the lowpass starts on the signal, so
		// the emphasis does not see a step from silence.
		state.primed = true;
		state.gain = targetGain;
		state.brightness = targetBrightness;
		state.low[0] = buf[0][0];
		state.low[1] = buf[0][1];
	}

	// One-pole lowpass coefficient for the brightness corner at this rate.
	const float rate = sampleRate > 0.f ? sampleRate : 44100.f;
	constexpr float kTwoPi = 6.28318530718f;	// not M_PI: MSVC hides it
	const float a = 1.f - std::exp( -kTwoPi * BrightnessCornerHz / rate );

	const bool neutral = state.gain == 1.f && targetGain == 1.f
		&& state.brightness == 0.f && targetBrightness == 0.f;
	if( neutral )
	{
		// Exact identity: keep the lowpass following the signal so a later
		// timbre change starts from the right state, but leave the audio alone.
		for( f_cnt_t f = 0; f < frames; ++f )
		{
			for( int c = 0; c < 2; ++c )
			{
				state.low[c] += a * ( buf[f][c] - state.low[c] );
			}
		}
		return;
	}

	const float gainStep = ( targetGain - state.gain ) / static_cast<float>( frames );
	const float brightnessStep = ( targetBrightness - state.brightness ) / static_cast<float>( frames );
	float gain = state.gain;
	float bright = state.brightness;
	for( f_cnt_t f = 0; f < frames; ++f )
	{
		gain += gainStep;
		bright += brightnessStep;
		for( int c = 0; c < 2; ++c )
		{
			const float x = buf[f][c];
			state.low[c] += a * ( x - state.low[c] );
			buf[f][c] = ( x + bright * ( x - state.low[c] ) ) * gain;
		}
	}
	// Land exactly on the targets (no float drift across blocks).
	state.gain = targetGain;
	state.brightness = targetBrightness;
}


void MpeSynthExpression::applyToNote( NotePlayHandle& note, SampleFrame* buf, f_cnt_t frames )
{
	// A note with no expression and no stream - every note of an old project,
	// every note played without MPE - is never touched.
	if( !note.hasMpeExpression() && !note.hasMpeStream() )
	{
		return;
	}
	process( note.mpeSynthState(), buf, frames, note.effectiveMpePressure(),
		note.effectiveMpeTimbre(), static_cast<float>( Engine::audioEngine()->outputSampleRate() ) );
}

} // namespace lmms
