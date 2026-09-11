/*
 * LatencyCompensation.h - fixed-capacity delay line for plugin delay
 *                         compensation at a mixer summing point
 *
 * Copyright (c) 2026 Zene Studio developers
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

#ifndef LMMS_LATENCY_COMPENSATION_H
#define LMMS_LATENCY_COMPENSATION_H

#include <atomic>
#include <vector>

#include "SampleFrame.h"
#include "lmms_export.h"

namespace lmms
{

/**
 * Delay line used to align one signal path at a mixer summing point.
 *
 * All storage is allocated by init() on the control thread; the audio thread
 * only calls setDelayFrames()/process() and never allocates, locks or grows
 * anything. The delay may change at a period boundary (the mixer recomputes it
 * once per period before the workers run); a change steps the read offset and
 * is not cross-faded, which is the same behaviour commercial hosts have when
 * the routing changes.
 *
 * The history is always written, even while the delay is zero, so a later
 * compensation change reads real history instead of stale samples. With a zero
 * delay process() returns the caller's buffer untouched, so a graph with no
 * reported latency is bit-identical to the uncompensated mixer.
 */
class LMMS_EXPORT LatencyCompensation
{
public:
	//! Largest delay the line accepts. Longer requests are clamped.
	static constexpr int MaxFrames = 1 << 14; // 16384 frames ~ 0.37 s @ 44.1 kHz

	LatencyCompensation() = default;

	//! Control thread: allocate the history for blocks of \p framesPerPeriod
	//! frames. Must be called before the first process().
	void init(f_cnt_t framesPerPeriod);

	//! Audio thread: delay applied by the next process() call.
	void setDelayFrames(int frames);
	int delayFrames() const;

	//! Push one interleaved block and return the block delayed by
	//! delayFrames(). Returns \p in unchanged when the delay is zero.
	const SampleFrame* process(const SampleFrame* in, f_cnt_t frames);

	//! In-place variant for a source that owns its buffer.
	void processInPlace(SampleFrame* buf, f_cnt_t frames);

	//! In-place variant for a planar stereo buffer (e.g. AudioBuffer).
	void processPlanar(float* left, float* right, f_cnt_t frames);

	//! Advance the history with silence (muted or empty periods), keeping the
	//! line's timeline aligned with the rest of the graph.
	void advanceSilence(f_cnt_t frames);

	//! Frames of history the line can hold (excluding the block being written).
	int capacity() const { return static_cast<int>(m_ring.size()); }

private:
	//! Effective delay, clamped so that read + write fit the ring.
	int effectiveDelay(f_cnt_t frames) const;

	//! Copy the \p frames-frame window starting at ring offset \p read into
	//! m_scratch, wrapping past the ring end; returns m_scratch.data().
	//! Callers guarantee m_ring.size() >= \p frames and
	//! m_scratch.size() >= \p frames. Audio thread only; never allocates.
	const SampleFrame* readWrapped(f_cnt_t read, f_cnt_t frames);

	std::vector<SampleFrame> m_ring;
	std::vector<SampleFrame> m_scratch;
	f_cnt_t m_write = 0;
	std::atomic<int> m_delay{0};
};

} // namespace lmms

#endif // LMMS_LATENCY_COMPENSATION_H
