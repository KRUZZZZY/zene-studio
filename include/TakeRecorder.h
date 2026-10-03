/*
 * TakeRecorder.h - an insert that records what passes it to a WAV file, off the audio thread
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

#ifndef LMMS_TAKE_RECORDER_H
#define LMMS_TAKE_RECORDER_H

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include <QString>

#include <sndfile.h>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class Effect;
class SampleFrame;

/*! 0.5.0 L10 (MX-156): what records the stereo signal passing an insert (the Recorder device) to a
 *  24-bit WAV take. The audio thread copies each block into a pre-allocated single-producer /
 *  single-consumer ring (no allocation, no lock; a full ring drops and counts); a disk-writer thread
 *  started by start() writes it out; stop() drains it, joins the thread and closes the file. A
 *  device owns one and attaches it under its own Effect, which is how plugin.record and
 *  plugin.recorder_land find it. start(), stop() and the registry are control-thread calls. */
class LMMS_EXPORT TakeRecorder
{
public:
	static constexpr std::size_t RingFrames = std::size_t{1} << 17;
	static constexpr std::size_t BatchFrames = 4096;

	TakeRecorder();
	~TakeRecorder();
	TakeRecorder(const TakeRecorder&) = delete;
	TakeRecorder& operator=(const TakeRecorder&) = delete;

	//! Open @a path as a stereo take at @a sampleRate and start writing; false (with @a why) when one
	//! is already being written or the file cannot be opened.
	bool start(const QString& path, int sampleRate, QString* why);
	//! Write out what is left and close the take. Idempotent.
	void stop();
	bool recording() const noexcept { return m_recording.load(std::memory_order_acquire); }

	//! Audio thread: @a frames stereo frames into the ring while recording.
	void push(const SampleFrame* frames, f_cnt_t count) noexcept;

	//! The last take started (kept after stop(), saved by the device), and the frames written and
	//! dropped since it started.
	const QString& lastTake() const { return m_lastTake; }
	void setLastTake(const QString& path) { m_lastTake = path; }
	std::uint64_t framesWritten() const noexcept { return m_written.load(std::memory_order_relaxed); }
	std::uint64_t framesDropped() const noexcept { return m_dropped.load(std::memory_order_relaxed); }

	//! The registry: @a effect's recorder (attach() / detach() from the device's constructor and
	//! destructor), or nullptr for a device that records nothing.
	void attach(const Effect* effect);
	void detach();
	static TakeRecorder* of(const Effect* effect);

	//! A new take's default path: the user's sample folder, Recordings/, by the time it started.
	static QString defaultTakePath();

private:
	void writerLoop();
	//! Writes the ring's frames from @a read on; false when there were none.
	bool writeAvailable();

	std::vector<float> m_ring;
	std::vector<float> m_batch;
	std::atomic<std::size_t> m_write{0};
	std::atomic<std::size_t> m_read{0};
	std::atomic<bool> m_recording{false};
	std::atomic<bool> m_stopping{false};
	std::atomic<std::uint64_t> m_written{0};
	std::atomic<std::uint64_t> m_dropped{0};
	std::thread m_thread;
	SNDFILE* m_file = nullptr;
	QString m_lastTake;
	const Effect* m_owner = nullptr;
};

} // namespace lmms

#endif // LMMS_TAKE_RECORDER_H
