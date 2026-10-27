/*
 * RealtimeExport.cpp - a project render played through the live device, so hardware inserts are heard
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

#include "RealtimeExport.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleRecordAccumulator.h"
#include "Song.h"

namespace lmms
{

RealtimeExportCapture::RealtimeExportCapture() = default;

RealtimeExportCapture::~RealtimeExportCapture() = default;

RealtimeExportCapture& RealtimeExportCapture::instance()
{
	static RealtimeExportCapture capture;
	return capture;
}

bool RealtimeExportCapture::arm(sample_rate_t sampleRate, tick_t endTick, f_cnt_t skipFrames, f_cnt_t tailFrames,
	float gain, std::uint64_t maxFrames)
{
	if (state() != State::Idle) { return false; }
	// The ring and its drain thread are made here, on the control thread: the audio thread only writes.
	m_accum = std::make_unique<SampleRecordAccumulator>(sampleRate, RingFrames);
	m_endTick = std::max<tick_t>(0, endTick);
	m_tail = std::max<f_cnt_t>(0, tailFrames);
	m_gain = gain;
	m_maxFrames = maxFrames;
	m_skip = static_cast<std::uint64_t>(std::max<f_cnt_t>(0, skipFrames));
	m_played = 0;
	m_target = 0;
	m_taken = 0;
	m_recorded.store(0, std::memory_order_relaxed);
	m_targetReading.store(0, std::memory_order_relaxed);
	m_capped.store(false, std::memory_order_relaxed);
	setState(State::Armed);  // release: everything above is visible to the audio thread first
	return true;
}

void RealtimeExportCapture::cancel()
{
	setState(State::Idle);
	m_accum.reset();  // joins the drain thread; the caller made sure no period renders
}

std::shared_ptr<const SampleBuffer> RealtimeExportCapture::takeFinished()
{
	if (state() != State::Done || m_accum == nullptr) { return nullptr; }
	// Done is terminal for the audio thread, so the accumulator is the control thread's again.
	std::shared_ptr<const SampleBuffer> buffer = m_accum->finish();
	m_accum.reset();
	const std::uint64_t length = m_target > 0 ? m_target : m_taken;
	if (buffer != nullptr && buffer->size() > length)
	{
		buffer = std::make_shared<const SampleBuffer>(std::vector<SampleFrame>(buffer->data(),
			buffer->data() + length), static_cast<int>(buffer->sampleRate()));
	}
	setState(State::Idle);
	return buffer;
}

std::uint64_t RealtimeExportCapture::framesDropped() const noexcept
{
	const State now = state();
	return (now == State::Recording || now == State::Done) && m_accum != nullptr ? m_accum->framesDropped() : 0;
}

void RealtimeExportCapture::push(const SampleFrame* buffer, f_cnt_t frames, tick_t ticks, double frameOffset,
	double framesPerTick) noexcept
{
	State now = state();
	if (now == State::Armed)
	{
		setState(State::Recording);
		now = State::Recording;
	}
	if (now != State::Recording || buffer == nullptr || frames <= 0) { return; }
	const auto count = static_cast<std::uint64_t>(frames);
	m_played += count;
	if (m_target == 0 && ticks >= m_endTick)
	{
		// The end tick was passed inside this period: the song's last frame is that many frames back.
		const double past = static_cast<double>(ticks - m_endTick) * framesPerTick + frameOffset;
		const auto back = static_cast<std::uint64_t>(std::clamp(std::llround(past), 0LL, static_cast<long long>(count)));
		// Song frame k is file frame k (the head the delay compensation adds is skipped), so the file ends
		// at the song's end plus the tail; at least one frame, so a passed end is never read as "not yet".
		m_target = std::max<std::uint64_t>(1, m_played - back + static_cast<std::uint64_t>(m_tail));
		m_targetReading.store(m_target, std::memory_order_relaxed);
	}
	const std::uint64_t skipped = std::min(count, m_skip);
	m_skip -= skipped;
	if (count > skipped) { keep(buffer + skipped, static_cast<f_cnt_t>(count - skipped)); }
	if (m_taken >= m_maxFrames && (m_target == 0 || m_taken < m_target)) { m_capped.store(true, std::memory_order_relaxed); }
	if ((m_target > 0 && m_taken >= m_target) || m_taken >= m_maxFrames) { setState(State::Done); }
}

void RealtimeExportCapture::keep(const SampleFrame* buffer, f_cnt_t frames) noexcept
{
	std::uint64_t limit = m_maxFrames;
	if (m_target > 0) { limit = std::min(limit, m_target); }
	std::uint64_t left = std::min(static_cast<std::uint64_t>(frames), limit - std::min(limit, m_taken));
	if (m_accum == nullptr) { return; }
	while (left > 0)
	{
		const auto chunk = static_cast<f_cnt_t>(std::min<std::uint64_t>(left, ScratchFrames));
		for (f_cnt_t f = 0; f < chunk; ++f) { m_scratch[f] = SampleFrame(buffer[f][0] * m_gain, buffer[f][1] * m_gain); }
		m_accum->append(m_scratch.data(), chunk);
		buffer += chunk;
		left -= static_cast<std::uint64_t>(chunk);
		m_taken += static_cast<std::uint64_t>(chunk);
	}
	m_recorded.store(m_taken, std::memory_order_relaxed);
}

namespace realtimeexport
{

void master(const SampleFrame* buffer, f_cnt_t frames) noexcept
{
	RealtimeExportCapture& capture = RealtimeExportCapture::instance();
	if (!capture.active()) { return; }
	const Song* song = Engine::getSong();
	// The take starts with the song: render.realtime arms it and starts playback under one change lock, so
	// the first period that sees it armed is the song's first (this read is ordered by that lock too).
	if (song == nullptr || !song->isPlaying()) { return; }
	// Read on the render thread that advances them: the song's position after this period's ticks.
	const Timeline& timeline = song->getTimeline(Song::PlayMode::Song);
	capture.push(buffer, frames, timeline.pos().getTicks(), static_cast<double>(timeline.frameOffset()),
		static_cast<double>(Engine::framesPerTick()));
}

} // namespace realtimeexport

} // namespace lmms
