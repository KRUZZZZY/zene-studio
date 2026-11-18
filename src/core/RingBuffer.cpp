/*
 * RingBuffer.cpp - an effective and flexible implementation of a ringbuffer for LMMS
 *
 * Copyright (c) 2014 Vesa Kivimäki
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "RingBuffer.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "MixHelpers.h"

#include <limits>
#include <stdexcept>
namespace lmms
{

namespace
{

std::size_t positionAfterOffset(std::size_t position, std::int64_t offset, std::size_t size)
{
	if (offset >= 0)
	{
		const auto advance = static_cast<std::size_t>(static_cast<std::uint64_t>(offset) % size);
		const std::size_t untilWrap = size - position;
		return advance < untilWrap ? position + advance : advance - untilWrap;
	}

	const auto magnitude = static_cast<std::uint64_t>(-(offset + 1)) + 1;
	const auto rewind = static_cast<std::size_t>(magnitude % size);
	return position >= rewind ? position - rewind : size - (rewind - position);
}

f_cnt_t checkedFramesFromMilliseconds(float milliseconds, sample_rate_t samplerate)
{
	if (!std::isfinite(milliseconds) || milliseconds < 0.0f)
	{
		throw std::invalid_argument("RingBuffer size must be finite and nonnegative");
	}

	const double frames = std::ceil(static_cast<double>(milliseconds) * samplerate * 0.001);
	const double frameLimit = std::ldexp(1.0, std::numeric_limits<f_cnt_t>::digits);
	if (frames >= frameLimit)
	{
		throw std::length_error("RingBuffer size exceeds the frame-count range");
	}
	return static_cast<f_cnt_t>(frames);
}

size_t checkedBufferSize(f_cnt_t requestedFrames, f_cnt_t periodFrames)
{
	if (requestedFrames > std::numeric_limits<f_cnt_t>::max() - periodFrames)
	{
		throw std::length_error("RingBuffer size plus period exceeds the frame-count range");
	}

	const f_cnt_t bufferFrames = requestedFrames + periodFrames;
	if (bufferFrames > std::numeric_limits<size_t>::max() / sizeof(SampleFrame))
	{
		throw std::length_error("RingBuffer allocation size exceeds the addressable range");
	}
	return static_cast<size_t>(bufferFrames);
}

f_cnt_t checkedRateScaledFrames(f_cnt_t frames, sample_rate_t newRate, sample_rate_t oldRate)
{
	if (oldRate == 0)
	{
		throw std::invalid_argument("RingBuffer cannot rescale from a zero sample rate");
	}

	const f_cnt_t wholeFrames = frames / oldRate;
	const f_cnt_t remainderFrames = frames % oldRate;
	const f_cnt_t frameLimit = std::numeric_limits<f_cnt_t>::max();
	if (newRate != 0 && wholeFrames > frameLimit / newRate)
	{
		throw std::length_error("RingBuffer sample-rate-scaled size exceeds the frame-count range");
	}

	const f_cnt_t scaledWholeFrames = wholeFrames * newRate;
	const f_cnt_t remainderNumerator = remainderFrames * newRate;
	const f_cnt_t roundedRemainderFrames = remainderNumerator / oldRate
		+ (remainderNumerator % oldRate != 0);
	if (scaledWholeFrames > frameLimit - roundedRemainderFrames)
	{
		throw std::length_error("RingBuffer sample-rate-scaled size exceeds the frame-count range");
	}
	return scaledWholeFrames + roundedRemainderFrames;
}

} // namespace


RingBuffer::RingBuffer( f_cnt_t size ) : 
	m_fpp( Engine::audioEngine()->framesPerPeriod() ),
	m_samplerate( Engine::audioEngine()->outputSampleRate() ),
	m_size( checkedBufferSize(size, m_fpp) )
{
	m_buffer = new SampleFrame[ m_size ];
	zeroSampleFrames(m_buffer, m_size);
	m_position = 0;
}


RingBuffer::RingBuffer( float size ) : 
	m_fpp( Engine::audioEngine()->framesPerPeriod() ),
	m_samplerate( Engine::audioEngine()->outputSampleRate() ),
	m_size( checkedBufferSize(checkedFramesFromMilliseconds(size, m_samplerate), m_fpp) )
{
	m_buffer = new SampleFrame[ m_size ];
	zeroSampleFrames(m_buffer, m_size);
	m_position = 0;
	setSamplerateAware( true );
	//qDebug( "m_size %d, m_position %d", m_size, m_position );
}


RingBuffer::~RingBuffer()
{
	delete[] m_buffer;
}


void RingBuffer::reset()
{
	zeroSampleFrames(m_buffer, m_size);
	m_position = 0;
}


void RingBuffer::changeSize( f_cnt_t size )
{
	const size_t newSize = checkedBufferSize(size, m_fpp);
	SampleFrame* newBuffer = new SampleFrame[ newSize ];
	zeroSampleFrames(newBuffer, newSize);
	SampleFrame* tmp = m_buffer;
	m_size = newSize;
	m_buffer = newBuffer;
	m_position = 0;
	delete[] tmp;
}


void RingBuffer::changeSize( float size )
{
	changeSize( checkedFramesFromMilliseconds(size, m_samplerate) );
}


void RingBuffer::setSamplerateAware( bool b )
{
	if( b )
	{
		connect( Engine::audioEngine(), SIGNAL(sampleRateChanged()), this, SLOT(updateSamplerate()), Qt::UniqueConnection );
	}
	else
	{
		disconnect( Engine::audioEngine(), SIGNAL(sampleRateChanged()), this, SLOT(updateSamplerate()));
	}
}


void RingBuffer::advance()
{
	m_position = ( m_position + m_fpp ) % m_size;
}


void RingBuffer::movePosition( std::int64_t amount )
{
	m_position = static_cast<unsigned int>(positionAfterOffset(m_position, amount, m_size));
}


void RingBuffer::movePosition( float amount )
{
	movePosition( msToFrameOffset( amount ) );
}


void RingBuffer::pop( SampleFrame* dst )
{
	if( m_position + m_fpp <= m_size ) // we won't go over the edge so we can just memcpy here
	{
		memcpy( dst, & m_buffer [ m_position ], m_fpp * sizeof( SampleFrame ) );
		zeroSampleFrames(&m_buffer[m_position], m_fpp);
	}
	else
	{
		f_cnt_t first = m_size - m_position;
		f_cnt_t second = m_fpp - first;
		
		memcpy( dst, & m_buffer [ m_position ], first * sizeof( SampleFrame ) );
		zeroSampleFrames(&m_buffer[m_position], first);
		
		memcpy( & dst [first], m_buffer, second * sizeof( SampleFrame ) );
		zeroSampleFrames(m_buffer, second);
	}
	
	m_position = ( m_position + m_fpp ) % m_size;
}


void RingBuffer::read( SampleFrame* dst, std::int64_t offset )
{
	const std::size_t pos = positionAfterOffset(m_position, offset, m_size);
	
	if( pos + m_fpp <= m_size ) // we won't go over the edge so we can just memcpy here
	{
		memcpy( dst, & m_buffer [pos], m_fpp * sizeof( SampleFrame ) );
	}
	else
	{
		f_cnt_t first = m_size - pos;
		f_cnt_t second = m_fpp - first;
		
		memcpy( dst, & m_buffer [pos], first * sizeof( SampleFrame ) );
		
		memcpy( & dst [first], m_buffer, second * sizeof( SampleFrame ) );
	}
}


void RingBuffer::read( SampleFrame* dst, float offset )
{
	read( dst, msToFrameOffset( offset ) );
}


void RingBuffer::read( SampleFrame* dst, std::int64_t offset, f_cnt_t length )
{
	const std::size_t pos = positionAfterOffset(m_position, offset, m_size);
	
	if( pos + length <= m_size ) // we won't go over the edge so we can just memcpy here
	{
		memcpy( dst, & m_buffer [pos], length * sizeof( SampleFrame ) );
	}
	else
	{
		f_cnt_t first = m_size - pos;
		f_cnt_t second = length - first;
		
		memcpy( dst, & m_buffer [pos], first * sizeof( SampleFrame ) );
		
		memcpy( & dst [first], m_buffer, second * sizeof( SampleFrame ) );
	}
}


void RingBuffer::read( SampleFrame* dst, float offset, f_cnt_t length )
{
	read( dst, msToFrameOffset( offset ), length );
}


void RingBuffer::write( SampleFrame* src, f_cnt_t offset, f_cnt_t length )
{
	const f_cnt_t pos = ( m_position + offset ) % m_size;
	if( length == 0 ) { length = m_fpp; }
	
	if( pos + length <= m_size ) // we won't go over the edge so we can just memcpy here
	{
		memcpy( & m_buffer [pos], src, length * sizeof( SampleFrame ) );
	}
	else
	{
		f_cnt_t first = m_size - pos;
		f_cnt_t second = length - first;

		memcpy( & m_buffer [pos], src, first * sizeof( SampleFrame ) );
		
		memcpy( m_buffer, & src [first], second * sizeof( SampleFrame ) );
	}
}


void RingBuffer::write( SampleFrame* src, float offset, f_cnt_t length )
{
	write( src, msToFrames( offset ), length );
}


void RingBuffer::writeAdding( SampleFrame* src, f_cnt_t offset, f_cnt_t length )
{
	const f_cnt_t pos = ( m_position + offset ) % m_size;
	if( length == 0 ) { length = m_fpp; }
	
	if( pos + length <= m_size ) // we won't go over the edge so we can just memcpy here
	{
		MixHelpers::add( & m_buffer [pos], src, length );
	}
	else
	{
		f_cnt_t first = m_size - pos;
		f_cnt_t second = length - first;

		MixHelpers::add( & m_buffer[pos], src, first );
		
		MixHelpers::add( m_buffer, & src[first], second );
	}
}


void RingBuffer::writeAdding( SampleFrame* src, float offset, f_cnt_t length )
{
	writeAdding( src, msToFrames( offset ), length );
}


void RingBuffer::writeAddingMultiplied( SampleFrame* src, f_cnt_t offset, f_cnt_t length, float level )
{
	const f_cnt_t pos = ( m_position + offset ) % m_size;
	//qDebug( "pos %d m_pos %d ofs %d siz %d", pos, m_position, offset, m_size );
	if( length == 0 ) { length = m_fpp; }
	
	if( pos + length <= m_size ) // we won't go over the edge so we can just memcpy here
	{
		MixHelpers::addMultiplied( & m_buffer[pos], src, level, length );
	}
	else
	{
		f_cnt_t first = m_size - pos;
		f_cnt_t second = length - first;

		MixHelpers::addMultiplied( & m_buffer[pos], src, level, first );
		
		MixHelpers::addMultiplied( m_buffer, & src [first], level, second );
	}
}


void RingBuffer::writeAddingMultiplied( SampleFrame* src, float offset, f_cnt_t length, float level )
{
	f_cnt_t ofs = msToFrames( offset );
	writeAddingMultiplied( src, ofs, length, level );
}


void RingBuffer::writeSwappedAddingMultiplied( SampleFrame* src, f_cnt_t offset, f_cnt_t length, float level )
{
	const f_cnt_t pos = ( m_position + offset ) % m_size;
	if( length == 0 ) { length = m_fpp; }
	
	if( pos + length <= m_size ) // we won't go over the edge so we can just memcpy here
	{
		MixHelpers::addSwappedMultiplied( & m_buffer [pos], src, level, length );
	}
	else
	{
		f_cnt_t first = m_size - pos;
		f_cnt_t second = length - first;

		MixHelpers::addSwappedMultiplied( & m_buffer [pos], src, level, first );
		
		MixHelpers::addSwappedMultiplied( m_buffer, & src [first], level, second );
	}
}


void RingBuffer::writeSwappedAddingMultiplied( SampleFrame* src, float offset, f_cnt_t length, float level )
{
	writeSwappedAddingMultiplied( src, msToFrames( offset ), length, level );
}


void RingBuffer::updateSamplerate()
{
	const sample_rate_t newSamplerate = Engine::audioEngine()->outputSampleRate();
	size_t newSize;
	SampleFrame* newBuffer;
	try
	{
		const f_cnt_t requestedFrames = checkedRateScaledFrames(m_size - m_fpp, newSamplerate, m_samplerate);
		newSize = checkedBufferSize(requestedFrames, m_fpp);
		newBuffer = new SampleFrame[newSize];
	}
	catch (const std::exception&)
	{
		return;
	}
	zeroSampleFrames(newBuffer, newSize);
	SampleFrame* oldBuffer = m_buffer;
	m_size = newSize;
	m_buffer = newBuffer;
	m_samplerate = newSamplerate;
	m_position = 0;
	delete[] oldBuffer;
}


} // namespace lmms
