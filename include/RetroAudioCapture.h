/*
 * RetroAudioCapture.h - retrospective AUDIO capture: one bounded recent-frames
 *                       ring per engine, off by default, plus the take writer
 *                       that turns a retained window into a WAV (0.3.0, feature
 *                       row 16).
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

#ifndef LMMS_RETRO_AUDIO_CAPTURE_H
#define LMMS_RETRO_AUDIO_CAPTURE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <QString>

#include "LmmsTypes.h"
#include "RetroAudioRing.h"
#include "SampleFrame.h"

namespace lmms
{


//! The AUDIO side of retrospective capture (feature row 16; docs/MIDI-RETRO-CAPTURE.md
//! is the MIDI half's design record and the model this mirrors).
/*!
 * OFF BY DEFAULT. Nothing is recorded until arm(true) is called, and the whole
 * cost on the audio thread while disarmed is one relaxed atomic load per
 * rendered period - the contract RetroMidiCapture documents for the MIDI input
 * thread, which is the same contract this one keeps for the audio thread.
 *
 * THE MODEL, COPIED DELIBERATELY FROM RetroMidiCapture:
 *  - arm()/isArmed() are GUI/control-thread calls; the arm flag is runtime
 *    state, NOT project state, so arming records no transaction;
 *  - push() is the audio thread's entry point: it allocates nothing, takes no
 *    lock, makes no syscall, never waits and touches no Model, Song or Track;
 *  - the ring's storage is allocated exactly once, in this object's constructor,
 *    which must run off the audio thread;
 *  - the consumer (the UI thread running a command) takes a consistent window
 *    with the ring's publication-sequence handshake.
 *
 * WHERE IT DIFFERS FROM THE MIDI HALF, and why:
 *  - the source of the frames is the ENGINE'S OWN INPUT PATH
 *    (AudioEngine::inputWideBuffer()/inputWideFrames(), feature row 64), not a
 *    device callback: retrospective AUDIO capture is the engine recording what
 *    the input path had, so it needs no extra device and no extra thread;
 *  - the capacity is a FRAME count and the default is stated in seconds at
 *    48 kHz, because for audio the interesting quantity is "how long ago" and
 *    for MIDI it was "how many events";
 *  - the take it produces is a 24-bit WAV written in ONE pass, off the audio
 *    thread, with libsndfile - the same encoder TrackRecorder uses for a
 *    forward capture, so a retrospective take and a recorded take are the same
 *    kind of file.
 *
 * WHAT 0.3.0 DOES NOT DO with the window: it is written to a file, it is not
 * inserted into the session as a clip - the same absence record.recovery_restore
 * states for a recovered take, and for the same reason (building a SampleClip
 * from a file is the product's own load path and it is not this group's job to
 * invent a second one). docs/KNOWN-LIMITATIONS.md carries the line.
 */
class RetroAudioCapture
{
public:
	//! 2^20 frames = 8 MiB, paid once per engine, and about 21.8 s at 48 kHz /
	//! 11.9 s at 96 kHz. Stated in seconds rather than in events because the
	//! window's measurable property is TIME: what you played before you
	//! pressed record.
	static constexpr std::size_t DefaultCapacityFrames = 1u << 20;

	RetroAudioCapture();

	RetroAudioCapture(const RetroAudioCapture&) = delete;
	RetroAudioCapture& operator=(const RetroAudioCapture&) = delete;

	//! Arm or disarm the capture. Returns the new state. Mode state, never part
	//! of a project, never a transaction.
	bool arm(bool enabled) noexcept
	{
		m_armed.store(enabled, std::memory_order_relaxed);
		return enabled;
	}

	//! True while frames are being recorded. False on construction.
	bool isArmed() const noexcept { return m_armed.load(std::memory_order_relaxed); }

	//! Audio thread: record one rendered period of the engine's input.
	//! Realtime-safe (see the class comment). While disarmed this is one
	//! relaxed atomic load and nothing else.
	void push(const SampleFrame* frames, f_cnt_t count) noexcept;

	//! The ring behind this capture (the consumer side, the UI/control thread).
	RetroAudioRing& ring() noexcept { return m_ring; }
	const RetroAudioRing& ring() const noexcept { return m_ring; }

	//! One consistent copy of the retained window, oldest first.
	struct Window
	{
		std::vector<SampleFrame> frames;
		bool quiesced = false;    //!< the producer acknowledged before the copy
		std::size_t refused = 0;  //!< frames retained but not readable just now
	};

	//! Consumer side. Never blocks the audio thread: the producer is asked to
	//! stand still, waited for briefly, and released again even when the wait
	//! timed out (the same handshake midi.retro_capture_status uses).
	Window takeWindow() const;

private:
	RetroAudioRing m_ring;
	std::atomic<bool> m_armed{false};
} ;


//! Write \a window to \a filePath as a 24-bit WAV at \a sampleRate.
/*!
 * Off the audio thread by construction (it is called by a command handler).
 * Returns false with \a error filled when the file cannot be opened or a write
 * fails; \a framesWritten receives the frames libsndfile accepted, which is
 * measured, never assumed. An empty window is refused rather than written as a
 * zero-length file: a take with no audio in it is not a take.
 */
bool writeRetroAudioTake(const QString& filePath, const std::vector<SampleFrame>& window,
	int sampleRate, std::uint64_t* framesWritten, QString* error);


} // namespace lmms

#endif // LMMS_RETRO_AUDIO_CAPTURE_H
