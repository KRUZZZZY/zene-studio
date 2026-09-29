/*
 * RubberBandStretch.h - the optional Rubber Band time stretch a clip can render
 *                       a rate change through (owner decision 12)
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

#ifndef LMMS_RUBBER_BAND_STRETCH_H
#define LMMS_RUBBER_BAND_STRETCH_H

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include "LmmsTypes.h"
#include "SampleFrame.h"
#include "lmms_export.h"

namespace RubberBand
{
class RubberBandStretcher;
} // namespace RubberBand

namespace lmms
{

/*! One Rubber Band stretcher and the fixed buffers it is fed from.
 *
 *  WHY THIS EXISTS. `AudioStretcher` (WSOLA) is the default pitch-preserving
 *  stretch and stays so (owner decision 12). Rubber Band's R3 ("finer") engine
 *  is the higher-quality phase-vocoder answer for polyphonic and tonal
 *  material, and it is an OPTIONAL dependency: a build without it keeps the
 *  mode in its vocabulary and renders such a clip through WSOLA.
 *
 *  REALTIME, measured rather than assumed (Rubber Band 3.3.0, R3 engine, real-
 *  time mode, a malloc/posix_memalign interposer on the calling thread):
 *  `process()`, `retrieve()`, `setTimeRatio()` and `setPitchScale()` allocate
 *  nothing once the stretcher exists, but `reset()` allocates twice on EVERY
 *  call. So a voice is never reset on the audio thread. It is built and reset
 *  off it (RubberBandPool's recycler), claimed lock-free by a play handle,
 *  used once, and released dirty - the recycler resets it for the next pass.
 */
class LMMS_EXPORT RubberBandVoice
{
public:
	//! The largest block handed to the stretcher in one process()/retrieve(),
	//! which is also the size `setMaxProcessSize` promises it.
	static constexpr int MaxBlock = 2048;

	RubberBandVoice(int sourceRate);
	~RubberBandVoice();

	RubberBandVoice(const RubberBandVoice&) = delete;
	RubberBandVoice& operator=(const RubberBandVoice&) = delete;

	/*! Starts a stream at window frame 0 with \a pitchScale - the source-to-
	 *  output rate correction, 1.0 when they agree. Audio thread, allocation-
	 *  free. The first render() then sets the ratio, feeds the preferred start
	 *  pad of silence and discards the start delay, so output frame 0 is
	 *  source frame 0. */
	void begin(double pitchScale) noexcept;

	/*! Emits \a dstFrames output frames into \a dst, reading the source span
	 *  \a src (frames [0, srcFrames)) at \a speed source frames per output
	 *  frame - the same quantity AudioStretcher::process takes. Reads outside
	 *  the span are silence. Returns the frames written (always \a dstFrames
	 *  unless the stretcher stalls, which the caller zero-fills). */
	f_cnt_t render(const SampleFrame* src, f_cnt_t srcFrames, SampleFrame* dst,
		f_cnt_t dstFrames, double speed) noexcept;

	//! The source position the emitted output stands at, in window frames.
	double sourcePosition() const { return m_sourcePos; }

	//! Hands the voice back, dirty. Any thread; one atomic store.
	void release() noexcept;

private:
	friend class RubberBandPool;
	enum State : int { Ready = 0, InUse = 1, Dirty = 2 };

	//! The first render's setup: pitch, ratio, start pad, start delay.
	void start(double timeRatio) noexcept;
	void feed(const SampleFrame* src, f_cnt_t srcFrames, int frames) noexcept;
	//! Retrieves up to \a available frames: dropped while the start delay
	//! lasts, else written to \a dst (at most \a room). Returns frames written.
	f_cnt_t drain(int available, SampleFrame* dst, f_cnt_t room) noexcept;

	std::unique_ptr<RubberBand::RubberBandStretcher> m_stretcher;
	std::atomic<int> m_state{Ready};
	std::array<float, MaxBlock> m_inLeft{};
	std::array<float, MaxBlock> m_inRight{};
	std::array<float, MaxBlock> m_outLeft{};
	std::array<float, MaxBlock> m_outRight{};
	double m_timeRatio = 1.0;
	double m_pitchScale = 1.0;
	bool m_started = false;
	double m_sourcePos = 0.0;
	std::int64_t m_feedPos = 0;
	std::int64_t m_toDiscard = 0;
};

/*! A clip's voices. Built on the control thread when the clip asks for the
 *  Rubber Band mode; claimed by the play handle on the audio thread.
 *
 *  Two voices, because two handles of one clip can overlap (a loop restart, a
 *  seek back into the clip) and the recycler needs a moment to reset the one
 *  just released. A handle that finds none ready renders through WSOLA and
 *  the miss is counted, so a caller that needs every pass on Rubber Band can
 *  check `misses()` rather than listen for it.
 */
class LMMS_EXPORT RubberBandPool
{
public:
	static constexpr int VoiceCount = 2;

	//! True when this build links Rubber Band. False: the pool holds no voice
	//! and every claim misses, which is the WSOLA fallback.
	static bool available();

	explicit RubberBandPool(int sourceRate);
	~RubberBandPool();

	RubberBandPool(const RubberBandPool&) = delete;
	RubberBandPool& operator=(const RubberBandPool&) = delete;

	//! Audio thread, lock-free: a ready voice marked in use, or nullptr.
	RubberBandVoice* claim() noexcept;

	int readyVoices() const noexcept;
	std::uint64_t misses() const noexcept { return m_misses.load(std::memory_order_relaxed); }

	/*! Resets every dirty voice of every pool now, on the calling thread (never
	 *  the audio thread). The recycler thread does this on its own every few
	 *  milliseconds; a test calls it to make "ready again" deterministic. */
	static void recycleNow();

private:
	friend class RubberBandRecycler;
	void recycleDirty();

	std::array<std::unique_ptr<RubberBandVoice>, VoiceCount> m_voices;
	std::atomic<std::uint64_t> m_misses{0};
};

} // namespace lmms

#endif // LMMS_RUBBER_BAND_STRETCH_H
