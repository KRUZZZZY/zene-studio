/*
 * CarlaVst2Renderer.h - runs Carla's processing for a VST2 instrument off the
 *                       audio thread (Q050-481.1).
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

#ifndef LMMS_CARLA_VST2_RENDERER_H
#define LMMS_CARLA_VST2_RENDERER_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <span>
#include <thread>
#include <vector>

#include "SampleFrame.h"
#include "LmmsTypes.h"

namespace lmms::carlavst2
{

/*!
 * Carla's process call is not realtime-safe for this host's purposes: for a
 * Windows plug-in it waits on the Wine bridge's semaphore every block, and its
 * parameter path takes mutexes. So Carla is never called from the audio thread.
 *
 * A worker thread of its own renders periods ahead through @a render (Carla's
 * process, with the MIDI the track queued) into a preallocated single-producer /
 * single-consumer frame ring. The audio thread's read() copies finished frames
 * out of the ring with two atomics and memcpy - no lock, no allocation, no
 * system call - or outputs silence for a period that is not ready (counted in
 * underruns()). The price is a fixed latency of latencyFrames(), which the
 * instrument reports to the track for compensation.
 *
 * While a project is exported there is no realtime deadline, and dropping a
 * period would put silence into the file: read(.., mayWait = true) then waits
 * for the worker (bounded), as the VeSTige host already does while exporting.
 */
class Renderer
{
public:
	//! Renders exactly @a out.size() frames; runs on the worker thread only.
	using RenderFn = std::function<void(std::span<SampleFrame> out)>;

	static constexpr f_cnt_t MaxPeriodFrames = 4096;   // MAXIMUM_BUFFER_SIZE
	static constexpr std::size_t RingFrames = 32768;   // a power of two, eight maximum periods

	explicit Renderer(RenderFn render);
	~Renderer();

	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	//! Starts the worker, keeping @a aheadFrames rendered ahead of the audio thread (clamped to the ring).
	void start(f_cnt_t aheadFrames);
	//! Stops and joins the worker; idempotent.
	void stop();

	//! Audio thread: @a out gets the next frames, or silence (counted) if they are not ready. With @a mayWait
	//! (an export, no realtime deadline) it waits for them, at most @a waitLimitMs.
	void read(std::span<SampleFrame> out, bool mayWait, int waitLimitMs = 2000) noexcept;

	//! Control thread: holds the worker between periods (it finishes the one it is rendering) until resume().
	void pause();
	void resume();

	f_cnt_t latencyFrames() const { return m_ahead.load(std::memory_order_relaxed); }
	std::uint64_t underruns() const { return m_underruns.load(std::memory_order_relaxed); }
	std::uint64_t renderedPeriods() const { return m_rendered.load(std::memory_order_relaxed); }

	//! The audio engine's period, as the worker renders it (bounded by MaxPeriodFrames).
	static f_cnt_t periodFrames();

private:
	void run();
	bool renderOnePeriod();

	RenderFn m_render;
	std::vector<SampleFrame> m_ring;
	std::vector<SampleFrame> m_scratch;
	std::atomic<std::uint64_t> m_written{0};  //!< frames the worker has published (worker writes)
	std::atomic<std::uint64_t> m_read{0};     //!< frames the audio thread has taken (audio thread writes)
	std::atomic<f_cnt_t> m_ahead{0};
	std::atomic<bool> m_running{false};
	std::atomic<bool> m_pauseRequested{false};
	std::atomic<bool> m_paused{false};
	std::atomic<std::uint64_t> m_underruns{0};
	std::atomic<std::uint64_t> m_rendered{0};
	std::thread m_thread;
};

} // namespace lmms::carlavst2

#endif // LMMS_CARLA_VST2_RENDERER_H
