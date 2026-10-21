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
 *  chosen one (the default: none) it is one atomic load. The ring is a fixed array of atomics in
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

	//! Choosing a source and forgetting the old frames are published in one generation.
	void setSource(Source source) noexcept
	{
		publish(source);
#ifdef LMMS_TESTING
		// The seam marks the instant the new source and its reset are published, before setSource() returns.
		if (m_sourcePublishedHook != nullptr) { m_sourcePublishedHook(m_sourcePublishedUser); }
#endif
	}
	Source source() const noexcept
	{
		return static_cast<Source>(m_state.load(std::memory_order_relaxed) & SourceMask);
	}

#ifdef LMMS_TESTING
	//! Test seam: setSource() with @a hook run once the new source is published, before it returns.
	void setSourceWithTestHook(Source source, void (*hook)(void*), void* user = nullptr) noexcept
	{
		m_sourcePublishedHook = hook;
		m_sourcePublishedUser = user;
		setSource(source);
		m_sourcePublishedHook = nullptr;
		m_sourcePublishedUser = nullptr;
	}
#endif

	//! AUDIO THREAD: keep @a count frames of @a frames when @a from is the chosen source.
	void push(Source from, const SampleFrame* frames, f_cnt_t count) noexcept;

	//! The newest @a count (<= Capacity) frames, oldest first, into @a out; false when fewer have come in
	//! since the source was chosen, or the copy was overrun.
	bool latest(float* out, std::size_t count) const noexcept;

	//! Forget what came in. latest() reports nothing until the audio thread takes this generation at its next push.
	void reset() noexcept { m_state.fetch_add(GenerationStep, std::memory_order_release); }

private:
	//! The chosen source and generation of its last reset share one word, so push() always observes both together.
	static constexpr std::uint64_t SourceMask = 0x3;
	static constexpr std::uint64_t GenerationStep = 0x4;

	//! Publish @a source and the ring reset as one generation change (the control thread's only writer).
	void publish(Source source) noexcept
	{
		std::uint64_t state = m_state.load(std::memory_order_relaxed);
		std::uint64_t next;
		do
		{
			next = ((state + GenerationStep) & ~SourceMask)
				| (static_cast<std::uint64_t>(source) & SourceMask);
		} while (!m_state.compare_exchange_weak(state, next, std::memory_order_release, std::memory_order_relaxed));
	}

	std::array<std::atomic<float>, Capacity> m_ring{};
	std::atomic<std::uint64_t> m_head{0};
	//! Where the push in progress ends: stored before its slots are written (m_head after), so
	//! latest() can see a write that has not been published yet overlapping its window.
	std::atomic<std::uint64_t> m_reserved{0};
	//! Source in the low bits, reset generation in the high bits.
	std::atomic<std::uint64_t> m_state{0};
	//! The generation whose reset and first write the producer has completed.
	std::atomic<std::uint64_t> m_applied{0};
#ifdef LMMS_TESTING
	void (*m_sourcePublishedHook)(void*) = nullptr;
	void* m_sourcePublishedUser = nullptr;
#endif
};

} // namespace lmms

#endif // LMMS_TUNER_TAP_H
