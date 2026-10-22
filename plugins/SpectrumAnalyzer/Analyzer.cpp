/*
 * Analyzer.cpp - definition of Analyzer class.
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

#include "Analyzer.h"

#ifdef SA_DEBUG
	#include <chrono>
	#include <iostream>
#endif

#include "embed.h"
#include "LmmsTypes.h"
#include "plugin_export.h"

namespace lmms
{


extern "C" {
	Plugin::Descriptor PLUGIN_EXPORT analyzer_plugin_descriptor =
	{
		"spectrumanalyzer",
		"Spectrum Analyzer",
		QT_TRANSLATE_NOOP("PluginBrowser", "A graphical spectrum analyzer."),
		"Martin Pavelek <he29/dot/HS/at/gmail/dot/com>",
		0x0112,
		Plugin::Type::Effect,
		new PixmapLoader("zene-plugin-logo"),
		nullptr,
		nullptr,
	};
}


Analyzer::Analyzer(Model *parent, const Plugin::Descriptor::SubPluginFeatures::Key *key) :
	AudioPlugin(&analyzer_plugin_descriptor, parent, key),
	m_processor(&m_controls),
	m_controls(this),
	m_processorThread(m_processor, m_inputBuffer),
	// Buffer is sized to cover 4* the current maximum LMMS audio buffer size,
	// so that it has some reserve space in case data processor is busy.
	m_inputBuffer(4 * m_maxBufferSize)
	#ifdef SA_DEBUG
	, m_last_dump_time(std::chrono::steady_clock::now())
	#endif
{
	m_processorThread.start();
	#ifdef SA_DEBUG
		// The timer belongs to the GUI thread, so the report is printed off the audio thread.
		QObject::connect(&m_reportTimer, &QTimer::timeout, this, [this] {reportAudioTiming();});
		m_reportTimer.start(1000);	// checked once a second; a report is printed every 5 seconds
	#endif
}


Analyzer::~Analyzer()
{
	m_processor.terminate();
	m_inputBuffer.wakeAll();
	m_processorThread.wait();
}

// Take audio data and pass them to the spectrum processor.
ProcessStatus Analyzer::processImpl(InterleavedBufferView<float, 2> in)
{
	// Measure time spent in audio thread; both average and peak should be well under 1 ms.
	#ifdef SA_DEBUG
		const auto audio_time = std::chrono::steady_clock::now();
	#endif

	// Skip processing if the controls dialog isn't visible, it would only waste CPU cycles.
	if (m_controls.isViewVisible())
	{
		// To avoid processing spikes on audio thread, data are stored in
		// a lockless ringbuffer and processed in a separate thread.
		m_inputBuffer.write(reinterpret_cast<SampleFrame*>(in.data()), in.frames(), true);
	}
	#ifdef SA_DEBUG
		// Lock-free only: the collector hands the measurements to the GUI
		// thread, which sums them up itself, so reportAudioTiming() never
		// prints on this thread.
		const auto execution_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - audio_time).count());
		m_timing.record(execution_ns);
	#endif

	return ProcessStatus::Continue;
}

#ifdef SA_DEBUG
// Runs on the GUI thread once a second and prints every 5 seconds. The GUI
// thread closes the interval that ends now and takes the measurements of that
// interval, so the count, the sum and the peak of one report always describe
// the same callbacks: a callback that lands after the request belongs to the
// next interval. The measurements are read by the GUI thread itself, so the
// accumulated timings of an interval are reported even when no callback
// follows the request.
void Analyzer::reportAudioTiming()
{
	const auto now = std::chrono::steady_clock::now();
	if (!isStatsIntervalElapsed(now, m_last_dump_time)) {return;}
	m_last_dump_time = now;

	// Close the interval that ends now and take its totals.
	m_timing.request();
	TimingCollector::Snapshot snapshot;
	if (m_timing.take(snapshot) && snapshot.count > 0)
	{
		std::cout << "Analyzer audio thread: " << static_cast<double>(snapshot.sumNs) / 1.0e6 / snapshot.count << " ms avg / "
			<< static_cast<double>(snapshot.maxNs) / 1.0e6 << " ms peak." << std::endl;
	}
}
#endif


extern "C" {
	// needed for getting plugin out of shared lib
	PLUGIN_EXPORT Plugin *lmms_plugin_main(Model *parent, void *data)
	{
		return new Analyzer(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key *>(data));
	}
}


} // namespace lmms
