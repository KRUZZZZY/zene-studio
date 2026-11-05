/*
 * TransientRepeat.cpp - transient repeat / gate warp rendering (Q050-162)
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

#include "TransientRepeat.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <utility>

#include "AudioEngine.h"
#include "Engine.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SamplePlayHandle.h"
#include "TimePos.h"

namespace lmms
{

bool TransientRepeatRate::validDivision(int division)
{
	return division == 1 || division == 2 || division == 4 || division == 8 || division == 16 || division == 32;
}

TransientRepeatRate TransientRepeatRate::syncedTo(int division)
{
	TransientRepeatRate rate;
	rate.division = validDivision(division) ? division : DefaultDivision;
	return rate;
}

TransientRepeatRate TransientRepeatRate::freeInterval(double ms)
{
	TransientRepeatRate rate;
	rate.division = 0;
	const double clamped = std::isfinite(ms) ? std::clamp(ms, MinIntervalMs, MaxIntervalMs) : MinIntervalMs;
	rate.intervalMicros = static_cast<std::int32_t>(std::llround(clamped * 1000.0));
	return rate;
}

TransientRepeatRate TransientRepeatRate::unpack(std::int32_t code)
{
	if (code > 0) { return syncedTo(code); }
	return freeInterval(static_cast<double>(-static_cast<std::int64_t>(code)) / 1000.0);
}

QString TransientRepeatRate::divisionName() const
{
	return synced() ? QStringLiteral("1/%1").arg(division) : QString();
}

QString TransientRepeatRate::name() const
{
	return synced() ? divisionName() : QStringLiteral("%1ms").arg(QString::number(intervalMs()));
}

int TransientRepeatRate::divisionFromName(const QString& name)
{
	if (!name.startsWith(QLatin1String("1/"))) { return 0; }
	bool ok = false;
	const int division = name.mid(2).toInt(&ok);
	return ok && validDivision(division) ? division : 0;
}

bool TransientRepeatRate::fromName(const QString& name, TransientRepeatRate* rate)
{
	if (const int division = divisionFromName(name); division > 0)
	{
		*rate = syncedTo(division);
		return true;
	}
	if (!name.endsWith(QLatin1String("ms"))) { return false; }
	bool ok = false;
	const double ms = name.chopped(2).toDouble(&ok);
	if (!ok || !std::isfinite(ms)) { return false; }
	*rate = freeInterval(ms);
	return true;
}

double TransientRepeatRate::intervalFrames(double outputFramesPerTick, double outputRate) const
{
	const double frames = synced()
		? static_cast<double>(DefaultTicksPerBar) / division * outputFramesPerTick
		: intervalMs() * outputRate / 1000.0;
	return std::max(1.0, frames);
}


void TransientTable::set(std::span<const f_cnt_t> frames)
{
	std::array<f_cnt_t, MaxTransients> sorted{};
	const std::size_t count = std::min(frames.size(), static_cast<std::size_t>(MaxTransients));
	std::copy(frames.begin(), frames.begin() + static_cast<std::ptrdiff_t>(count), sorted.begin());
	std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count));
	const auto end = std::unique(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count));
	m_count = static_cast<int>(end - sorted.begin());
	m_frames = sorted;
}


const SampleFrame& TransientSource::at(f_cnt_t index) const
{
	const f_cnt_t play = windowIn + index;
	return buffer[reversed ? bufferFrames - 1 - play : play];
}


void PublishedTransients::publish(std::span<const f_cnt_t> frames)
{
	const int current = m_published.load();
	for (;;)
	{
		for (int slot = 0; slot < Slots; ++slot)
		{
			if (slot == current || m_readers[static_cast<std::size_t>(slot)].load() != 0) { continue; }
			m_tables[static_cast<std::size_t>(slot)].set(frames);
			m_published.store(slot);
			return;
		}
		// Both unpublished slots pinned by readers that are mid-copy: wait for
		// one on THIS (control) thread - a reader never waits for the writer.
		std::this_thread::yield();
	}
}

int PublishedTransients::pin() const
{
	for (int attempt = 0; attempt < ReadAttempts; ++attempt)
	{
		const int slot = m_published.load();
		m_readers[static_cast<std::size_t>(slot)].fetch_add(1);
		if (m_published.load() == slot) { return slot; }
		m_readers[static_cast<std::size_t>(slot)].fetch_sub(1);
	}
	return -1;
}

void PublishedTransients::unpin(int slot) const
{
	m_readers[static_cast<std::size_t>(slot)].fetch_sub(1);
}

TransientTable PublishedTransients::read() const
{
	const int slot = pin();
	if (slot < 0) { return {}; }
	TransientTable copy = pinned(slot);
	unpin(slot);
	return copy;
}


double transientTicksAt(const WarpMarkers& warp, f_cnt_t frame, f_cnt_t clipIn, double framesPerTick)
{
	const double rate = framesPerTick > 0.0 ? framesPerTick : 1.0;
	if (warp.empty()) { return (static_cast<double>(frame) - static_cast<double>(clipIn)) / rate; }
	const auto markers = warp.all();
	const WarpMarker& first = markers.front();
	const WarpMarker& last = markers.back();
	if (frame <= first.sourceFrame)
	{
		return first.offsetTicks - (static_cast<double>(first.sourceFrame) - static_cast<double>(frame)) / rate;
	}
	if (frame >= last.sourceFrame)
	{
		return last.offsetTicks + (static_cast<double>(frame) - static_cast<double>(last.sourceFrame)) / rate;
	}
	std::size_t index = 0;
	while (index + 2 < markers.size() && markers[index + 1].sourceFrame <= frame) { ++index; }
	const WarpMarker& a = markers[index];
	const WarpMarker& b = markers[index + 1];
	return a.offsetTicks + static_cast<double>(b.offsetTicks - a.offsetTicks)
		* (static_cast<double>(frame) - static_cast<double>(a.sourceFrame))
		/ (static_cast<double>(b.sourceFrame) - static_cast<double>(a.sourceFrame));
}


void TransientRepeatRenderer::prepare(WarpStretchMode mode, double intervalFrames, double sourcePerOutput,
	f_cnt_t windowFrames)
{
	m_mode = mode;
	m_interval = std::max(1.0, intervalFrames);
	m_sourcePerOutput = sourcePerOutput > 0.0 ? sourcePerOutput : 1.0;
	m_windowFrames = windowFrames;
	m_count = 0;
	m_segment = 0;
	m_position = 0;
	m_sourcePosition = 0.0;
}

bool TransientRepeatRenderer::addSegment(double source, double output)
{
	if (m_count >= MaxSegments) { return false; }
	if (m_count > 0)
	{
		const auto previous = static_cast<std::size_t>(m_count - 1);
		if (source <= m_source[previous] || output <= m_output[previous]) { return false; }
	}
	m_source[static_cast<std::size_t>(m_count)] = source;
	m_output[static_cast<std::size_t>(m_count)] = output;
	++m_count;
	return true;
}

namespace
{

//! How long segment @a k's own audio lasts at its natural rate, and its slot
//! (unbounded for the last one), both in output frames.
std::pair<double, double> segmentSpan(const double* source, const double* output, int k, int count,
	double windowFrames, double sourcePerOutput)
{
	const bool last = k + 1 >= count;
	const double sourceEnd = last ? windowFrames : source[k + 1];
	return {(sourceEnd - source[k]) / sourcePerOutput, last ? HUGE_VAL : output[k + 1] - output[k]};
}

//! How far into its audio a segment is @a x frames into its slot, and where
//! the current interval began (@a start): Repeat restarts at every interval,
//! Gate plays the first one only (-1 after it).
double playedAt(WarpStretchMode mode, double interval, double x, double* start)
{
	if (mode == WarpStretchMode::Gate)
	{
		*start = 0.0;
		return x < interval ? x : -1.0;
	}
	*start = std::floor(x / interval) * interval;
	return x - *start;
}

} // namespace

float TransientRepeatRenderer::frameAt(std::int64_t position, double* source)
{
	while (m_segment + 1 < m_count && m_output[static_cast<std::size_t>(m_segment) + 1] <= position) { ++m_segment; }
	const auto k = static_cast<std::size_t>(m_segment);
	const double x = static_cast<double>(position) - m_output[k];
	if (m_count == 0 || x < 0.0) { return 0.0f; }

	// The segment's own audio, at its natural rate, and the slot the warp gives it.
	const auto [audio, slot] = segmentSpan(m_source.data(), m_output.data(), m_segment, m_count,
		static_cast<double>(m_windowFrames), m_sourcePerOutput);
	double start = 0.0;
	const double played = playedAt(m_mode, m_interval, x, &start);
	if (played < 0.0 || played >= audio) { return 0.0f; }

	// A cut that falls inside the slot (a retrigger, the gate, or the audio
	// running out before the slot does) ramps down; the slot's own end is the
	// next transient taking over, which needs no ramp.
	const double cut = std::min(m_interval, audio);
	const double gain = start + cut < slot ? std::min(1.0, (cut - played) / RampFrames) : 1.0;
	*source = m_source[k] + played * m_sourcePerOutput;
	return static_cast<float>(gain);
}

void TransientRepeatRenderer::render(const TransientSource& source, SampleFrame* dst, f_cnt_t frames)
{
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		double read = 0.0;
		const float gain = frameAt(m_position + static_cast<std::int64_t>(f), &read) * source.amplification;
		const auto index = static_cast<f_cnt_t>(read);
		if (gain == 0.0f || source.buffer == nullptr || index >= source.windowFrames)
		{
			dst[f] = SampleFrame(0.0f, 0.0f);
			continue;
		}
		// Linear interpolation between the two play frames around the read.
		const auto fraction = static_cast<float>(read - static_cast<double>(index));
		const SampleFrame& a = source.at(index);
		const SampleFrame& b = source.at(index + 1 < source.windowFrames ? index + 1 : index);
		dst[f] = SampleFrame((a[0] + (b[0] - a[0]) * fraction) * gain, (a[1] + (b[1] - a[1]) * fraction) * gain);
		m_sourcePosition = read;
	}
	m_position += static_cast<std::int64_t>(frames);
}


/*! Q050-162: the transient segments of this pass, snapshotted with the window
 *  and the warp (handle construction; audio-thread-safe: value copies of fixed
 *  arrays and arithmetic only). The clip's transients were analysed on the
 *  control thread when the mode was chosen; here they are only mapped through
 *  the warp - in exact ticks - to the output frames their slots begin on,
 *  relative to this pass's first frame. The interval is read from the project
 *  tempo NOW for a synced rate, which is what makes it follow the tempo. */
