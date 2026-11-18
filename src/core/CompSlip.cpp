/*
 * CompSlip.cpp - a comped audio take sounds its slipped segments slipped (0.5.0 E2)
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

#include "CompSlip.h"

#include <algorithm>
#include <cmath>

#include "AudioEngine.h"  // DEFAULT_BUFFER_SIZE
#include "SampleClip.h"
#include "SampleFrame.h"
#include "TakeLane.h"
#include "TimePos.h"

namespace lmms
{

std::unique_ptr<CompSlipReaders> CompSlipReaders::create(const TakeLaneModel& comp, const SampleClip& clip,
	f_cnt_t sourceIn, double outputFramesPerTick)
{
	// An auditioned lane sounds whole and unslipped; the comp is set aside (R3.1).
	if (comp.auditionLane() >= 0 || outputFramesPerTick <= 0.0) { return nullptr; }
	std::unique_ptr<CompSlipReaders> readers(new CompSlipReaders());
	const int start = clip.startPosition().getTicks();
	const int end = clip.endPosition().getTicks();
	for (const TakeLaneSegment& segment : comp.segments())
	{
		if (segment.laneIndex != clip.laneIndex() || segment.sourceOffset == 0) { continue; }
		const int from = std::max(segment.beginTick, start);
		const int to = std::min(segment.endTick, end);
		if (to > from) { readers->addSpan(from - start, to - start, segment.sourceOffset, outputFramesPerTick); }
	}
	if (readers->m_spanCount == 0) { return nullptr; }
	readers->openReaders(sourceIn, clip.clipFramesPerTick());
	return readers;
}

int CompSlipReaders::readerFor(int slipTicks)
{
	for (int reader = 0; reader < m_readerCount; ++reader)
	{
		if (m_slips[static_cast<std::size_t>(reader)] == slipTicks) { return reader; }
	}
	if (m_readerCount >= MaxReaders) { return -1; }
	m_slips[static_cast<std::size_t>(m_readerCount)] = slipTicks;
	return m_readerCount++;
}

void CompSlipReaders::addSpan(int fromTick, int toTick, int slipTicks, double outputFramesPerTick)
{
	if (m_spanCount >= MaxSpans) { return; }
	const int reader = readerFor(slipTicks);
	if (reader < 0) { return; }  // a fifth distinct slip plays unslipped (the header's bound)
	m_spans[static_cast<std::size_t>(m_spanCount++)] = {static_cast<f_cnt_t>(std::llround(fromTick * outputFramesPerTick)),
		static_cast<f_cnt_t>(std::llround(toTick * outputFramesPerTick)), reader};
}

void CompSlipReaders::openReaders(f_cnt_t sourceIn, float sourceFramesPerTick)
{
	for (int reader = 0; reader < m_readerCount; ++reader)
	{
		auto& state = m_readers[static_cast<std::size_t>(reader)];
		state.emplace(AudioResampler::RealtimeVoiceTag{});
		state->setFrameIndex(static_cast<int>(sourceIn + TimePos(m_slips[static_cast<std::size_t>(reader)]).frames(sourceFramesPerTick)));
	}
}

auto CompSlipReaders::prepareConverterMode(AudioResampler::Mode mode) noexcept -> bool
{
	for (int reader = 0; reader < m_readerCount; ++reader)
	{
		auto& state = m_readers[static_cast<std::size_t>(reader)];
		if (!state || state->prepareConverterMode(mode)) { continue; }
		for (int done = 0; done < reader; ++done)
		{
			auto& prepared = m_readers[static_cast<std::size_t>(done)];
			if (prepared) { prepared->discardPreparedConverterMode(); }
		}
		return false;
	}
	return true;
}

void CompSlipReaders::adoptPreparedConverterMode() noexcept
{
	for (int reader = 0; reader < m_readerCount; ++reader)
	{
		auto& state = m_readers[static_cast<std::size_t>(reader)];
		if (state) { state->adoptPreparedConverterMode(); }
	}
}

void CompSlipReaders::discardPreparedConverterMode() noexcept
{
	for (int reader = 0; reader < m_readerCount; ++reader)
	{
		auto& state = m_readers[static_cast<std::size_t>(reader)];
		if (state) { state->discardPreparedConverterMode(); }
	}
}

void CompSlipReaders::render(const Sample& sample, SampleFrame* dst, f_cnt_t frames, f_cnt_t clipFrame, double ratio)
{
	std::array<SampleFrame, DEFAULT_BUFFER_SIZE> chunk;
	for (int reader = 0; reader < m_readerCount; ++reader)
	{
		Sample::PlaybackState& state = *m_readers[static_cast<std::size_t>(reader)];
		// Every period, spans or not: the reader keeps step with the handle's own read.
		for (f_cnt_t done = 0; done < frames;)
		{
			const f_cnt_t count = std::min<f_cnt_t>(frames - done, DEFAULT_BUFFER_SIZE);
			if (!sample.play(chunk.data(), &state, count, Sample::Loop::Off, ratio)) { std::fill_n(chunk.data(), count, SampleFrame{}); }
			copySpans(reader, chunk.data(), dst + done, count, clipFrame + done);
			done += count;
		}
	}
}

void CompSlipReaders::copySpans(int reader, const SampleFrame* src, SampleFrame* dst, f_cnt_t frames, f_cnt_t at) const
{
	for (int i = 0; i < m_spanCount; ++i)
	{
		const Span& span = m_spans[static_cast<std::size_t>(i)];
		if (span.reader != reader) { continue; }
		const f_cnt_t from = std::max(span.from, at);
		const f_cnt_t to = std::min(span.to, at + frames);
		if (to > from) { std::copy(src + (from - at), src + (to - at), dst + (from - at)); }
	}
}

} // namespace lmms
