/*
 * TrackRecorder.h - per-track capture stream for the two-track recording
 *                   prototype: one input channel -> lock-free ring buffer ->
 *                   disk-writer thread -> 24-bit WAV
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

#ifndef LMMS_TRACK_RECORDER_H
#define LMMS_TRACK_RECORDER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <QString>

#include <sndfile.h>

#include "LmmsTypes.h"
#include "RecordRingBuffer.h"
#include "lmms_constants.h"  // DEFAULT_CHANNELS: the engine bus's own width

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

#ifdef LMMS_TESTING
	//! Inject one portable writer-start failure for lifecycle regression tests.
	static void failNextWriterThreadStartForTesting() noexcept;
#endif

	TrackRecorder();
	~TrackRecorder();

	TrackRecorder(const TrackRecorder&) = delete;
	TrackRecorder& operator=(const TrackRecorder&) = delete;

	//! Select which interleaved input channel (0-based) this track records.
	//! Clamped to [0, inputChannelCapacity()). Only meaningful while disarmed.
	void setInputChannel(int channel) noexcept;
	int inputChannel() const noexcept;

	//! How many interleaved input channels this route may select from (0.3.0,
	//! feature row 64 "Arbitrary input count"). With no capture device open the
	//! engine's bus is two channels wide, so that is the default; an N-channel
	//! interface raises it. Off the audio thread.
	void setInputChannelCapacity(int channels);
	int inputChannelCapacity() const noexcept;
	//! True when \a channel can be selected: the check arm() makes, exposed so
	//! the control surface can refuse a bad channel with the range in the
	//! message instead of reporting a bare false.
	bool inputChannelSelectable(int channel) const noexcept;

	//! Off the audio thread: reset the ring, open \a filePath as a 24-bit WAV
	//! at \a sampleRate and start the disk-writer thread. Returns false when
	//! already armed, when \a inputChannel is not selectable, or when the file
	//! cannot be opened.
	bool arm(const std::string& filePath, int sampleRate, int inputChannel);
	//! Off the audio thread: stop the writer, drain the ring and close the
	//! file. Idempotent.
	void disarm();

	bool isArmed() const noexcept
	{
		return m_capturing.load(std::memory_order_acquire)
			&& !m_inputUnavailable.load(std::memory_order_acquire);
	}
	//! True after the audio thread sees a delivered block narrower than the
	//! selected channel, until the control thread retires the route.
	bool hasUnavailableInput() const noexcept
	{
		return m_inputUnavailable.load(std::memory_order_acquire);
	}
	bool isWriterRunning() const noexcept { return m_writerRunning.load(std::memory_order_acquire); }

	//! Audio-thread entry point. Demuxes one channel of the interleaved input
	//! period into the ring buffer. Realtime-safe.
	void processInput(const SampleFrame* input, f_cnt_t frames) noexcept;

	//! Audio-thread entry point for an ARBITRARY input width (0.3.0, feature
	//! row 64): \a interleaved holds \a frames frames of \a channels channels
	//! each, and this route reads channel inputChannel() from it. The
	//! SampleFrame overload is exactly this call with DEFAULT_CHANNELS.
	//! Realtime-safe: one ring-buffer store and one relaxed atomic add.
	void processInputInterleaved(const sample_t* interleaved, int channels,
		f_cnt_t frames) noexcept;

	//! Frames accepted by the ring buffer (audio thread).
	std::uint64_t framesPushed() const noexcept { return m_framesPushed.load(std::memory_order_relaxed); }
	//! Frames successfully written to disk (disk-writer thread).
	std::uint64_t framesRecorded() const noexcept { return m_framesRecorded.load(std::memory_order_relaxed); }
	//! Frames dropped because the ring buffer was full.
	std::uint64_t overflowCount() const noexcept { return m_ring->overflowCount(); }
	//! sndfile short/failed writes.
	std::uint64_t writeErrorCount() const noexcept { return m_writeErrors.load(std::memory_order_relaxed); }

	std::string filePath() const { return m_filePath; }

	//! The take journal's path while this capture is journalled, or empty when
	//! it is not (0.3.0). arm() writes the journal beside the take and disarm()
	//! removes it - a CLEAN stop leaves no journal, which is what makes "there is
	//! a journal" and "the capture died" the same fact. Empty also means the side
	//! file could not be written (a directory that refuses it): the recording
	//! still happened, it is simply not recoverable, and this accessor says so
	//! rather than the caller assuming protection.
	std::string journalPath() const { return m_journalPath; }
	//! Frames the journal had recorded at its last update (0.3.0). The bound the
	//! recovery offer reports as guaranteed is this number, read back from the
	//! file - see include/RecordingJournal.h.
	std::uint64_t journalledFrames() const noexcept { return m_journalFrames.load(std::memory_order_relaxed); }

	//! Commits made by the disk-writer (header rewrite + sync + journal) since arm(),
	//! and the nanoseconds it spent in them: the honest I/O cost of the short
	//! journal interval (Q050-045). Reset by arm().
	std::uint64_t commitCount() const noexcept { return m_commitCount.load(std::memory_order_relaxed); }
	std::uint64_t commitNanoseconds() const noexcept { return m_commitNanos.load(std::memory_order_relaxed); }

private:
	void writerLoop();
	//! Header rewrite + sync + journal write; disk-writer thread only.
	void commitTake();

	//! Writes the take journal with the frames recorded so far (0.3.0). Called
	//! from arm() and from writerLoop() - never from the audio thread.
	void writeJournal();

	std::unique_ptr<RecordRingBuffer> m_ring; // allocated in the constructor
	std::vector<sample_t> m_writeScratch;      // allocated in the constructor

	std::thread m_writerThread;
	SNDFILE* m_sf = nullptr;                   // disk-writer thread only

	std::atomic<bool> m_armed{false};
	//! What the audio thread reads: set by arm() only once the ring, the channel, the file and the
	//! writer are ready (m_armed is claimed first, so it cannot gate the audio path), cleared first
	//! by disarm().
	std::atomic<bool> m_capturing{false};
	std::atomic<bool> m_inputUnavailable{false};
	std::atomic<bool> m_stopRequested{false};
	std::atomic<bool> m_writerRunning{false};
	std::atomic<int> m_inputChannel{0};
	//! How many interleaved input channels a route may select from (0.3.0).
	//! DEFAULT_CHANNELS is the engine's own bus width and the value a capture
	//! path that delivers nothing else leaves it at.
	std::atomic<int> m_inputChannelCapacity{static_cast<int>(DEFAULT_CHANNELS)};
	std::atomic<std::uint64_t> m_framesPushed{0};
	std::atomic<std::uint64_t> m_framesRecorded{0};
	std::atomic<std::uint64_t> m_writeErrors{0};

	std::string m_filePath;

	// The take journal (0.3.0). m_journalPath is set in arm() before the writer
	// thread starts and cleared in disarm() after it is joined, so the writer
	// thread reads a value that cannot change under it; m_journalFrames is
	// written by arm() (before the thread exists) and by that thread only, and
	// is atomic because journalledFrames() reads it from the control thread
	// while that thread runs.
	std::atomic<std::uint64_t> m_commitCount{0};
	std::atomic<std::uint64_t> m_commitNanos{0};
	std::string m_journalPath;
	std::atomic<std::uint64_t> m_journalFrames{0};
	std::uint64_t m_journalIntervalFrames = 0;
	int m_journalSampleRate = 0;
	QString m_journalTake;
} ;


} // namespace lmms

#endif // LMMS_TRACK_RECORDER_H