void SamplePlayHandle::snapshotTransientRepeat(const SampleClip* clip, WarpStretchMode mode)
{
	const SampleWindow clipWindow = clip->sampleWindow();
	const TransientTable transients = clip->transientTable();
	const double outputRate = Engine::audioEngine()->outputSampleRate();
	const double outputFramesPerTick = Engine::framesPerTick(static_cast<sample_rate_t>(outputRate));
	const double base = m_baseFramesPerTick;
	const double passTicks = transientTicksAt(m_warp, m_window.sourceIn, clipWindow.sourceIn, base);
	// Each segment starts on a whole output frame of the CLIP (rounded from the
	// clip's head, then made pass-relative), so every pass - from the start, or
	// mid-clip after a seek or a loop - puts each boundary on the same frame.
	const double passOutput = std::round(passTicks * outputFramesPerTick);
	const auto outputAt = [&](f_cnt_t frame) {
		return std::round(transientTicksAt(m_warp, frame, clipWindow.sourceIn, base) * outputFramesPerTick) - passOutput;
	};

	// The segments are read from the CLIP's window (a pass may begin mid-clip,
	// inside a segment whose transient lies before the pass's own first frame),
	// so their source frames are relative to the clip window's head and their
	// output frames to the pass's first frame - negative for the earlier ones.
	m_repeatWindow = clipWindow;
	m_transientRepeat.prepare(mode, clip->transientRepeatRate().intervalFrames(outputFramesPerTick, outputRate),
		static_cast<double>(m_sample->sampleRate()) / outputRate, clipWindow.length());
	// The head of the clip's window is the first segment's start, transient or not.
	m_transientRepeat.addSegment(0.0, outputAt(clipWindow.sourceIn));
	for (int i = 0; i < transients.size(); ++i)
	{
		const f_cnt_t frame = transients[i];
		if (frame <= clipWindow.sourceIn || frame >= clipWindow.sourceOut) { continue; }
		m_transientRepeat.addSegment(static_cast<double>(frame - clipWindow.sourceIn), outputAt(frame));
	}
	m_repeatsTransients = true;
}

/*! The amplification and the direction are read each period, as Sample::play
 *  reads them (two relaxed atomic loads), so a change while the clip plays
 *  reaches this render exactly as it reaches the resampled one. */
void SamplePlayHandle::renderTransientRepeat(SampleFrame* dst, f_cnt_t frames)
{
	TransientSource source;
	source.buffer = m_sample->data();
	source.bufferFrames = m_sample->sampleSize();
	source.windowIn = m_repeatWindow.sourceIn;
	source.windowFrames = m_repeatWindow.length();
	source.reversed = m_sample->reversed();
	source.amplification = m_sample->amplification();
	if (source.buffer == nullptr || source.windowFrames == 0 || m_repeatWindow.sourceOut > source.bufferFrames)
	{
		zeroSampleFrames(dst, frames);
		return;
	}
	m_transientRepeat.render(source, dst, frames);
	m_state.setFrameIndex(static_cast<int>(m_repeatWindow.sourceIn
		+ static_cast<f_cnt_t>(m_transientRepeat.sourcePosition())));
}

} // namespace lmms
