/*
 * AudioSampleRecorder.cpp - device-class that implements recording
 *                           audio-buffers into RAM
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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


#include "AudioSampleRecorder.h"
#include "SampleBuffer.h"

#include <limits>


namespace lmms
{


AudioSampleRecorder::AudioSampleRecorder( const ch_cnt_t _channels,
							bool & _success_ful,
							AudioEngine * _audioEngine ) :
	AudioDevice( _channels, _audioEngine ),
	m_buffers()
{
	_success_ful = true;
}




AudioSampleRecorder::~AudioSampleRecorder()
{
	while( !m_buffers.empty() )
	{
		delete[] m_buffers.front().first;
		m_buffers.erase( m_buffers.begin() );
	}
}




f_cnt_t AudioSampleRecorder::framesRecorded() const
{
	f_cnt_t frames = 0;
	for (const auto& buffer : m_buffers)
	{
		// Saturating, not wrapping: a total past what f_cnt_t can name is reported as the
		// largest nameable count, so no caller reads a small number out of a big take.
		if( buffer.second > std::numeric_limits<f_cnt_t>::max() - frames )
		{
			return std::numeric_limits<f_cnt_t>::max();
		}
		frames += buffer.second;
	}
	return frames;
}


bool AudioSampleRecorder::totalFramesWithin( const f_cnt_t* counts, const std::size_t n,
						std::size_t capacity, std::size_t& total ) noexcept
{
	std::size_t sum = 0;
	for( std::size_t i = 0; i < n; ++i )
	{
		// The room left, and the only comparison needed: sum never passes capacity, so an
		// oversized block is refused before it can add past the destination and wrap.
		if( counts[i] > capacity - sum )
		{
			return false;
		}
		sum += static_cast<std::size_t>( counts[i] );
	}
	total = sum;
	return true;
}


std::shared_ptr<const SampleBuffer> AudioSampleRecorder::createSampleBuffer()
{
	// The blocks' frame counts in one array: the wide, checked total below reads a plain
	// f_cnt_t range, which is what a unit test can hand it without the recording.
	std::vector<f_cnt_t> counts;
	counts.reserve( static_cast<std::size_t>( m_buffers.size() ) );
	for( const auto& buffer : m_buffers )
	{
		counts.push_back( buffer.second );
	}

	std::vector<SampleFrame> bigBuffer;
	std::size_t frames = 0;
	// The destination holds one SampleFrame per frame and is indexed by its signed
	// difference_type: a total either of them cannot represent is refused before anything is
	// allocated, so the copy below never indexes past what the vector holds.
	if( !totalFramesWithin( counts.data(), counts.size(), bigBuffer.max_size(), frames ) )
	{
		return nullptr;
	}
	bigBuffer.resize( frames );

	// now copy all buffers into big buffer
	std::size_t framesCopied = 0;
	for (const auto& [buf, numFrames] : m_buffers)
	{
		std::copy_n(buf, numFrames,
			bigBuffer.begin() + static_cast<std::ptrdiff_t>(framesCopied));
		framesCopied += static_cast<std::size_t>(numFrames);
	}

	// create according sample-buffer out of big buffer
	return std::make_shared<const SampleBuffer>(std::move(bigBuffer), sampleRate());
}

void AudioSampleRecorder::writeBuffer(const SampleFrame* _ab, const f_cnt_t _frames)
{
	auto buf = new SampleFrame[_frames];
	for( f_cnt_t frame = 0; frame < _frames; ++frame )
	{
		for( ch_cnt_t chnl = 0; chnl < DEFAULT_CHANNELS; ++chnl )
		{
			buf[frame][chnl] = _ab[frame][chnl];
		}
	}
	m_buffers.push_back( qMakePair( buf, _frames ) );
}


} // namespace lmms
