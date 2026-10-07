/*
 * SessionAudioRecorder.cpp - record AUDIO into a launcher cell (0.5.0 L02, WF-055)
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
 */

#include "SessionAudioRecorder.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "SampleBuffer.h"
#include "SampleRecordAccumulator.h"

namespace lmms
{

namespace
{

//! Frames in @a ticks at @a framesPerTick, rounded; never negative.
std::uint64_t framesOfTicks(tick_t ticks, float framesPerTick) noexcept
{
	if (ticks <= 0 || framesPerTick <= 0.0f) { return 0; }
	return static_cast<std::uint64_t>(std::llround(static_cast<double>(ticks) * static_cast<double>(framesPerTick)));
}

} // namespace

SessionAudioRecorder::SessionAudioRecorder() = default;

SessionAudioRecorder::~SessionAudioRecorder() = default;

bool SessionAudioRecorder::arm(int track, int scene, tick_t lengthTicks, sample_rate_t sampleRate, f_cnt_t latencyFrames)
{
	if (state() != State::Idle) { return false; }
	// The ring and its drain thread are made here, on the control thread: the audio thread only writes.
	m_accum = std::make_unique<SampleRecordAccumulator>(sampleRate, RingFrames);
	m_track.store(track, std::memory_order_relaxed);
	m_scene.store(scene, std::memory_order_relaxed);
	m_lengthTicks = std::max<tick_t>(0, lengthTicks);
	m_sampleRate = sampleRate;
	m_latency = latencyFrames;
	m_recorded.store(0, std::memory_order_relaxed);
	m_targetReading.store(0, std::memory_order_relaxed);
	m_startTick.store(-1, std::memory_order_relaxed);
	setState(State::Armed);  // release: everything above is visible to the audio thread first
	return true;
}

void SessionAudioRecorder::cancel()
{
	setState(State::Idle);
	m_accum.reset();  // joins the drain thread; the caller made sure no period renders
	m_track.store(-1, std::memory_order_relaxed);
	m_scene.store(-1, std::memory_order_relaxed);
}

std::shared_ptr<const SampleBuffer> SessionAudioRecorder::takeFinished()
{
	if (state() != State::Done || m_accum == nullptr) { return nullptr; }
	// Done is terminal for the audio thread, so the accumulator is the control thread's again.
	std::shared_ptr<const SampleBuffer> buffer = m_accum->finish();
	m_accum.reset();
	if (buffer != nullptr && m_target > 0 && buffer->size() > m_target)
	{
		// The span is counted in whole ticks, so a stop can name a line a fraction of a tick before the
		// frames already taken: the take is cut to the span, never longer.
		buffer = std::make_shared<const SampleBuffer>(std::vector<SampleFrame>(buffer->data(),
			buffer->data() + m_target), static_cast<int>(buffer->sampleRate()));
	}
	m_track.store(-1, std::memory_order_relaxed);
	m_scene.store(-1, std::memory_order_relaxed);
	setState(State::Idle);
	return buffer;
}

std::uint64_t SessionAudioRecorder::framesDropped() const noexcept
{
	const State now = state();
	return (now == State::Recording || now == State::Finishing || now == State::Done) && m_accum != nullptr
		? m_accum->framesDropped() : 0;
}

tick_t SessionAudioRecorder::slotStarted(int track, int scene, tick_t startedTick, float framesPerTick) noexcept
{
	if (state() != State::Armed || track != m_track.load(std::memory_order_relaxed)
		|| scene != m_scene.load(std::memory_order_relaxed))
	{
		return 0;
	}
	m_framesPerTick = framesPerTick;
	m_skip = static_cast<std::uint64_t>(std::max<f_cnt_t>(0, m_latency));
	m_taken = 0;
	m_target = framesOfTicks(m_lengthTicks, framesPerTick);
	m_targetReading.store(m_target, std::memory_order_relaxed);
	m_startTick.store(startedTick, std::memory_order_relaxed);
	setState(State::Recording);
	return m_lengthTicks;
}

void SessionAudioRecorder::slotStopped(int track, int scene, tick_t stopTick) noexcept
{
	if (state() != State::Recording || track != m_track.load(std::memory_order_relaxed)
		|| scene != m_scene.load(std::memory_order_relaxed))
	{
		return;
	}
	// The span the cell played, in frames; a stop before a fixed length ends the take there.
	const std::uint64_t span = framesOfTicks(stopTick - m_startTick.load(std::memory_order_relaxed), m_framesPerTick);
	m_target = m_target == 0 ? span : std::min(m_target, span);
	m_targetReading.store(m_target, std::memory_order_relaxed);
	setState(m_taken >= m_target ? State::Done : State::Finishing);
}

void SessionAudioRecorder::push(const SampleFrame* input, f_cnt_t frames, bool streamStarted, f_cnt_t periodFrames) noexcept
{
	const State now = state();
	if (now != State::Recording && now != State::Finishing) { return; }
	if (frames <= 0 && !streamStarted)
	{
		// No capture yet: the staging block this period stands for is one the take never sees.
		m_skip -= std::min<std::uint64_t>(m_skip, static_cast<std::uint64_t>(std::max<f_cnt_t>(0, periodFrames)));
	}
	const auto available = static_cast<std::uint64_t>(std::max<f_cnt_t>(0, frames));
	const std::uint64_t skipped = std::min(available, m_skip);
	m_skip -= skipped;
	if (input != nullptr && available > skipped)
	{
		take(input + skipped, static_cast<f_cnt_t>(available - skipped));
	}
	if (m_target > 0 && m_taken >= m_target && now == State::Finishing) { setState(State::Done); }
}

void SessionAudioRecorder::take(const SampleFrame* input, f_cnt_t frames) noexcept
{
	std::uint64_t count = static_cast<std::uint64_t>(frames);
	if (m_target > 0) { count = std::min(count, m_target - std::min(m_target, m_taken)); }
	if (count == 0 || m_accum == nullptr) { return; }
	m_accum->append(input, static_cast<f_cnt_t>(count));
	m_taken += count;
	m_recorded.store(m_taken, std::memory_order_relaxed);
}

} // namespace lmms
