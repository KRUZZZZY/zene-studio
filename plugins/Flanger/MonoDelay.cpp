/*
 * monodelay.cpp - defination of MonoDelay class.
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

#include "MonoDelay.h"
#include "string.h"

namespace lmms
{


MonoDelay::MonoDelay( int maxTime , int sampleRate )
{
	m_buffer = 0;
	m_maxTime = static_cast<float>(maxTime);
	m_maxLength = maxTime * sampleRate;
	m_length = static_cast<float>(m_maxLength);

	m_writeIndex = 0;
	m_feedback = 0.0f;
	setSampleRate( sampleRate );
}




MonoDelay::~MonoDelay()
{
	if( m_buffer )
	{
		delete[] m_buffer;
	}
}



void MonoDelay::tick( sample_t* sample )
{
	m_writeIndex = ( m_writeIndex + 1 ) % ( int )m_maxLength;
	int readIndex = m_writeIndex - static_cast<int>(m_length);
	if (readIndex < 0 ) { readIndex += m_maxLength; }
	float out = m_buffer[ readIndex ];
	m_buffer[ m_writeIndex ] = *sample + ( out * m_feedback );
	*sample = out;
}




void MonoDelay::setSampleRate( int sampleRate )
{
	if( m_buffer )
	{
		delete[] m_buffer;
	}

	const int newMaxLength = static_cast<int>(sampleRate * m_maxTime);
	if (m_maxLength > 0)
	{
		m_length = static_cast<float>(static_cast<double>(m_length) * newMaxLength / m_maxLength);
	}
	else
	{
		m_length = static_cast<float>(newMaxLength);
	}
	m_maxLength = newMaxLength;
	m_writeIndex = 0;

	m_buffer = new sample_t[m_maxLength];
	memset(m_buffer, 0, sizeof(sample_t) * m_maxLength);
}


} // namespace lmms
