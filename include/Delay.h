/*
 * Delay.h - Delay effect objects to use as building blocks in DSP
 *
 * Copyright (c) 2014 Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_DELAY_H
#define LMMS_DELAY_H

#include <array>
#include <cmath>
#include <limits>

#include "LmmsTypes.h"

namespace lmms
{

namespace detail
{
// Split a delay in frames into the whole-frame tap and the interpolation weight.
// The delay clamps to one frame.  The weight belongs to the older tap so that
// update() can lerp from the older ring slot toward the next one.
inline void splitDelay( double delay, int& whole, double& frac )
{
	const double d = delay < 1.0 ? 1.0 : delay;
	const double wholeFrames = std::floor( d );
	whole = static_cast<int>( wholeFrames );
	frac = 1.0 - ( d - wholeFrames );
}
} // namespace detail

// brief usage 

// Classes:

// CombFeedback: a feedback comb filter - basically a simple delay line, makes a comb shape in the freq response
// CombFeedfwd: a feed-forward comb filter - an "inverted" comb filter, can be combined with CombFeedback to create a net allpass if negative gain is used
// CombFeedbackDualtap: same as CombFeedback but takes two delay values
// AllpassDelay: an allpass delay - combines feedback and feed-forward - has flat frequency response

// all classes are templated with channel count, any arbitrary channel count can be used for each fx

// Methods (for all classes):

// setDelay sets delay amount in frames. It's up to you to make this samplerate-agnostic. 
// Delay d gives output[n] = lerp of the inputs at n-floor(d) and n-floor(d)-1, with a minimum of 1 frame.
// CombFeedbackDualTap is a special case: it requires 2 delay times

// setMaxDelay (re)sets the maximum allowed delay, in frames
// NOTE: for performance reasons, there's no bounds checking at setDelay, so make sure you set maxDelay >= delay!

// clearHistory clears the delay buffer

// setGain sets the feedback/feed-forward gain, in linear amplitude, negative values are allowed
// 1.0 is full feedback/feed-forward, -1.0 is full negative feedback/feed-forward

// update runs the fx for one frame - takes as arguments input and number of channel to run, returns output

template<ch_cnt_t CHANNELS>
class CombFeedback
{
public:
	using frame = std::array<double, CHANNELS>;

	CombFeedback( int maxDelay ) :
		m_size( maxDelay ),
		m_gain( 0.0 ),
		m_delay( 0 ),
		m_fraction( 0.0 )
	{
		m_buffer = new frame[maxDelay];
		memset( m_buffer, 0, sizeof( frame ) * maxDelay );
	}
	virtual ~CombFeedback()
	{
		delete[] m_buffer;
	}
	
	inline void setMaxDelay( int maxDelay )
	{
		if( maxDelay > m_size )
		{
			delete[] m_buffer;
			m_buffer = new frame[maxDelay];
			memset( m_buffer, 0, sizeof( frame ) * maxDelay );
		}
		m_size = maxDelay;
		for (int& position : m_positions)
		{
			position %= m_size;
		}
	}
	
	inline void clearHistory()
	{
		memset( m_buffer, 0, sizeof( frame ) * m_size );
	}
	
	inline void setDelay( double delay )
	{
		if( !std::isfinite( delay ) || delay < 0.0 || delay > static_cast<double>( std::numeric_limits<int>::max() ) )
		{
			return;
		}

		detail::splitDelay( delay, m_delay, m_fraction );
	}
	
	inline void setGain( double gain )
	{
		m_gain = gain;
	}
	
	inline double update( double in, ch_cnt_t ch )
	{
		int readPos = m_positions[ch] - m_delay;
		if( readPos < 0 ) { readPos += m_size; }
		
		const double y = std::lerp(m_buffer[readPos][ch], m_buffer[(readPos + 1) % m_size][ch], m_fraction);
		
		m_positions[ch] = (m_positions[ch] + 1) % m_size;
		
		m_buffer[m_positions[ch]][ch] = in + m_gain * y;
		return y;
	}

private:
	frame * m_buffer;
	int m_size;
	std::array<int, CHANNELS> m_positions{};
	double m_gain;
	int m_delay;
	double m_fraction;
};


template<ch_cnt_t CHANNELS>
class CombFeedfwd
{
public:
	using frame = std::array<double, CHANNELS>;

	CombFeedfwd( int maxDelay ) :
		m_size( maxDelay ),
		m_gain( 0.0 ),
		m_delay( 0 ),
		m_fraction( 0.0 )
	{
		m_buffer = new frame[maxDelay];
		memset( m_buffer, 0, sizeof( frame ) * maxDelay );
	}
	virtual ~CombFeedfwd()
	{
		delete[] m_buffer;
	}
	
	inline void setMaxDelay( int maxDelay )
	{
		if( maxDelay > m_size )
		{
			delete[] m_buffer;
			m_buffer = new frame[maxDelay];
			memset( m_buffer, 0, sizeof( frame ) * maxDelay );
		}
		m_size = maxDelay;
		for (int& position : m_positions)
		{
			position %= m_size;
		}
	}
	
	inline void clearHistory()
	{
		memset( m_buffer, 0, sizeof( frame ) * m_size );
	}
	
	inline void setDelay( double delay )
	{
		detail::splitDelay( delay, m_delay, m_fraction );
	}
	
	inline void setGain( double gain )
	{
		m_gain = gain;
	}
	
	inline double update( double in, ch_cnt_t ch )
	{
		int readPos = m_positions[ch] - m_delay;
		if( readPos < 0 ) { readPos += m_size; }
		
		const double y = std::lerp(m_buffer[readPos][ch], m_buffer[(readPos + 1) % m_size][ch], m_fraction) + in * m_gain;
		
		m_positions[ch] = (m_positions[ch] + 1) % m_size;
		
		m_buffer[m_positions[ch]][ch] = in;
		return y;
	}

private:
	frame * m_buffer;
	int m_size;
	std::array<int, CHANNELS> m_positions{};
	double m_gain;
	int m_delay;
	double m_fraction;
};


template<ch_cnt_t CHANNELS>
class CombFeedbackDualtap
{
public:
	using frame = std::array<double, CHANNELS>;

	CombFeedbackDualtap( int maxDelay ) :
		m_size( maxDelay ),
		m_gain( 0.0 ),
		m_delay1( 0 ),
		m_delay2( 0 ),
		m_fraction1( 0.0 ),
		m_fraction2( 0.0 )
	{
		m_buffer = new frame[maxDelay];
		memset( m_buffer, 0, sizeof( frame ) * maxDelay );
	}
	virtual ~CombFeedbackDualtap()
	{
		delete[] m_buffer;
	}
	
	inline void setMaxDelay( int maxDelay )
	{
		if( maxDelay > m_size )
		{
			delete[] m_buffer;
			m_buffer = new frame[maxDelay];
			memset( m_buffer, 0, sizeof( frame ) * maxDelay );
		}
		m_size = maxDelay;
		for (int& position : m_positions)
		{
			position %= m_size;
		}
	}
	
	inline void clearHistory()
	{
		memset( m_buffer, 0, sizeof( frame ) * m_size );
	}
	
	inline void setDelays( double delay1, double delay2 )
	{
		detail::splitDelay( delay1, m_delay1, m_fraction1 );
		detail::splitDelay( delay2, m_delay2, m_fraction2 );
	}
	
	inline void setGain( double gain )
	{
		m_gain = gain;
	}
	
	inline double update( double in, ch_cnt_t ch )
	{
		int readPos1 = m_positions[ch] - m_delay1;
		if( readPos1 < 0 ) { readPos1 += m_size; }
		
		int readPos2 = m_positions[ch] - m_delay2;
		if( readPos2 < 0 ) { readPos2 += m_size; }
		
		const double y = std::lerp(m_buffer[readPos1][ch], m_buffer[(readPos1 + 1) % m_size][ch], m_fraction1)
			+ std::lerp(m_buffer[readPos2][ch], m_buffer[(readPos2 + 1) % m_size][ch], m_fraction2);
		
		m_positions[ch] = (m_positions[ch] + 1) % m_size;
		
		m_buffer[m_positions[ch]][ch] = in + m_gain * y;
		return y;
	}

private:
	frame * m_buffer;
	int m_size;
	std::array<int, CHANNELS> m_positions{};
	double m_gain;
	int m_delay1;
	int m_delay2;
	double m_fraction1;
	double m_fraction2;
};


template<ch_cnt_t CHANNELS>
class AllpassDelay
{
public:
	using frame = std::array<double, CHANNELS>;

	AllpassDelay( int maxDelay ) :
		m_size( maxDelay ),
		m_gain( 0.0 ),
		m_delay( 0 ),
		m_fraction( 0.0 )
	{
		m_buffer = new frame[maxDelay];
		memset( m_buffer, 0, sizeof( frame ) * maxDelay );
	}
	virtual ~AllpassDelay()
	{
		delete[] m_buffer;
	}
	
	inline void setMaxDelay( int maxDelay )
	{
		if( maxDelay > m_size )
		{
			delete[] m_buffer;
			m_buffer = new frame[maxDelay];
			memset( m_buffer, 0, sizeof( frame ) * maxDelay );
		}
		m_size = maxDelay;
		for (int& position : m_positions)
		{
			position %= m_size;
		}
	}
	
	inline void clearHistory()
	{
		memset( m_buffer, 0, sizeof( frame ) * m_size );
	}
	
	inline void setDelay( double delay )
	{
		detail::splitDelay( delay, m_delay, m_fraction );
	}
	
	inline void setGain( double gain )
	{
		m_gain = gain;
	}
	
	inline double update( double in, ch_cnt_t ch )
	{
		int readPos = m_positions[ch] - m_delay;
		if( readPos < 0 ) { readPos += m_size; }
		
		const double y = std::lerp(m_buffer[readPos][ch], m_buffer[(readPos + 1) % m_size][ch], m_fraction) + in * -m_gain;
		const double x = in + m_gain * y;
		
		m_positions[ch] = (m_positions[ch] + 1) % m_size;
		
		m_buffer[m_positions[ch]][ch] = x;
		return y;
	}

private:
	frame * m_buffer;
	int m_size;
	std::array<int, CHANNELS> m_positions{};
	double m_gain;
	int m_delay;
	double m_fraction;	
};

// convenience typedefs for stereo effects
using StereoCombFeedback = CombFeedback<2>;
using StereoCombFeedfwd = CombFeedfwd<2>;
using StereoCombFeedbackDualtap = CombFeedbackDualtap<2>;
using StereoAllpassDelay = AllpassDelay<2>;

} // namespace lmms

#endif // LMMS_DELAY_H
