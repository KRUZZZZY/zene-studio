/*
 * AudioDevice.cpp - base-class for audio-devices used by LMMS audio engine
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

#include <cstring>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AudioInputPath.h"

#include <algorithm>
#include <thread>

namespace lmms
{

AudioDevice::AudioDevice(const ch_cnt_t _channels, AudioEngine* _audioEngine)
	: m_supportsCapture(false)
	, m_sampleRate(_audioEngine->outputSampleRate())
	, m_channels(_channels)
	, m_audioEngine(_audioEngine)
{
}



AudioDevice::~AudioDevice()
{
	assert(!isRunning() && "device should have been stopped before being destroyed");
}

void AudioDevice::prepareCapture(f_cnt_t maxFrames, int left, int right)
{
	m_captureBus.assign(static_cast<std::size_t>(std::max<f_cnt_t>(maxFrames, 1)), SampleFrame{});
	// A negative pair member means the caller (JACK, SDL, PortAudio) passed none: the
	// configuration's pair is the honoured one, not the old hard-coded 0/1. ALSA passes the
	// pair it could clamp to its own granted width. Off the audio thread, so reading the
	// configuration here costs the capture path nothing; publishCaptured() clamps the pair
	// to the width of each block.
	const AudioInputPath::Plan plan = AudioInputPath::configuredPlan();
	m_captureLeft = std::max(left < 0 ? plan.left : left, 0);
	m_captureRight = std::max(right < 0 ? plan.right : right, 0);
}




void AudioDevice::publishCaptured(const float* interleaved, int channels, f_cnt_t frames) noexcept
{
	AudioEngine* engine = m_audioEngine;
	if (engine == nullptr || interleaved == nullptr || frames == 0 || channels <= 0 || m_captureBus.empty())
	{
		return;
	}
	// 1. The N-CHANNEL path (feature row 64): every captured channel, so a record route can
	//    select any of them - channels 2..N-1 included, which the stereo bus cannot carry.
	//    It is published FIRST and it LEADS: the stereo pair below is the SECOND
	//    representation of the SAME capture block, and the engine drains the two
	//    together (cluster 3705), so a block that reaches only one of the two
	//    paths is a block the other path is a whole period behind.
	const std::size_t staged = engine->pushInputFramesWide(interleaved, channels, frames);
	// 2. The counter record.input_get_state reports. It counts the frames the
	//    device delivered, whether or not the two paths took them (the drop
	//    counters say what was lost), so it moves before the return below.
	AudioInputPath::addCapturedFrames(static_cast<std::uint64_t>(frames));
	if (staged == 0)
	{
		// The wide path refused the block whole - a width it cannot carry, or a
		// width change while frames of the old width are still staged. Publishing
		// the stereo pair alone would leave the two paths one block apart: the
		// recorder would take the wide block in one period and this one in the
		// next. So the block goes to NEITHER path, and the wide path has counted
		// it as dropped.
		return;
	}
	// 3. The STEREO bus the rest of the engine reads: the selected pair, clamped to what
	//    this block carries (a mono device feeds its one channel to both sides). Exactly
	//    the frames the wide path took, so the two representations are one block.
	const auto width = static_cast<std::size_t>(channels);
	const auto left = static_cast<std::size_t>(std::min(m_captureLeft, channels - 1));
	const auto right = static_cast<std::size_t>(std::min(m_captureRight, channels - 1));
	const auto piece = static_cast<f_cnt_t>(m_captureBus.size());
	const auto accepted = static_cast<f_cnt_t>(staged);
	for (f_cnt_t done = 0; done < accepted; done += piece)
	{
		const f_cnt_t count = std::min(piece, accepted - done);
		for (f_cnt_t i = 0; i < count; ++i)
		{
			const std::size_t frame = static_cast<std::size_t>(done + i) * width;
			m_captureBus[static_cast<std::size_t>(i)] = SampleFrame(interleaved[frame + left], interleaved[frame + right]);
		}
		engine->pushInputFrames(m_captureBus.data(), count);
	}
}




f_cnt_t AudioDevice::captureBlockFrames() const
{
	return m_audioEngine != nullptr ? m_audioEngine->framesPerAudioBuffer() : 0;
}




void AudioDevice::startProcessing()
{
	// The capture of a device that is (re)starting has delivered nothing yet (BUGS_FOUND 11.29).
	if (m_audioEngine != nullptr) { m_audioEngine->resetInputStream(); }
	m_running.test_and_set(std::memory_order_acquire);
	startProcessingImpl();
}

void AudioDevice::stopProcessing()
{
	m_running.clear(std::memory_order_release);
	stopProcessingImpl();
	waitForOpenCallbacks();
}

void AudioDevice::waitForOpenCallbacks()
{
	// Dekker pair with CallbackScope. The clear of m_running above is ordered before the read of
	// m_openCallbacks below. A callback whose scope opened before that read is waited for here; a
	// callback that opens after it reads m_running as false in its body, because its isRunning()
	// read is ordered after its scope opened.
	std::atomic_thread_fence(std::memory_order_seq_cst);
	while (m_openCallbacks.load(std::memory_order_seq_cst) != 0)
	{
		onStopWait();
		std::this_thread::yield();
	}
}

void AudioDevice::stopProcessingThread( QThread * thread )
{
	if( !thread->wait( 30000 ) )
	{
		fprintf( stderr, "Terminating audio device thread\n" );
		thread->terminate();
		if( !thread->wait( 1000 ) )
		{
			fprintf( stderr, "Thread not terminated yet\n" );
		}
	}
}



void AudioDevice::registerPort(AudioBusHandle*)
{
}




void AudioDevice::unregisterPort(AudioBusHandle*)
{
}




void AudioDevice::renamePort(AudioBusHandle*)
{
}

int AudioDevice::convertToS16(const SampleFrame* _ab,
								const f_cnt_t _frames,
								int_sample_t * _output_buffer,
								const bool _convert_endian )
{
	if( _convert_endian )
	{
		for( f_cnt_t frame = 0; frame < _frames; ++frame )
		{
			for( ch_cnt_t chnl = 0; chnl < channels(); ++chnl )
			{
				auto temp = static_cast<int_sample_t>(AudioEngine::clip(_ab[frame][chnl]) * OUTPUT_SAMPLE_MULTIPLIER);

				( _output_buffer + frame * channels() )[chnl] =
						( temp & 0x00ff ) << 8 |
						( temp & 0xff00 ) >> 8;
			}
		}
	}
	else
	{
		for( f_cnt_t frame = 0; frame < _frames; ++frame )
		{
			for( ch_cnt_t chnl = 0; chnl < channels(); ++chnl )
			{
				(_output_buffer + frame * channels())[chnl]
					= static_cast<int_sample_t>(AudioEngine::clip(_ab[frame][chnl]) * OUTPUT_SAMPLE_MULTIPLIER);
			}
		}
	}

	return _frames * channels() * BYTES_PER_INT_SAMPLE;
}




void AudioDevice::clearS16Buffer( int_sample_t * _outbuf, const f_cnt_t _frames )
{

	assert( _outbuf != nullptr );

	memset( _outbuf, 0,  _frames * channels() * BYTES_PER_INT_SAMPLE );
}

} // namespace lmms
