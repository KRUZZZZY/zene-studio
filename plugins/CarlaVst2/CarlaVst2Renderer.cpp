/*
 * CarlaVst2Renderer.cpp - runs Carla's processing for a VST2 instrument or
 *                         effect off the audio thread (Q050-481.1, .2).
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

Renderer::Renderer(RenderFn render, Mode mode) :
	m_render(std::move(render)),
	m_mode(mode),
	m_ring(RingFrames),
	m_scratch(MaxPeriodFrames),
	m_inRing(mode == Mode::Process ? RingFrames : 0)
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
	// An effect's output starts latencyFrames() of silence (the ring is zeroed) ahead of its input.
	if (m_mode == Mode::Process) { m_written.store(m_ahead.load(), std::memory_order_release); }
	m_running.store(true);
	m_thread = std::thread([this] { run(); });
	offlinerender::add(this);
}

void Renderer::stop()
{
	// Out of the export barrier first (it waits for an exporter waiting on this one right now).
	offlinerender::remove(this);
	m_running.store(false);
	if (m_thread.joinable()) { m_thread.join(); }
}

bool Renderer::caughtUp() const
{
	const std::uint64_t ready = m_written.load(std::memory_order_acquire) - m_read.load(std::memory_order_acquire);
	if (m_mode == Mode::Process)
	{
		// Every input queued so far is processed: the output ring then holds latencyFrames() beyond what the
		// periods before took, which covers the next one.
		return m_inTaken.load(std::memory_order_acquire) == m_inWritten.load(std::memory_order_acquire);
	}
	return ready >= static_cast<std::uint64_t>(m_ahead.load(std::memory_order_relaxed)) || ready + MaxPeriodFrames > RingFrames;
}

void Renderer::waitUntilCaughtUp(int limitMs)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(limitMs);
	while (m_running.load() && !m_pauseRequested.load() && !caughtUp() && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::sleep_for(std::chrono::microseconds(100));
	}
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

bool Renderer::processOnePeriod()
{
	const std::uint64_t inTaken = m_inTaken.load(std::memory_order_relaxed);
	const std::uint64_t queued = m_inWritten.load(std::memory_order_acquire) - inTaken;
	const std::uint64_t written = m_written.load(std::memory_order_relaxed);
	const std::uint64_t room = RingFrames - (written - m_read.load(std::memory_order_acquire));
	// At most one engine period: the buffer size Carla was told (a longer call makes it reallocate).
	const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>({queued, room, static_cast<std::uint64_t>(periodFrames())}));
	if (frames == 0) { return false; }
	const std::span<SampleFrame> scratch{m_scratch.data(), frames};
	for (std::size_t f = 0; f < frames; ++f) { scratch[f] = m_inRing[(inTaken + f) & (RingFrames - 1)]; }
	m_inTaken.store(inTaken + frames, std::memory_order_release);
	m_render(scratch);
	for (std::size_t f = 0; f < frames; ++f) { m_ring[(written + f) & (RingFrames - 1)] = scratch[f]; }
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
		const bool worked = m_mode == Mode::Process ? processOnePeriod() : renderOnePeriod();
		if (!worked) { std::this_thread::sleep_for(IdleSleep); }
	}
	m_paused.store(true);
}

void Renderer::process(std::span<SampleFrame> inOut) noexcept
{
	const std::uint64_t inWritten = m_inWritten.load(std::memory_order_relaxed);
	if (inWritten - m_inTaken.load(std::memory_order_acquire) + inOut.size() <= RingFrames)
	{
		for (std::size_t f = 0; f < inOut.size(); ++f) { m_inRing[(inWritten + f) & (RingFrames - 1)] = inOut[f]; }
		m_inWritten.store(inWritten + inOut.size(), std::memory_order_release);
	}
	else
	{
		// The worker is a whole ring behind: this period's input is lost, so its output will never come.
		m_inputOverruns.fetch_add(1, std::memory_order_relaxed);
		m_late -= static_cast<std::int64_t>(inOut.size());
	}
	if (m_late > 0)
	{
		// Frames owed by earlier silent periods that have arrived since: drop them, keeping the latency reported.
		const std::uint64_t ready = m_written.load(std::memory_order_acquire) - m_read.load(std::memory_order_relaxed);
		if (ready > inOut.size())
		{
			const auto drop = std::min<std::uint64_t>(static_cast<std::uint64_t>(m_late), ready - inOut.size());
			m_read.store(m_read.load(std::memory_order_relaxed) + drop, std::memory_order_release);
			m_late -= static_cast<std::int64_t>(drop);
		}
	}
	if (!take(inOut)) { m_late += static_cast<std::int64_t>(inOut.size()); }
}

void Renderer::read(std::span<SampleFrame> out) noexcept
{
	take(out);
}

bool Renderer::take(std::span<SampleFrame> out) noexcept
{
	const std::uint64_t taken = m_read.load(std::memory_order_relaxed);
	const std::uint64_t written = m_written.load(std::memory_order_acquire);
	if (written - taken < out.size())
	{
		std::fill(out.begin(), out.end(), SampleFrame{});
		m_underruns.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	for (std::size_t f = 0; f < out.size(); ++f) { out[f] = m_ring[(taken + f) & (RingFrames - 1)]; }
	m_read.store(taken + out.size(), std::memory_order_release);
	return true;
}

} // namespace lmms::carlavst2
