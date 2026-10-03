/*
 * DeltaTap.h - hear only what a device changes: its output minus its input
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

#ifndef LMMS_DELTA_TAP_H
#define LMMS_DELTA_TAP_H

#include <atomic>
#include <vector>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class AudioBuffer;
class AudioBus;
class Effect;

/*! 0.5.0 L10 (MX-080): a device's delta - what it adds or takes away - instead of its output. While
 *  on, the block a device receives is kept, and after the device has processed it the kept block is
 *  subtracted: a bypassed device then sounds silent, a compressor sounds as the part it removes
 *  (inverted), a saturator as the harmonics it adds. A monitor switch, like the listen bus: session
 *  state, neither saved nor undone.
 *
 *  The scratch block is allocated on the control thread, under the engine's model-change guard,
 *  before the switch reads on; the audio thread copies and subtracts within it and never allocates.
 *  A block longer than the scratch (the period grew since) is passed through unchanged. */
class LMMS_EXPORT DeltaTap
{
public:
	//! Control thread: on with room for @a frames-frame blocks, or off.
	void setEnabled(bool on, f_cnt_t frames);
	bool enabled() const noexcept { return m_on.load(std::memory_order_acquire); }

	//! Audio thread: @a effect on @a bus (interleaved stereo), as its delta while the tap is on.
	bool process(Effect& effect, AudioBus& bus) noexcept;
	//! Audio thread: the same on a planar block (the routing graph's effect nodes).
	bool process(Effect& effect, AudioBuffer& buffer) noexcept;

private:
	std::vector<float> m_dry;
	std::atomic<bool> m_on{false};
};

} // namespace lmms

#endif // LMMS_DELTA_TAP_H
