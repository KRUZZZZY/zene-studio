/*
 * TrackRecorder.h - per-track capture stream for the two-track recording
 *                   prototype: one input channel -> lock-free ring buffer ->
 *                   disk-writer thread -> 24-bit WAV
 *
 * Copyright (c) 2026 LMMS developers
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

#ifndef LMMS_TRACK_RECORDER_H
#define LMMS_TRACK_RECORDER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <sndfile.h>

#include "LmmsTypes.h"
#include "RecordRingBuffer.h"

namespace lmms
{

class SampleFrame;


//! One capture stream: a single interleaved input channel is demuxed on the
//! audio thread into a pre-allocated SPSC ring buffer and written to a 24-bit
//! WAV file by a dedicated disk-writer thread (prototype, task #556).
/*!
 * Threading contract:
 * - constructor: allocates the ring buffer and the writer scratch buffer;
 *   must run off the audio thread;
 * - arm()/disarm(): off the audio thread (GUI/control thread). All
 *   allocation, file I/O and thread management happens here;
 * - processInput(): audio thread only. Realtime-safe - no allocation, no
 *   locking, no syscalls;
 * - writerLoop(): the disk-writer thread. Does all sndfile I/O.
 */
class TrackRecorder
{
public:
	//! 65536 frames (~1.37 s at 48 kHz), power of two.
	static constexpr std::size_t RingCapacityFrames = 1u << 16;
	//! Frames per sndfile write on the disk-writer thread.
	static constexpr std::size_t WriteBatchFrames = 4096;

	TrackRecorder();
	~TrackRecorder();

	TrackRecorder(const TrackRecorder&) = delete;
	TrackRecorder& operator=(const TrackRecorder&) = delete;

	//! Select which interleaved input channel (0-based) this track records.
	//! Clamped to [0, DEFAULT_CHANNELS). Only meaningful while disarmed.
	void setInputChannel(int channel) noexcept;
	int inputChannel() const noexcept;

	//! Off the audio thread: reset the ring, open \a filePath as a 24-bit WAV
	//! at \a sampleRate and start the disk-writer thread. Returns false when
	//! already armed or the file cannot be opened.
	bool arm(const std::string& filePath, int sampleRate, int inputChannel);
	//! Off the audio thread: stop the writer, drain the ring and close the
	//! file. Idempotent.
	void disarm();

	bool isArmed() const noexcept { return m_armed.load(std::memory_order_acquire); }
	bool isWriterRunning() const noexcept { return m_writerRunning.load(std::memory_order_acquire); }

	//! Audio-thread entry point. Demuxes one channel of the interleaved input
	//! period into the ring buffer. Realtime-safe.
	void processInput(const SampleFrame* input, f_cnt_t frames) noexcept;

	//! Frames accepted by the ring buffer (audio thread).
	std::uint64_t framesPushed() const noexcept { return m_framesPushed.load(std::memory_order_relaxed); }
	//! Frames successfully written to disk (disk-writer thread).
	std::uint64_t framesRecorded() const noexcept { return m_framesRecorded.load(std::memory_order_relaxed); }
	//! Frames dropped because the ring buffer was full.
	std::uint64_t overflowCount() const noexcept { return m_ring->overflowCount(); }
	//! sndfile short/failed writes.
	std::uint64_t writeErrorCount() const noexcept { return m_writeErrors.load(std::memory_order_relaxed); }

	std::string filePath() const { return m_filePath; }

private:
	void writerLoop();

	std::unique_ptr<RecordRingBuffer> m_ring; // allocated in the constructor
	std::vector<sample_t> m_writeScratch;      // allocated in the constructor

	std::thread m_writerThread;
	SNDFILE* m_sf = nullptr;                   // disk-writer thread only

	std::atomic<bool> m_armed{false};
	std::atomic<bool> m_stopRequested{false};
	std::atomic<bool> m_writerRunning{false};
	std::atomic<int> m_inputChannel{0};
	std::atomic<std::uint64_t> m_framesPushed{0};
	std::atomic<std::uint64_t> m_framesRecorded{0};
	std::atomic<std::uint64_t> m_writeErrors{0};

	std::string m_filePath;
} ;


} // namespace lmms

#endif // LMMS_TRACK_RECORDER_H
