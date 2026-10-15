/*
 * PhaseScopeTap.h - 0.5.0 L08 (MX-137): one mixer channel's newest stereo frames, for the phase view
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

#ifndef LMMS_PHASE_SCOPE_TAP_H
#define LMMS_PHASE_SCOPE_TAP_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class SampleFrame;

/*! 0.5.0 L08 (MX-137, the stereo phase view): the newest frames of ONE mixer channel - the block its
 *  correlation is measured from (mixerstrip::updateMeters) - kept left and right in a fixed ring, for the
 *  goniometer (PhaseScopeView.h) to draw. The tuner tap's pattern (TunerTap.h), kept stereo.
 *
 *  AUDIO THREAD (a mixer worker): push() allocates nothing, takes no lock and makes no call; for a channel
 *  that is not the chosen one (the default: none) it is one relaxed atomic load. The ring is two fixed arrays
 *  of atomics in static storage, so nothing is ever allocated for it, on any thread. Channels are processed
 *  on several workers at once, but only the chosen channel's push writes; a second push that finds one in
 *  flight - possible only in the period the choice changes - is dropped at once, never waited for.
 *  GUI / CONTROL THREAD: setChannel(), channel(), written() and latest(), which copies the newest frames and
 *  refuses a copy the producer overran - or one a concurrent setChannel() or a fresh push rebased - while it
 *  was taken (the caller reads again on its next refresh). */
class LMMS_EXPORT PhaseScopeTap
{
public:
	static constexpr int Off = -1;
	//! 4096 frames: 85 ms at 48 kHz - more than one 40 ms refresh of the view.
	static constexpr std::size_t Capacity = 4096;

	static PhaseScopeTap& instance();

	//! Choosing a channel (or Off) forgets what came in, so a picture never mixes two channels.
	void setChannel(int index) noexcept
	{
		m_channel.store(index, std::memory_order_relaxed);
		reset();  // after the store: a push that takes this reset is ordered after it, and sees the new channel
	}
	int channel() const noexcept { return m_channel.load(std::memory_order_relaxed); }

	//! AUDIO THREAD: keep @a count frames of @a frames when channel @a index is the chosen one.
	void push(int index, const SampleFrame* frames, f_cnt_t count) noexcept;

	//! The newest @a count (<= Capacity) frames, oldest first, into @a left and @a right; false when fewer
	//! have come in since the channel was chosen, or the copy was overrun.
	bool latest(float* left, float* right, std::size_t count) const noexcept;

	//! Frames pushed since the channel was chosen (0 until the producer takes the reset): a view that sees it
	//! unchanged knows nothing new came in - the channel is muted, or the engine stopped.
	std::uint64_t written() const noexcept
	{
		return m_resetPending.load(std::memory_order_acquire) ? 0 : m_head.load(std::memory_order_acquire);
	}

	//! A request the audio thread takes at its next push, so a push in flight cannot overwrite it; until then
	//! latest() reports nothing. The generation moves too, so a copy latest() already started is refused
	//! rather than returned (setChannel() and latest() may run on the control and GUI threads respectively).
	void reset() noexcept
	{
		m_resetPending.store(true, std::memory_order_release);
		m_generation.fetch_add(2, std::memory_order_release);  // even: only a fresh push may make it odd
	}

private:
	std::array<std::atomic<float>, Capacity> m_left{};
	std::array<std::atomic<float>, Capacity> m_right{};
	std::atomic<std::uint64_t> m_head{0};
	//! Where the push in progress ends: stored before its slots are written (m_head after), so latest() can
	//! see a write that has not been published yet overlapping its window.
	std::atomic<std::uint64_t> m_reserved{0};
	std::atomic<bool> m_resetPending{false};
	std::atomic<bool> m_writing{false};  //!< a push is in flight: a second producer drops its block
	std::atomic<int> m_channel{Off};
	//! Even while the ring is stable, odd while a fresh (post-reset) push rebases it: latest() samples it
	//! before and after its copy and refuses a window a channel change or rebase stole half of.
	std::atomic<std::uint64_t> m_generation{0};
};

} // namespace lmms

#endif // LMMS_PHASE_SCOPE_TAP_H
