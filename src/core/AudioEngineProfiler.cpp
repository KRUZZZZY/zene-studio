/*
 * AudioEngineProfiler.cpp - class for profiling performance of AudioEngine
 *
 * Copyright (c) 2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "AudioEngineProfiler.h"

#include <cstdint>
#include <thread>

namespace lmms
{

AudioEngineProfiler::AudioEngineProfiler() :
	m_periodTimer(),
	m_cpuLoad( 0 ),
	m_outputFile()
{
}

AudioEngineProfiler::~AudioEngineProfiler()
{
	stopWriter();
}

void AudioEngineProfiler::finishPeriod( sample_rate_t sampleRate, f_cnt_t framesPerPeriod )
{
	// Time taken to process all data and fill the audio buffer.
	const unsigned int periodElapsed = m_periodTimer.elapsed();
	// Maximum time the processing can take before causing buffer underflow. Convert to us.
	const uint64_t timeLimit = static_cast<uint64_t>(1000000) * framesPerPeriod / sampleRate;

	// Compute new overall CPU load and apply exponential averaging.
	// The result is used for overload detection in AudioEngine::criticalXRuns()
	// → the weight of a new sample must be high enough to allow relatively fast changes!
	const auto newCpuLoad = 100.f * periodElapsed / timeLimit;
	m_cpuLoad = newCpuLoad * 0.1f + m_cpuLoad * 0.9f;

	// Compute detailed load analysis. Can use stronger averaging to get more stable readout.
	for (std::size_t i = 0; i < DetailCount; i++)
	{
		const auto newLoad = 100.f * m_detailTime[i] / timeLimit;
		const auto oldLoad = m_detailLoad[i].load(std::memory_order_relaxed);
		m_detailLoad[i].store(newLoad * 0.05f + oldLoad * 0.95f, std::memory_order_relaxed);
	}

	// Hand the measurement to a bounded buffer; a non-audio thread drains it.
	const std::uint64_t writeIndex = m_writeIndex.load(std::memory_order_relaxed);
	const std::uint64_t readIndex = m_readIndex.load(std::memory_order_acquire);
	if (writeIndex - readIndex < BufferCapacity)
	{
		m_buffer[writeIndex % BufferCapacity] = periodElapsed;
		m_writeIndex.store(writeIndex + 1, std::memory_order_release);
	}
}

void AudioEngineProfiler::writerLoop()
{
	if (!m_outputFileName.isEmpty())
	{
		m_outputFile.setFileName(m_outputFileName);
		if (!m_outputFile.open(QFile::WriteOnly | QFile::Truncate))
		{
			return;
		}
	}

	while (true)
	{
		bool wrote = false;
		const std::uint64_t readIndex = m_readIndex.load(std::memory_order_relaxed);
		const std::uint64_t writeIndex = m_writeIndex.load(std::memory_order_acquire);
		std::uint64_t current = readIndex;
		while (current < writeIndex)
		{
			if (m_outputFile.isOpen())
			{
				m_outputFile.write(QString("%1\n").arg(m_buffer[current % BufferCapacity]).toLatin1());
			}
			current++;
			m_readIndex.store(current, std::memory_order_release);
			wrote = true;
		}
		if (!m_running.load(std::memory_order_relaxed) &&
			m_readIndex.load(std::memory_order_acquire) == m_writeIndex.load(std::memory_order_acquire))
		{
			break;
		}
		if (!wrote)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

	if (m_outputFile.isOpen())
	{
		m_outputFile.close();
	}
}

void AudioEngineProfiler::stopWriter()
{
	m_running.store(false, std::memory_order_relaxed);
	if (m_writerThread.joinable())
	{
		m_writerThread.join();
	}
}

void AudioEngineProfiler::setOutputFile( const QString& outputFile )
{
	stopWriter();
	m_outputFileName = outputFile;
	m_running.store(true, std::memory_order_relaxed);
	m_writerThread = std::thread(&AudioEngineProfiler::writerLoop, this);
}

} // namespace lmms
