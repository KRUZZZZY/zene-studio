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
		// processImpl() updates the counters with lock-free atomics only; reportAudioTiming()
		// reads and resets them, so std::cout never runs on the audio thread.
		std::chrono::steady_clock::time_point m_last_dump_time;
		std::atomic<int> m_dump_count{0};
		std::atomic<std::uint64_t> m_sum_execution_ns{0};
		std::atomic<std::uint64_t> m_max_execution_ns{0};
		static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
			"audio-thread timing counters must be lock-free");
		// Declared last so it is destroyed before the counters it reads.
		QTimer m_reportTimer;
	#endif
};


} // namespace lmms

#endif // ANALYZER_H

