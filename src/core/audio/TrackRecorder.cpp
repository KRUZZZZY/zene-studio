/*
 * TrackRecorder.cpp - per-track capture stream for the two-track recording
 *                     prototype
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

#include "TrackRecorder.h"

#include <algorithm>
#include <chrono>

#include <QFileInfo>
#include <QString>

#include <sndfile.h>

#include "RecordingJournal.h"
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
	m_inputChannel.store(inputChannelSelectable(channel) ? channel : 0,
		std::memory_order_relaxed);
}




int TrackRecorder::inputChannel() const noexcept
{
	return m_inputChannel.load(std::memory_order_relaxed);
}




void TrackRecorder::setInputChannelCapacity(int channels) noexcept
{
	m_inputChannelCapacity.store(channels < 1 ? 1 : channels, std::memory_order_relaxed);
}




int TrackRecorder::inputChannelCapacity() const noexcept
{
	return m_inputChannelCapacity.load(std::memory_order_relaxed);
}




bool TrackRecorder::inputChannelSelectable(int channel) const noexcept
{
	return channel >= 0 && channel < inputChannelCapacity();
}




bool TrackRecorder::arm(const std::string& filePath, int sampleRate, int inputChannel)
{
	// The channel is validated BEFORE the file is opened: a route that cannot
	// read the channel it was asked for must not leave a take file (or a take
	// journal) behind. setInputChannel() itself clamps and can never fail, so
	// the check has to happen here.
	if (!inputChannelSelectable(inputChannel))
	{
		return false;
	}

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

	// DEFECT 6 (feedback/grade-B-recording.md) is fixed in writerLoop(), by
	// clamping the samples in C++ before the write. It is deliberately NOT
	// fixed by sf_command(m_sf, SFC_SET_CLIPPING, ...) here, even though that is
	// the flag both export writers use (AudioFileWave.cpp, AudioFileFlac.cpp):
	// measured against libsndfile 1.2.2, enabling its clipping changes the
	// conversion of IN-RANGE negative samples too: with the flag, -0.75 comes
	// back 1 LSB lower than the same sample written without it (-1610612736 vs
	// -1610612480 in the int32 read-back scale), and -1.0 gives -2147483648 vs
	// -2147483392. This recorder has no reason to rewrite audio that was never
	// out of range.
	// Clamping in C++ keeps every in-range sample bit-identical - see
	// tests/src/core/RecordClipTest.cpp::inRangeTakeIsUnchanged - and matches
	// the mechanism this tree already uses on its other integer write path
	// (AudioDevice::convertToS16 applies AudioEngine::clip() in C++).
	//
	// sf_command(m_sf, SFC_SET_CLIPPING, nullptr, SF_TRUE) would also work; it
	// just is not free of an in-range delta, which the test measures.

	m_stopRequested.store(false, std::memory_order_release);
	// The take journal (0.3.0) BEFORE the writer thread starts: a recording in
	// progress is journalled to disk, so an exit that never reaches disarm()
	// leaves something the next start can recover FROM rather than a half-written
	// WAV nobody can identify. Written here, off the audio thread; updated by
	// writerLoop() once per second of audio (the whole of the lag bound in
	// include/RecordingJournal.h).
	m_journalTake = QString::fromStdString(filePath);
	m_journalPath = recordingjournal::journalPathFor(m_journalTake).toStdString();
	m_journalFrames = 0;
	m_journalSampleRate = sampleRate;
	m_journalIntervalFrames = recordingjournal::updateIntervalFrames(sampleRate);
	writeJournal();
	if (!QFileInfo::exists(QString::fromStdString(m_journalPath)))
	{
		// The journal is the recovery AID, not the recording: a take written into
		// a directory that refuses the side file is still recorded, and
		// journalPath() answers empty so the caller can see it is not protected.
		m_journalPath.clear();
	}

	m_writerThread = std::thread(&TrackRecorder::writerLoop, this);
	return true;
}


void TrackRecorder::writeJournal()
{
	if (m_journalPath.empty()) { return; }
	TakeJournal journal;
	journal.takePath = m_journalTake;
	journal.state = QStringLiteral("in_progress");
	journal.sampleRate = m_journalSampleRate;
	journal.channels = 1;
	journal.framesOnDisk = m_framesRecorded.load(std::memory_order_relaxed);
	journal.startTicks = -1;
	recordingjournal::write(QString::fromStdString(m_journalPath), journal);
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

	// A CLEAN stop leaves no journal (0.3.0): that is what makes "there is a
	// journal" and "the capture died" the same fact. Removed after the writer
	// thread is joined and the file is closed, so the frames the journal last
	// recorded cannot exceed what the take holds.
	if (!m_journalPath.empty())
	{
		recordingjournal::remove(QString::fromStdString(m_journalPath));
		m_journalPath.clear();
		m_journalFrames = 0;
	}
}




void TrackRecorder::processInput(const SampleFrame* input, f_cnt_t frames) noexcept
{
	if (input == nullptr)
	{
		return;
	}
	// The engine's input bus: DEFAULT_CHANNELS-wide interleaved frames, which
	// is the same memory layout as an array of SampleFrame (two adjacent
	// floats each) that this class has always relied on.
	processInputInterleaved(input->data(), static_cast<int>(DEFAULT_CHANNELS), frames);
}




void TrackRecorder::processInputInterleaved(const sample_t* interleaved, int channels,
	f_cnt_t frames) noexcept
{
	if (interleaved == nullptr || channels < 1 || frames == 0
		|| !m_armed.load(std::memory_order_acquire))
	{
		return;
	}

	// Realtime-safe: only a ring-buffer store and one relaxed atomic add.
	// No allocation, no locks, no syscalls (asserted by the offline harness
	// and the RecordRingBufferTest allocation probe).
	const auto channel = m_inputChannel.load(std::memory_order_relaxed);
	if (channel >= channels)
	{
		// This route is pointed past the width the engine just delivered (a
		// 2-channel bus feeding a route armed for channel 5). It records
		// nothing rather than reading past the end of the block, and the route
		// still reports the frames it did NOT get - see armTrack's refusal,
		// which is where such a route is normally stopped before this point.
		return;
	}
	const auto pushed = m_ring->writeStrided(interleaved + channel,
		static_cast<std::size_t>(channels), static_cast<std::size_t>(frames));
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

		// DEFECT 6: clip before the write. libsndfile does NOT clip by default
		// (SFC_GET_CLIPPING answers "off" for a fresh PCM file), so an
		// out-of-range float is converted by wrapping: +1.5 is stored as
		// -1073742336 and +2.0 as -512, i.e. a hot JACK take (AudioJack.cpp
		// hands over unbounded floats) is written as full-scale sign flips and
		// near-silence dropouts instead of clipped peaks. Clamping here, on the
		// disk-writer thread, costs the audio thread nothing and leaves every
		// in-range sample bit-identical (see RecordClipTest).
		for (std::size_t i = 0; i < frames; ++i)
		{
			m_writeScratch[i] = std::clamp(m_writeScratch[i], sample_t{-1}, sample_t{1});
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

		// Keep the take journal current (0.3.0). Once per second of audio, on the
		// disk-writer thread - the one thread that knows what has actually
		// reached the file - so the recovery offer's guaranteed count lags real
		// disk state by at most one update interval.
		if (m_framesRecorded.load(std::memory_order_relaxed) - m_journalFrames
			>= m_journalIntervalFrames)
		{
			m_journalFrames = m_framesRecorded.load(std::memory_order_relaxed);
			writeJournal();
		}
	}

	m_writerRunning.store(false, std::memory_order_release);
}


} // namespace lmms
