/*
 * PunchWindow.h - R2.4: which frames of one engine period the punch region lets the recorders take
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

#ifndef LMMS_PUNCH_WINDOW_H
#define LMMS_PUNCH_WINDOW_H

#include <algorithm>
#include <cmath>

#include "LmmsTypes.h"

namespace lmms
{

//! The frames [begin, end) of one period that reach the armed recorders.
struct PunchFrames
{
	f_cnt_t begin = 0;
	f_cnt_t end = 0;
	f_cnt_t count() const { return end > begin ? end - begin : 0; }
};

/*! The punch region's audio gate, FRAME-accurately (relief plan R2.4). The region
 *  [punchBegin, punchEnd) is in ticks on the song timeline; the period starts at song frame
 *  @a periodStartFrame (ticks * framesPerTick + the frame offset within the tick) and lasts
 *  @a frames frames. A frame f of the period is captured iff its song position lies in the
 *  region, i.e. iff  periodStartFrame + f  is in  [punchBegin * fpt, punchEnd * fpt).
 *
 *  - No armed region: every frame is captured, exactly as before the gate existed.
 *  - An armed region while the song is not playing: nothing (a punch records a pass of the
 *    timeline; with the transport stopped there is no position to be inside).
 *  - Otherwise the intersection, which makes pre-roll and post-roll free: the transport
 *    runs before punch-in and after punch-out while the gate stays shut.
 *
 *  Pure and allocation-free: the render thread calls it once per period. */
inline PunchFrames punchFramesForPeriod(bool armed, bool songPlaying, double periodStartFrame,
	double framesPerTick, f_cnt_t frames, tick_t punchBegin, tick_t punchEnd) noexcept
{
	if (!armed) { return {0, frames}; }
	if (!songPlaying || framesPerTick <= 0.0 || punchEnd <= punchBegin) { return {0, 0}; }
	const double from = std::ceil(static_cast<double>(punchBegin) * framesPerTick - periodStartFrame);
	const double to = std::ceil(static_cast<double>(punchEnd) * framesPerTick - periodStartFrame);
	const auto clampFrame = [frames](double value) {
		return static_cast<f_cnt_t>(std::clamp(value, 0.0, static_cast<double>(frames)));
	};
	return {clampFrame(from), clampFrame(to)};
}

} // namespace lmms

#endif // LMMS_PUNCH_WINDOW_H
