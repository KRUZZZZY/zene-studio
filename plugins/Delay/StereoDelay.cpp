/*
 * stereodelay.cpp - defination of StereoDelay class.
 *
 * Copyright (c) 2014 David French <dave/dot/french3/at/googlemail/dot/com>
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

#include "StereoDelay.h"

#include "SampleFrame.h"

#include <cstdint>
#include <limits>

namespace lmms
{


StereoDelay::StereoDelay( int maxTime, int sampleRate )
{
	m_buffer = 0;
	m_maxTime = static_cast<float>(maxTime);
	// Size the first ring in 64 bits: maxTime * sampleRate can overflow an int.
	// A positive product that fits an int is kept; anything else starts empty.
	const std::int64_t frames = static_cast<std::int64_t>(maxTime) * sampleRate;
	m_maxLength = ( frames >= 1 && frames <= std::numeric_limits<int>::max() )
		? static_cast<int>(frames) : 0;
	m_length = static_cast<float>(m_maxLength);

	m_writeIndex = 0;
	m_feedback = 0.0f;
	setSampleRate( sampleRate );
	if( !m_buffer )
	{
		// setSampleRate refused the rate, so there is no ring and no length.
		m_maxLength = 0;
		m_length = 0.0f;
	}
}




StereoDelay::~StereoDelay()
{
	if( m_buffer )
	{
		delete[] m_buffer;
	}
}




void StereoDelay::tick( SampleFrame& frame )
{
	// No ring exists when the constructor was given a refused rate; pass through.
	if( !m_buffer )
	{
		return;
	}

	m_writeIndex = ( m_writeIndex + 1 ) % ( int )m_maxLength;
	int readIndex = m_writeIndex - static_cast<int>(m_length);
	if (readIndex < 0 ) { readIndex += m_maxLength; }
	float lOut = m_buffer[ readIndex ][ 0 ];
	float rOut = m_buffer[ readIndex ] [1 ];
	m_buffer[ m_writeIndex ][ 0 ] = frame[ 0 ] + ( lOut * m_feedback );
	m_buffer[ m_writeIndex ][ 1 ] = frame[ 1 ] + ( rOut * m_feedback );
	frame[ 0 ] = lOut;
	frame[ 1 ] = rOut;
}






void StereoDelay::setSampleRate( int sampleRate )
{
	// A ring above 2^31 frames cannot be indexed by an int. Refuse it before the
	// current ring is touched, so the delay keeps its last good ring.
	const float bufferSizeF = sampleRate * m_maxTime;
	if( bufferSizeF >= 2147483648.0f ) // 2^31
	{
		return;
	}

	if( m_buffer )
	{
		delete[] m_buffer;
	}

	// tick() uses the ring length as a modulo divisor, so the ring must hold at
	// least one frame even when maxTime or sampleRate is zero or negative. The
	// float is only converted when it is in range; a negative one would not be.
	const int bufferSize = bufferSizeF >= 1.0f ? static_cast<int>(bufferSizeF) : 1;
	if (m_maxLength > 0)
	{
		m_length *= static_cast<float>(bufferSize) / static_cast<float>(m_maxLength);
	}
	m_maxLength = bufferSize;
	if (m_length > static_cast<float>(m_maxLength))
	{
		m_length = static_cast<float>(m_maxLength);
	}
	// A negative length would make tick() read past the end of the ring.
	if (m_length < 0.0f)
	{
		m_length = 0.0f;
	}
	m_writeIndex = 0;

	m_buffer = new SampleFrame[bufferSize];
	for( int i = 0 ; i < bufferSize ; i++)
	{
		m_buffer[i][0] = 0.0;
		m_buffer[i][1] = 0.0;
	}
}


} // namespace lmms
