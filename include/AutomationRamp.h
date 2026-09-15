/*
 * AutomationRamp.h - a per-sample automation ramp for ONE audio block
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

#ifndef LMMS_AUTOMATION_RAMP_H
#define LMMS_AUTOMATION_RAMP_H

#include <cstdint>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

/*! Sample-accurate automation for one audio block (feature-list row 9).
 *
 * THE BEHAVIOUR THIS REPLACES. `Song::processAutomations()` has always been a
 * per-TICK pass, and the per-sample buffer a track or a channel actually
 * multiplies its samples with (`AutomatableModel::valueBuffer()`) was filled by
 * interpolating from the value the model had when the previous block ended to
 * the value the FIRST tick of this block applied. Two things follow, and both
 * are audible on a fast move: the parameter is a whole block late (the samples
 * at the block's start still carry the previous block's value), and the move
 * itself is smeared over the whole block whatever the curve's own shape is.
 * `include/AudioEngine.h` names the second half of that in its own words -
 * "per-buffer updates like non-sample-accurate automation".
 *
 * WHAT A RAMP IS. The automation curve is stored as nodes on integer ticks and
 * its value is piecewise linear in ticks between two nodes, so a block needs
 * no more than one knot per tick boundary inside it: between two knots the
 * value moves in a straight line, and a knot per tick is therefore the curve
 * itself, sampled per audio frame. A ramp is a bounded array of (frame, value)
 * knots - nothing else - so:
 *
 *  * no allocation: the array is a member, and `MaxKnots` is the whole bound;
 *  * no locking: a ramp is written and read by the audio thread alone, under
 *    the clip's own (pre-existing) mutex while it is being BUILT, and lock-free
 *    once published;
 *  * no unbounded growth: a knot that does not fit is refused and COUNTED
 *    (`refusals()`), so a block whose tempo packs more tick boundaries than the
 *    capacity can hold degrades to the knots around them and says so, instead
 *    of allocating for it.
 *
 *  A ramp with ONE knot is the block-quantised behaviour (the old case): the
 *  value is held for the whole block. `sampleAccurate()` is the difference the
 *  surface and the tests report.
 */
class LMMS_EXPORT AutomationRamp
{
public:
	/*! Knots one ramp can hold: two per tick boundary inside a block (the frame
	 *  before the boundary and the frame on it) plus the block's two ends, so
	 *  ~15 ticks - about 400 BPM with a 512-frame block at 44.1 kHz and 192
	 *  ticks per bar. Each knot is 16 bytes, i.e. the ramp is a 528-byte member
	 *  of the model. A block denser than that is refused a knot at a time and
	 *  counted, never allocated for. */
	static constexpr int MaxKnots = 32;

	//! One knot: a frame offset inside the block and the value there.
	struct Knot
	{
		f_cnt_t frame;
		float value;
	};

	//! Reset to an empty ramp for a block of @a frames frames.
	void reset(f_cnt_t frames) noexcept;

	/*! Append the knot at @a frame (frames must not go BACKWARDS; two knots at
	 *  one frame are allowed and the LATER one wins, which is how a step in the
	 *  curve is represented). Returns false and counts a refusal when the frame
	 *  order is broken or the capacity is full. */
	bool addKnot(f_cnt_t frame, float value) noexcept;

	//! One knot at frame 0: the block-quantised behaviour, for comparison.
	void setBlockValue(float value) noexcept;

	//! The value at @a frame: linear between two knots, held outside them.
	float valueAt(f_cnt_t frame) const noexcept;

	//! More than one knot, i.e. the value MOVES inside the block.
	bool sampleAccurate() const noexcept { return m_knotCount > 1; }

	int knotCount() const noexcept { return m_knotCount; }
	f_cnt_t frames() const noexcept { return m_frames; }
	//! Knots refused since the last reset() - capacity or order.
	std::uint32_t refusals() const noexcept { return m_refusals; }

	//! Knot @a index, for the surface and the tests. Out of range returns
	//! the empty knot rather than reading past the array.
	const Knot& knot(int index) const noexcept;

private:
	Knot m_knots[MaxKnots];
	int m_knotCount = 0;
	f_cnt_t m_frames = 0;
	std::uint32_t m_refusals = 0;
};

} // namespace lmms

#endif // LMMS_AUTOMATION_RAMP_H
