/*
 * TrackRecorder.cpp - per-track capture stream for the two-track recording
 *                     prototype
 *
 * Copyright (c) 2026 LMMS developers
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

#include "TrackRecorder.h"

#include <algorithm>
#include <chrono>

#include <sndfile.h>

#include "SampleFrame.h"
#include "lmms_constants.h"

namespace lmms
{


TrackRecorder::TrackRecorder() :
	m_ring(std::make_unique<RecordRingBuffer>(RingCapacityFrames)),
	m_writeScratch(WriteBatchFrames, 0.f)
{
}


TrackRecorder::~TrackRecorder()
{
	disarm();
}




void TrackRecorder::setInputChannel(int channel) noexcept
{
	m_inputChannel.store(std::clamp(channel, 0, static_cast<int>(DEFAULT_CHANNELS) - 1),
		std::memory_order_relaxed);
}




int TrackRecorder::inputChannel() const noexcept
{
	return m_inputChannel.load(std::memory_order_relaxed);
}




bool TrackRecorder::arm(const std::string& filePath, int sampleRate, int inputChannel)
{
	bool expected = false;
	if (!m_armed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
	{
		return false; // already armed
	}

	m_ring->reset();
	m_framesPushed.store(0, std::memory_order_relaxed);
	m_framesRecorded.store(0, std::memory_order_relaxed);
	m_writeErrors.store(0, std::memory_order_relaxed);
	setInputChannel(inputChannel);
	m_filePath = filePath;

	SF_INFO info{};
	info.samplerate = sampleRate;
	info.channels = 1;
	info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
	m_sf = sf_open(filePath.c_str(), SFM_WRITE, &info);
	if (m_sf == nullptr)
	{
		m_armed.store(false, std::memory_order_release);
		return false;
	}

	m_stopRequested.store(false, std::memory_order_release);
	m_writerThread = std::thread(&TrackRecorder::writerLoop, this);
	return true;
}




void TrackRecorder::disarm()
{
	if (!m_armed.exchange(false, std::memory_order_acq_rel))
	{
		return; // not armed
	}

	m_stopRequested.store(true, std::memory_order_release);
	if (m_writerThread.joinable())
	{
		m_writerThread.join();
	}

	if (m_sf != nullptr)
	{
		sf_write_sync(m_sf);
		sf_close(m_sf);
		m_sf = nullptr;
	}
}




void TrackRecorder::processInput(const SampleFrame* input, f_cnt_t frames) noexcept
{
	if (input == nullptr || frames == 0 || !m_armed.load(std::memory_order_acquire))
	{
		return;
	}

	// Realtime-safe: only a ring-buffer store and one relaxed atomic add.
	// No allocation, no locks, no syscalls (asserted by the offline harness
	// and the RecordRingBufferTest allocation probe).
	const auto channel = m_inputChannel.load(std::memory_order_relaxed);
	const auto pushed = m_ring->writeStrided(input->data() + channel,
		DEFAULT_CHANNELS, static_cast<std::size_t>(frames));
	m_framesPushed.fetch_add(pushed, std::memory_order_relaxed);
}




void TrackRecorder::writerLoop()
{
	m_writerRunning.store(true, std::memory_order_release);

	while (!m_stopRequested.load(std::memory_order_acquire) || m_ring->available() > 0)
	{
		const auto frames = m_ring->read(m_writeScratch.data(), m_writeScratch.size());
		if (frames == 0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			continue;
		}

		const auto written = sf_writef_float(m_sf, m_writeScratch.data(),
			static_cast<sf_count_t>(frames));
		if (written < 0 || static_cast<std::size_t>(written) != frames)
		{
			m_writeErrors.fetch_add(1, std::memory_order_relaxed);
			if (written > 0)
			{
				m_framesRecorded.fetch_add(static_cast<std::uint64_t>(written),
					std::memory_order_relaxed);
			}
		}
		else
		{
			m_framesRecorded.fetch_add(static_cast<std::uint64_t>(written),
				std::memory_order_relaxed);
		}
	}

	m_writerRunning.store(false, std::memory_order_release);
}


} // namespace lmms
