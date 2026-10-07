/*
 * SessionAudioRecorder.h - record AUDIO into a launcher cell (0.5.0 L02, WF-055)
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

#ifndef LMMS_SESSION_AUDIO_RECORDER_H
#define LMMS_SESSION_AUDIO_RECORDER_H

#include <atomic>
#include <cstdint>
#include <memory>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class SampleBuffer;
class SampleFrame;
class SampleRecordAccumulator;

/*! One audio take recorded into a launcher cell (WF-055; docs/SESSION-AUDIO-RECORD.md).
 *
 *  THE SHAPE. A cell is ARMED (session.record_slot), then LAUNCHED through the session scheduler like
 *  any cell, so the take starts on the launch quantisation's grid line. It ends on the cell's stop -
 *  session.stop_slot's quantised stop, another cell taking the column over, a reset - or after a fixed
 *  length the arm asked for. What the take holds is the engine's stereo input bus (the frames
 *  SampleRecordHandle records into an arrangement clip), kept by the clip recorder's own accumulator
 *  (SampleRecordAccumulator: a pre-allocated ring the audio thread writes and a drain thread empties).
 *
 *  ALIGNMENT. The take is shifted by the engine's recording round trip (AudioEngine::
 *  recordingLatencyFrames, the R2.2 figure SampleRecordHandle drops from a take's head): the first
 *  `latency` input frames after the start are skipped, and the take runs on past the stop until it is
 *  exactly as long as the span it was recorded for - (stop tick - start tick) x frames per tick, or the
 *  fixed length. So a take recorded for one bar is one bar of frames, and it lines up with what was
 *  playing when it was performed.
 *
 *  THREADS. arm(), cancel(), takeFinished() and the readings are control-thread calls. The audio
 *  thread calls slotStarted()/slotStopped() from the scheduler's launch events and push() once per
 *  rendered period; none of them allocates, locks or logs - the accumulator was made by arm().
 *  The audio thread touches the accumulator only while the state is Recording or Finishing, and it
 *  never leaves Done on its own, so takeFinished() may take the accumulator once it reads Done.
 *  cancel() can interrupt a take mid-period, so it must run while no period renders: the command
 *  holds the engine's model-change lock around it. */
class LMMS_EXPORT SessionAudioRecorder
{
public:
	enum class State : int
	{
		Idle = 0,   //!< nothing armed
		Armed,      //!< waiting for the cell's launch
		Recording,  //!< the cell plays; input is taken
		Finishing,  //!< the cell stopped; the take runs on to its length
		Done        //!< the take is complete and waits for takeFinished()
	};

	//! The accumulator's ring: 2^18 frames (~5.5 s at 48 kHz), so a starved drain thread drops nothing
	//! for seconds. 2 MiB, made by arm(), never on the audio thread.
	static constexpr std::size_t RingFrames = std::size_t{1} << 18;

	SessionAudioRecorder();
	~SessionAudioRecorder();
	SessionAudioRecorder(const SessionAudioRecorder&) = delete;
	SessionAudioRecorder& operator=(const SessionAudioRecorder&) = delete;

	/*! Arms cell (@a track, @a scene) to record at @a sampleRate, @a lengthTicks long (0: until the cell
	 *  stops), dropping @a latencyFrames from the head. False while another take is in hand (not Idle). */
	bool arm(int track, int scene, tick_t lengthTicks, sample_rate_t sampleRate, f_cnt_t latencyFrames);
	//! Drops whatever is armed or recorded. Call with no period rendering (see the class comment).
	void cancel();
	//! The finished take (nullptr when it holds no frames) once the state is Done; back to Idle.
	std::shared_ptr<const SampleBuffer> takeFinished();

	State state() const noexcept { return static_cast<State>(m_state.load(std::memory_order_acquire)); }
	int track() const noexcept { return m_track.load(std::memory_order_relaxed); }
	int scene() const noexcept { return m_scene.load(std::memory_order_relaxed); }
	//! Frames handed to the take so far, and the length it will have (0 until it is known).
	std::uint64_t framesRecorded() const noexcept { return m_recorded.load(std::memory_order_relaxed); }
	std::uint64_t targetFrames() const noexcept { return m_targetReading.load(std::memory_order_relaxed); }
	//! The grid line the take started on (-1 before it started).
	tick_t startTick() const noexcept { return m_startTick.load(std::memory_order_relaxed); }
	//! Frames the accumulator's ring had to drop (a starved drain thread).
	std::uint64_t framesDropped() const noexcept;
	f_cnt_t latencyFrames() const noexcept { return m_latency; }
	sample_rate_t sampleRate() const noexcept { return m_sampleRate; }

	// ---- audio thread -------------------------------------------------------------------------

	/*! The scheduler started cell (@a track, @a scene) on grid line @a startedTick. Returns the fixed
	 *  length in ticks the cell must stop after (0: none) when this is the armed cell, else 0. */
	tick_t slotStarted(int track, int scene, tick_t startedTick, float framesPerTick) noexcept;
	//! The cell stopped (or was taken over or reset) at @a stopTick: the take's length is now known.
	void slotStopped(int track, int scene, tick_t stopTick) noexcept;
	/*! One rendered period of the engine's stereo input. While nothing records this is one atomic load.
	 *  @a streamStarted / @a periodFrames: a period before the capture delivered anything counts against
	 *  the skipped head, as SampleRecordHandle counts it (BUGS_FOUND 11.29). */
	void push(const SampleFrame* input, f_cnt_t frames, bool streamStarted, f_cnt_t periodFrames) noexcept;

private:
	void setState(State state) noexcept { m_state.store(static_cast<int>(state), std::memory_order_release); }
	//! Hands @a frames frames to the accumulator, never past the target. Audio thread.
	void take(const SampleFrame* input, f_cnt_t frames) noexcept;

	std::atomic<int> m_state{static_cast<int>(State::Idle)};
	std::atomic<int> m_track{-1};
	std::atomic<int> m_scene{-1};
	std::unique_ptr<SampleRecordAccumulator> m_accum;
	tick_t m_lengthTicks = 0;
	sample_rate_t m_sampleRate = 0;
	f_cnt_t m_latency = 0;

	// audio thread's own bookkeeping, published through the atomics below for the readings
	float m_framesPerTick = 0.0f;
	std::uint64_t m_skip = 0;
	std::uint64_t m_target = 0;  //!< 0: not known yet
	std::uint64_t m_taken = 0;
	std::atomic<std::uint64_t> m_recorded{0};
	std::atomic<std::uint64_t> m_targetReading{0};
	std::atomic<tick_t> m_startTick{-1};
};

} // namespace lmms

#endif // LMMS_SESSION_AUDIO_RECORDER_H
