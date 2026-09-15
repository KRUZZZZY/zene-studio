/*
 * AudioWideInputStage.h - the N-CHANNEL capture staging stage: a lock-free
 *                         SPSC ring between a backend's capture thread and the
 *                         render thread, drained once per rendered period into
 *                         a pre-allocated buffer this stage owns (0.3.0,
 *                         feature row 64).
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

#ifndef LMMS_AUDIO_WIDE_INPUT_STAGE_H
#define LMMS_AUDIO_WIDE_INPUT_STAGE_H

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "InputChannelRing.h"
#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{


/*! The engine's N-CHANNEL input stage (feature row 64 "Arbitrary input count /
 *  multiple simultaneous inputs").
 *
 * WHY IT IS A SEPARATE OBJECT AND NOT FOUR MORE MEMBERS ON AudioEngine.
 * AudioEngine already has the stereo input path
 * (AudioEngine::pushInputFrames / drainInputStage / m_inputStage) and that path
 * is stereo BY TYPE: a SampleFrame is two floats. The wide path needs a ring, a
 * pair of pre-allocated destination buffers, a width and a frame count, and
 * putting them on AudioEngine would grow an inherited class that the file-length
 * ratchet already measures at its recorded size. Here they are one object with
 * one contract, and the engine holds one pointer to it.
 *
 * THE CONTRACT, IN FULL:
 *  - construction must run off the audio thread and off any capture thread. It
 *    allocates the ring and both destination buffers, once, and never again;
 *  - push() runs on the backend's capture thread. It allocates nothing, takes
 *    no lock, makes no syscall, and never blocks. A block wider than this
 *    stage's width is refused whole and counted (see droppedFrames());
 *  - drain() runs on the render thread, once per rendered period, BEFORE any
 *    consumer reads data(). It moves at most min(staged, capacityFrames)
 *    frames - bounded by both sides, so it can neither allocate nor overrun;
 *  - data()/frames()/channels() are the render thread's view of the most
 *    recently drained period. The buffer is double-buffered, so a drain never
 *    writes the period a consumer is reading.
 *
 * With no capture backend, frames() is 0 and data() is a zeroed buffer: the
 * "a record route can take more than zero inputs" claim is exactly what
 * frames() > 0 measures.
 */
class LMMS_EXPORT AudioWideInputStage
{
public:
	//! \a maxChannels is the widest block push() will accept; \a capacityFrames
	//! bounds how much a single drain can hand to the render thread.
	AudioWideInputStage(int maxChannels, f_cnt_t capacityFrames);
	~AudioWideInputStage();

	AudioWideInputStage(const AudioWideInputStage&) = delete;
	AudioWideInputStage& operator=(const AudioWideInputStage&) = delete;

	//! Capture thread: stage one interleaved block. Returns the frames staged.
	//! Realtime-safe. A block whose width differs from \a channels - or from
	//! the width of the block staged before it - is refused whole: the stage
	//! carries ONE width at a time, because a buffer whose width changed
	//! mid-period cannot be read back without the reader knowing which frames
	//! had which width.
	std::size_t push(const float* interleaved, int channels, f_cnt_t frames) noexcept;

	//! Render thread: move the staged frames into the read buffer. Idempotent
	//! when nothing is staged (frames() becomes 0).
	void drain() noexcept;

	//! Render thread: the most recently drained period.
	const float* data() const noexcept { return m_buffers[m_read].data(); }
	f_cnt_t frames() const noexcept { return m_frames[m_read]; }
	int channels() const noexcept { return m_channels[m_read]; }

	//! Frames a push() refused, whole (width mismatch) or in part (ring full).
	std::uint64_t droppedFrames() const noexcept { return m_dropped.load(std::memory_order_relaxed); }
	//! Blocks refused because their width was not this stage's current width.
	std::uint64_t refusedBlocks() const noexcept { return m_refused.load(std::memory_order_relaxed); }
	//! Frames staged and not yet drained.
	std::size_t stagedFrames() const noexcept { return m_stage != nullptr ? m_stage->available() : 0u; }

	//! The widest block this stage accepts.
	int maxChannels() const noexcept { return m_maxChannels; }
	f_cnt_t capacityFrames() const noexcept { return m_capacityFrames; }

private:
	//! Slots in the double buffer: one being drained into, one being read.
	static constexpr int kBufferCount = 2;

	const int m_maxChannels;
	const f_cnt_t m_capacityFrames;

	std::unique_ptr<InputChannelRing> m_stage;
	std::vector<std::vector<float>> m_buffers;
	f_cnt_t m_frames[kBufferCount];
	int m_channels[kBufferCount];
	int m_read = 0;
	int m_write = 1;

	std::atomic<std::uint64_t> m_dropped{0};
	std::atomic<std::uint64_t> m_refused{0};
} ;


} // namespace lmms

#endif // LMMS_AUDIO_WIDE_INPUT_STAGE_H
