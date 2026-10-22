/* Analyzer.h - declaration of Analyzer class.
 *
 * Copyright (c) 2019 Martin Pavelek <he29/dot/HS/at/gmail/dot/com>
 *
 * Based partially on Eq plugin code,
 * Copyright (c) 2014-2017, David French <dave/dot/french3/at/googlemail/dot/com>
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

#ifndef ANALYZER_H
#define ANALYZER_H

#ifdef SA_DEBUG
	#include <atomic>
	#include <chrono>
	#include <cstdint>
	#include <memory>
	#include <QTimer>
#endif

#include "AudioPlugin.h"
#include "DataprocLauncher.h"
#include "LocklessRingBuffer.h"
#include "SaControls.h"
#include "SaProcessor.h"

namespace lmms
{


//! Top level class; handles LMMS interface and feeds data to the data processor.
class Analyzer
	: public AudioPluginExt<Effect, AudioPortsSettings {
		.kind = AudioDataKind::F32,
		.interleaved = true,
		.inputs = 2,
		.outputs = 0,
		.inplace = true,
		.buffered = false
	}>
{
public:
	Analyzer(Model *parent, const Descriptor::SubPluginFeatures::Key *key);
	~Analyzer() override;

	EffectControls *controls() override {return &m_controls;}

	SaProcessor *getProcessor() {return &m_processor;}

	#ifdef SA_DEBUG
	//! Realtime-safe collector for the SA_DEBUG audio-thread timing report.
	//!
	//! The audio thread records one measurement per callback into a lock-free
	//! single-producer queue. The GUI thread closes the interval that ends now
	//! (request()) and takes the measurements of that interval (take()).
	//!
	//! The count, the sum and the peak of a report are computed by the GUI
	//! thread from exactly the measurements of one interval, so one report can
	//! never mix samples from two intervals: a callback that lands after the
	//! request belongs to the next interval. The measurements of an interval
	//! also stay reportable when no callback follows the request, because the
	//! GUI thread reads them itself instead of waiting for the audio thread to
	//! publish them.
	class TimingCollector
	{
	public:
		//! The measurements of exactly one interval.
		struct Snapshot
		{
			std::uint64_t count{0};
			std::uint64_t sumNs{0};
			std::uint64_t maxNs{0};
			//! Measurements that were dropped because the GUI thread did not
			//! consume the queue for a very long time.
			std::uint64_t dropped{0};
		};

		TimingCollector() : m_samples(new std::atomic<std::uint64_t>[kCapacity]) {}

		//! Audio thread: record how long one callback took.
		void record(std::uint64_t executionNs) noexcept
		{
			const auto write = m_writeIndex.load(std::memory_order_relaxed);
			const auto read = m_readIndex.load(std::memory_order_acquire);
			if (write - read >= kCapacity - 1)
			{
				// The GUI thread has not consumed the queue for a very long
				// time. Drop this measurement instead of overwriting one that
				// is still waiting to be reported: a report must never mix
				// two callbacks. One slot stays free, so the audio thread
				// never writes into a slot the GUI thread is reading.
				m_droppedCount.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			m_samples[write & (kCapacity - 1)].store(executionNs, std::memory_order_relaxed);
			m_writeIndex.store(write + 1, std::memory_order_release);
		}

		//! GUI thread: close the interval that ends now.
		void request() noexcept
		{
			m_cutIndex.store(m_writeIndex.load(std::memory_order_acquire), std::memory_order_release);
		}

		//! GUI thread: take the measurements of the closed interval.
		bool take(Snapshot& out) noexcept
		{
			const auto cut = m_cutIndex.load(std::memory_order_acquire);
			const auto read = m_readIndex.load(std::memory_order_relaxed);
			if (cut <= read) { return false; }
			std::uint64_t count = 0;
			std::uint64_t sumNs = 0;
			std::uint64_t maxNs = 0;
			for (auto index = read; index < cut; ++index)
			{
				const auto value = m_samples[index & (kCapacity - 1)].load(std::memory_order_relaxed);
				++count;
				sumNs += value;
				if (value > maxNs) { maxNs = value; }
			}
			m_readIndex.store(cut, std::memory_order_release);
			out.count = count;
			out.sumNs = sumNs;
			out.maxNs = maxNs;
			out.dropped = m_droppedCount.exchange(0, std::memory_order_relaxed);
			return true;
		}

	private:
		//! Number of measurements the queue holds. It is a power of two, so
		//! the index wraps with a mask. At the fastest supported sample rate
		//! the audio thread runs 750 times per second, so a report every five
		//! seconds needs 3750 of them.
		static constexpr std::uint64_t kCapacity = 16384;

		// The measurements; written by the audio thread, read by the GUI thread.
		std::unique_ptr<std::atomic<std::uint64_t>[]> m_samples;
		// The end of the queue; written by the audio thread.
		std::atomic<std::uint64_t> m_writeIndex{0};
		// The start of the queue; written by the GUI thread.
		std::atomic<std::uint64_t> m_readIndex{0};
		// The end of the closed interval; written by the GUI thread.
		std::atomic<std::uint64_t> m_cutIndex{0};
		// The measurements that were dropped; written by the audio thread.
		std::atomic<std::uint64_t> m_droppedCount{0};

		static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
			"the audio-thread timing queue must be lock-free");
	};
	#endif // SA_DEBUG

private:
	ProcessStatus processImpl(InterleavedBufferView<float, 2> in) override;

	#ifdef SA_DEBUG
	static bool isStatsIntervalElapsed(std::chrono::steady_clock::time_point now,
		std::chrono::steady_clock::time_point last)
	{
		return now - last > std::chrono::seconds{5};
	}

	// Runs on the GUI thread (see m_reportTimer), never from processImpl().
	void reportAudioTiming();
	#endif

	SaProcessor m_processor;
	SaControls m_controls;

	// Maximum LMMS buffer size (hard coded, the actual constant is hard to get)
	const unsigned int m_maxBufferSize = 4096;

	// QThread::create() workaround
	// Replace DataprocLauncher by QThread and replace initializer in constructor
	// with the following commented line when LMMS CI starts using Qt > 5.9
	//m_processorThread = QThread::create([=]{m_processor.analyze(m_inputBuffer);});
	DataprocLauncher m_processorThread;

	LocklessRingBuffer<SampleFrame> m_inputBuffer;

	#ifdef SA_DEBUG
		// processImpl() records the measurements into the collector, which is
		// lock-free; the GUI thread closes the interval, sums the
		// measurements up and prints that, so std::cout never runs on the
		// audio thread.
		std::chrono::steady_clock::time_point m_last_dump_time;
		TimingCollector m_timing;
		// Declared last so it is destroyed before the collector it reads.
		QTimer m_reportTimer;
	#endif
};


} // namespace lmms

#endif // ANALYZER_H

