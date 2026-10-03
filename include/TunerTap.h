/*
 * TunerTap.h - the newest audio frames, for the tuner
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

#ifndef LMMS_TUNER_TAP_H
#define LMMS_TUNER_TAP_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class SampleFrame;

/*! 0.5.0 W3 L14 (WF-147, the tuner): the newest frames of one source - the engine's input or the master
 *  output - folded to mono in a fixed ring, for tuner.get_state to read the pitch of.
 *
 *  AUDIO THREAD: push() allocates nothing, takes no lock and makes no call; when its source is not the
 *  chosen one (the default: none) it is one relaxed atomic load. The ring is a fixed array of atomics in
 *  static storage, so nothing is ever allocated for it, on any thread.
 *  CONTROL THREAD: setSource() and latest(), which copies the newest frames and refuses a copy the
 *  producer overran while it was taken (the caller reads again on its next refresh). */
class LMMS_EXPORT TunerTap
{
public:
	enum class Source : int
	{
		Off = 0,
		Input = 1,
		Master = 2
	};

	//! 8192 frames: 170 ms at 48 kHz, room for two periods of the lowest string's pitch.
	static constexpr std::size_t Capacity = 8192;

	static TunerTap& instance();

	void setSource(Source source) noexcept { m_source.store(static_cast<int>(source), std::memory_order_relaxed); }
	Source source() const noexcept { return static_cast<Source>(m_source.load(std::memory_order_relaxed)); }

	//! AUDIO THREAD: keep @a count frames of @a frames when @a from is the chosen source.
	void push(Source from, const SampleFrame* frames, f_cnt_t count) noexcept;

	//! The newest @a count (<= Capacity) frames, oldest first, into @a out; false when fewer have come in
	//! since the source was chosen, or the copy was overrun.
	bool latest(float* out, std::size_t count) const noexcept;

	//! Forget what came in (choosing a source does this, so a reading never mixes two sources).
	//! A request the audio thread takes at its next push, so a push in flight cannot overwrite it (review
	//! W3-agent-custom #3); until then latest() reports nothing.
	void reset() noexcept { m_resetPending.store(true, std::memory_order_release); }

private:
	std::array<std::atomic<float>, Capacity> m_ring{};
	std::atomic<std::uint64_t> m_head{0};
	std::atomic<bool> m_resetPending{false};
	std::atomic<int> m_source{0};
};

} // namespace lmms

#endif // LMMS_TUNER_TAP_H
