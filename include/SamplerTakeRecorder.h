/*
 * SamplerTakeRecorder.h - capture the input straight into a sampler (0.5.0 L10, Q050-156)
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

#ifndef LMMS_SAMPLER_TAKE_RECORDER_H
#define LMMS_SAMPLER_TAKE_RECORDER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <QString>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class SampleBuffer;
class SampleFrame;
class SampleRecordAccumulator;

/*! One bounded take recorded from the engine's configured stereo input straight into an
 *  AudioFileProcessor or SlicerT (Q050-156).
 *
 *  THE SHAPE. record.sampler_capture arms a take for one instrument track and one device;
 *  the audio thread stages the input into the clip recorder's own pre-allocated ring
 *  (SampleRecordAccumulator) until the requested length is reached, and a drain thread builds
 *  the SampleBuffer off the audio thread. record.sampler_capture_stop ends the take early;
 *  record.sampler_capture_land writes it as a WAV and installs it in the device.
 *
 *  THREADS. arm(), stop(), cancel(), takeFinished() and releaseFinished() are control-thread
 *  calls; arm/stop/cancel are called with the engine's model-change lock held, so no period
 *  renders while the accumulator handle changes. push() is the audio thread's only entry
 *  point: it allocates nothing, takes no lock, makes no syscall and never waits. While the
 *  recorder is idle it is one relaxed atomic load per rendered period.
 *
 *  The take is shifted by the engine's recording round trip (AudioEngine::
 *  recordingLatencyFrames): the first latency frames after arming are skipped, so the take
 *  lines up with what was heard while it was performed - the same rule SessionAudioRecorder
 *  and SampleRecordHandle follow.
 */
class LMMS_EXPORT SamplerTakeRecorder
{
public:
	enum class State : int
	{
		Idle = 0,   //!< nothing armed
		Recording,  //!< input frames are being taken
		Done        //!< the take is complete and waits for takeFinished()
	};

	//! Ring capacity: 2^18 frames (~5.5 s at 48 kHz). The accumulator drains it on its own
	//! thread; only a starved drain thread drops frames, and every drop is counted.
	static constexpr std::size_t RingFrames = std::size_t{1} << 18;
	//! A take is bounded: the longest requested duration a caller may ask for.
	static constexpr double MaxSeconds = 30.0;

	SamplerTakeRecorder();
	~SamplerTakeRecorder();
	SamplerTakeRecorder(const SamplerTakeRecorder&) = delete;
	SamplerTakeRecorder& operator=(const SamplerTakeRecorder&) = delete;

	/*! Arm a take for @a trackId and @a device at @a sampleRate, @a targetFrames long, dropping
	 *  @a latencyFrames from the head. False while another take is in hand (not Idle). Call with
	 *  the engine held (AudioEngine::requestChangeInModel). */
	bool arm(const QString& trackId, const QString& device, sample_rate_t sampleRate,
		f_cnt_t latencyFrames, std::uint64_t targetFrames);
	//! End the take early: the frames taken so far are the take. Idempotent. Call with the engine held.
	void stop();
	//! Drop whatever is armed or recorded. Call with the engine held.
	void cancel();

	//! The finished take (nullptr when it holds no frames) while the state is Done. The buffer is
	//! built on the drain thread; like finish() below this joins it (a few ms at most), so call it
	//! from the control thread, never from the audio thread.
	std::shared_ptr<const SampleBuffer> takeFinished();
	//! Back to Idle after a land (or a refused land whose take the caller drops).
	void releaseFinished();

	State state() const noexcept { return static_cast<State>(m_state.load(std::memory_order_acquire)); }
	QString trackId() const { return m_trackId; }
	QString device() const { return m_device; }
	std::uint64_t targetFrames() const noexcept { return m_target; }
	std::uint64_t framesRecorded() const noexcept { return m_recorded.load(std::memory_order_relaxed); }
	//! Frames the accumulator's ring had to drop (a starved drain thread).
	std::uint64_t framesDropped() const noexcept;
	sample_rate_t sampleRate() const noexcept { return m_sampleRate; }
	f_cnt_t latencyFrames() const noexcept { return m_latency; }

	// ---- audio thread -------------------------------------------------------------------------

	//! One rendered period of the engine's stereo input. While idle this is one atomic load.
	void push(const SampleFrame* input, f_cnt_t frames) noexcept;

private:
	void setState(State state) noexcept { m_state.store(static_cast<int>(state), std::memory_order_release); }
	void finishTake() noexcept;

	std::atomic<int> m_state{static_cast<int>(State::Idle)};
	QString m_trackId;
	QString m_device;
	std::unique_ptr<SampleRecordAccumulator> m_accum;
	std::shared_ptr<const SampleBuffer> m_result;
	sample_rate_t m_sampleRate = 0;
	f_cnt_t m_latency = 0;
	std::uint64_t m_target = 0;
	std::atomic<std::uint64_t> m_recorded{0};

	// Audio thread's own bookkeeping; reset by arm() while the engine is held.
	std::uint64_t m_skip = 0;
	std::uint64_t m_taken = 0;
};

} // namespace lmms

#endif // LMMS_SAMPLER_TAKE_RECORDER_H
