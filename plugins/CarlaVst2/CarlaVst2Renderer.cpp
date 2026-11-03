/*
 * CarlaVst2Renderer.cpp - runs Carla's processing for a VST2 instrument off the
 *                         audio thread (Q050-481.1).
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

#include "CarlaVst2Renderer.h"

#include <algorithm>
#include <chrono>

#include "AudioEngine.h"
#include "Engine.h"

namespace lmms::carlavst2
{

namespace
{

//! How long the worker sleeps when the ring is full enough: well under a period at any usual rate.
constexpr auto IdleSleep = std::chrono::microseconds(250);

} // namespace

Renderer::Renderer(RenderFn render) :
	m_render(std::move(render)),
	m_ring(RingFrames),
	m_scratch(MaxPeriodFrames)
{
}

Renderer::~Renderer()
{
	stop();
}

f_cnt_t Renderer::periodFrames()
{
	const AudioEngine* engine = Engine::audioEngine();
	const f_cnt_t frames = engine != nullptr ? engine->framesPerPeriod() : 256;
	return std::clamp<f_cnt_t>(frames, 1, MaxPeriodFrames);
}

void Renderer::start(f_cnt_t aheadFrames)
{
	if (m_running.load()) { return; }
	m_ahead.store(std::clamp<f_cnt_t>(aheadFrames, 1, static_cast<f_cnt_t>(RingFrames - MaxPeriodFrames)));
	m_running.store(true);
	m_thread = std::thread([this] { run(); });
}

void Renderer::stop()
{
	m_running.store(false);
	if (m_thread.joinable()) { m_thread.join(); }
}

void Renderer::pause()
{
	m_pauseRequested.store(true);
	// The control thread may wait: the worker acknowledges between two periods.
	while (m_running.load() && !m_paused.load()) { std::this_thread::sleep_for(IdleSleep); }
}

void Renderer::resume()
{
	m_pauseRequested.store(false);
}

bool Renderer::renderOnePeriod()
{
	const std::uint64_t written = m_written.load(std::memory_order_relaxed);
	const std::uint64_t taken = m_read.load(std::memory_order_acquire);
	const f_cnt_t frames = periodFrames();
	const std::uint64_t filled = written - taken;
	if (filled >= static_cast<std::uint64_t>(m_ahead.load(std::memory_order_relaxed))
		|| filled + frames > RingFrames)
	{
		return false;
	}
	const std::span<SampleFrame> scratch{m_scratch.data(), static_cast<std::size_t>(frames)};
	m_render(scratch);
	for (f_cnt_t f = 0; f < frames; ++f) { m_ring[(written + f) & (RingFrames - 1)] = scratch[f]; }
	m_written.store(written + frames, std::memory_order_release);
	m_rendered.fetch_add(1, std::memory_order_relaxed);
	return true;
}

void Renderer::run()
{
	while (m_running.load())
	{
		if (m_pauseRequested.load())
		{
			m_paused.store(true);
			std::this_thread::sleep_for(IdleSleep);
			continue;
		}
		m_paused.store(false);
		if (!renderOnePeriod()) { std::this_thread::sleep_for(IdleSleep); }
	}
	m_paused.store(true);
}

void Renderer::read(std::span<SampleFrame> out, bool mayWait, int waitLimitMs) noexcept
{
	const std::uint64_t taken = m_read.load(std::memory_order_relaxed);
	std::uint64_t written = m_written.load(std::memory_order_acquire);
	if (written - taken < out.size() && mayWait)
	{
		// An export only: no realtime deadline, and a dropped period would be silence in the file.
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitLimitMs);
		while (written - taken < out.size() && m_running.load(std::memory_order_relaxed)
			&& std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::microseconds(100));
			written = m_written.load(std::memory_order_acquire);
		}
	}
	if (written - taken < out.size())
	{
		std::fill(out.begin(), out.end(), SampleFrame{});
		m_underruns.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	for (std::size_t f = 0; f < out.size(); ++f) { out[f] = m_ring[(taken + f) & (RingFrames - 1)]; }
	m_read.store(taken + out.size(), std::memory_order_release);
}

} // namespace lmms::carlavst2
