/*
 * RealtimeExport.h - a project render played through the live device, so hardware inserts are heard
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

#ifndef LMMS_REALTIME_EXPORT_H
#define LMMS_REALTIME_EXPORT_H

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include "LmmsTypes.h"
#include "SampleFrame.h"
#include "lmms_export.h"

namespace lmms
{

class SampleBuffer;
class SampleRecordAccumulator;

/*! Q050-089.2: the capture behind render.realtime (docs/050-features/Q050-089.2.md).
 *
 *  An offline export runs no device, so a hardware insert is bypassed there (HardwareIo.cpp). A real-time
 *  render plays the song on the running device instead - the insert's send goes out, its return comes back -
 *  and this keeps the master mix of every period it plays, from the song's first period until the song
 *  position passes the end tick, plus a tail.
 *
 *  ALIGNMENT. The master runs @a skipFrames (the mixer's total delay compensation when the render started)
 *  behind the song, so that many frames are dropped from the head: frame k of the file is song frame k, the
 *  frame an offline export writes at k.
 *
 *  THREADS. arm(), cancel() and takeFinished() are control-thread calls; arm() makes the accumulator (its
 *  pre-allocated ring and drain thread). The audio thread calls push() once per period from the master mix:
 *  while nothing is armed that is one acquire load, and otherwise it only scales into a fixed scratch array
 *  and writes the accumulator's ring - no allocation, lock, I/O or signal. Done is terminal for the audio
 *  thread, so takeFinished() may take the accumulator once it reads Done; cancel() must run while no period
 *  renders (the caller holds the engine's change lock). */
class LMMS_EXPORT RealtimeExportCapture
{
public:
	enum class State : int
	{
		Idle = 0,   //!< nothing armed
		Armed,      //!< the next period is the song's first
		Recording,  //!< periods are kept
		Done        //!< the take is complete and waits for takeFinished()
	};

	//! The accumulator's ring: 2^18 frames (~5.5 s at 48 kHz) between the audio thread and the drain thread.
	static constexpr std::size_t RingFrames = std::size_t{1} << 18;

	RealtimeExportCapture();
	~RealtimeExportCapture();
	RealtimeExportCapture(const RealtimeExportCapture&) = delete;
	RealtimeExportCapture& operator=(const RealtimeExportCapture&) = delete;

	//! The capture the master mix feeds.
	static RealtimeExportCapture& instance();

	/*! Arms a take at @a sampleRate that ends @a tailFrames after song tick @a endTick, drops the first
	 *  @a skipFrames, scales by @a gain and never runs past @a maxFrames. False when a take is in hand. */
	bool arm(sample_rate_t sampleRate, tick_t endTick, f_cnt_t skipFrames, f_cnt_t tailFrames, float gain,
		std::uint64_t maxFrames);
	//! Drops whatever is armed or recorded. Call with no period rendering (see the class comment).
	void cancel();
	//! The finished take, cut to its length (nullptr when it holds nothing) once the state is Done; back to Idle.
	std::shared_ptr<const SampleBuffer> takeFinished();

	State state() const noexcept { return static_cast<State>(m_state.load(std::memory_order_acquire)); }
	bool active() const noexcept { return state() == State::Armed || state() == State::Recording; }
	//! Frames kept so far, and the take's length once the end tick was passed (0 before).
	std::uint64_t framesRecorded() const noexcept { return m_recorded.load(std::memory_order_relaxed); }
	std::uint64_t targetFrames() const noexcept { return m_targetReading.load(std::memory_order_relaxed); }
	//! Whether the take stopped at its frame cap before the song's end was reached.
	bool capped() const noexcept { return m_capped.load(std::memory_order_relaxed); }
	//! Frames the ring had to drop (a starved drain thread); padded with silence in the take.
	std::uint64_t framesDropped() const noexcept;

	// ---- audio thread -------------------------------------------------------------------------

	/*! One period of the master mix, @a frames long, after which the song stands at tick @a ticks plus
	 *  @a frameOffset frames, at @a framesPerTick. */
	void push(const SampleFrame* buffer, f_cnt_t frames, tick_t ticks, double frameOffset, double framesPerTick) noexcept;

private:
	void setState(State state) noexcept { m_state.store(static_cast<int>(state), std::memory_order_release); }
	//! The end tick was passed in this period of @a count frames: the take's length is known now. Audio thread.
	void noteEnd(std::uint64_t count, tick_t ticks, double frameOffset, double framesPerTick) noexcept;
	//! Done once the take reached its length or the cap. Audio thread.
	void finishIfDone() noexcept;
	//! Keeps @a frames frames of @a buffer, scaled, never past the target or the cap. Audio thread.
	void keep(const SampleFrame* buffer, f_cnt_t frames) noexcept;

	static constexpr f_cnt_t ScratchFrames = 512;

	std::atomic<int> m_state{static_cast<int>(State::Idle)};
	std::unique_ptr<SampleRecordAccumulator> m_accum;
	tick_t m_endTick = 0;
	f_cnt_t m_tail = 0;
	float m_gain = 1.f;
	std::uint64_t m_maxFrames = 0;

	// the audio thread's own bookkeeping, published through the atomics below for the readings
	std::uint64_t m_skip = 0;
	std::uint64_t m_played = 0;  //!< frames of the song played since its first period
	std::uint64_t m_target = 0;  //!< 0: the end tick was not passed yet
	std::uint64_t m_taken = 0;
	std::array<SampleFrame, ScratchFrames> m_scratch{};
	std::atomic<std::uint64_t> m_recorded{0};
	std::atomic<std::uint64_t> m_targetReading{0};
	std::atomic<bool> m_capped{false};
};

namespace realtimeexport
{

//! Render thread, from the master mix: this period's master output, before any listen bus replaces it.
LMMS_EXPORT void master(const SampleFrame* buffer, f_cnt_t frames) noexcept;

} // namespace realtimeexport

} // namespace lmms

#endif // LMMS_REALTIME_EXPORT_H
