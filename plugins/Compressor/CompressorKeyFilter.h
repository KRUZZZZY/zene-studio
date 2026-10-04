/*
 * CompressorKeyFilter.h - the compressor's key filter, designed where a knob moves and run on the audio thread
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

#ifndef LMMS_COMPRESSOR_KEY_FILTER_H
#define LMMS_COMPRESSOR_KEY_FILTER_H

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <numbers>

#include "BiquadDesign.h"

namespace lmms
{

/*! catch-up W3: the compressor's key filter (0.5.0 L09, MX-057) - a high-pass and a low-pass on the
 *  detector's input - split so that no thread rewrites what the audio thread is running. Before, the
 *  knobs' direct-connected slot rewrote the coefficients and zeroed the filter state from whichever
 *  thread moved the knob, while the audio thread ran the filter.
 *  - designKeyFilter(): the coefficients for two cutoffs, a pure function, computed by that slot.
 *  - KeyFilterMailbox: hands the design to the audio thread without a lock.
 *  - KeyFilter: the audio thread's own filter, which adopts a new design between blocks and alone
 *    owns its state. */
struct KeyFilterDesign
{
	BiquadCoefficients hp;
	BiquadCoefficients lp;
	bool hpOn = false;
	bool lpOn = false;
};

//! RBJ high-pass and low-pass at Q = 1/sqrt(2); a cutoff at the end of its range switches that half
//! off (a high-pass at 20.5 Hz or below; a low-pass at 19999 Hz or above, or above 0.45 of the rate).
//! The arithmetic is the 0.5.0 L09 slot's, in float, so a saved project detects exactly as before.
inline KeyFilterDesign designKeyFilter(float hpHz, float lpHz, float sampleRate)
{
	using namespace std::numbers;
	const auto design = [sampleRate](float hz, bool highPass) {
		const float w = 2.f * pi_v<float> * hz / sampleRate;
		const float alpha = std::sin(w) / sqrt2_v<float>;  // sin(w) / (2Q), Q = 1/sqrt(2)
		const float cosw = std::cos(w);
		const float a0 = 1.f + alpha;
		const float k = highPass ? (1.f + cosw) / 2.f : (1.f - cosw) / 2.f;
		return BiquadCoefficients{k / a0, (highPass ? -2.f * k : 2.f * k) / a0, k / a0, -2.f * cosw / a0, (1.f - alpha) / a0};
	};
	KeyFilterDesign out;
	out.hpOn = hpHz > 20.5f;
	out.lpOn = lpHz < 19999.f && lpHz < sampleRate * 0.45f;
	if (out.hpOn) { out.hp = design(hpHz, true); }
	if (out.lpOn) { out.lp = design(lpHz, false); }
	return out;
}

/*! The latest KeyFilterDesign, from any thread that moves a knob (the control thread, or the audio
 *  thread itself when automation moves it) to the audio thread: a sequence lock over atomics, so
 *  there is no data race and a torn design is never adopted.
 *  The reader never waits: while a design is being written it keeps the one it has and picks the new
 *  one up next block. Writers never wait either: one that finds another mid-write marks the design
 *  dirty and leaves; the writer holding the lock then designs again from the current knobs, so the
 *  last design published is always the one the knobs ask for. */
class KeyFilterMailbox
{
public:
	//! Any thread: publishes @a make(), which designs from the current knob values.
	template<typename Make>
	void publish(const Make& make)
	{
		m_dirty.store(true);
		while (m_dirty.load())
		{
			unsigned version = m_version.load();
			if ((version & 1u) != 0u || !m_version.compare_exchange_strong(version, version + 1)) { return; }
			std::atomic_thread_fence(std::memory_order_release);
			m_dirty.exchange(false);  // a read: it sees the knob a writer that left early moved
			store(make());
			m_version.store(version + 2);
		}
	}

	//! Audio thread: a newer complete design into @a out, or false (nothing new, or one mid-write).
	bool take(KeyFilterDesign& out) noexcept
	{
		const unsigned version = m_version.load(std::memory_order_acquire);
		if ((version & 1u) != 0u || version == m_taken) { return false; }
		std::array<float, Fields> values;
		for (std::size_t i = 0; i < Fields; ++i) { values[i] = m_fields[i].load(std::memory_order_relaxed); }
		std::atomic_thread_fence(std::memory_order_acquire);
		if (m_version.load(std::memory_order_relaxed) != version) { return false; }
		m_taken = version;
		out = unpack(values);
		return true;
	}

private:
	static constexpr std::size_t Fields = 12;

	void store(const KeyFilterDesign& design) noexcept
	{
		const std::array<float, Fields> values{design.hp.b0, design.hp.b1, design.hp.b2, design.hp.a1, design.hp.a2,
			design.hpOn ? 1.f : 0.f, design.lp.b0, design.lp.b1, design.lp.b2, design.lp.a1, design.lp.a2, design.lpOn ? 1.f : 0.f};
		for (std::size_t i = 0; i < Fields; ++i) { m_fields[i].store(values[i], std::memory_order_relaxed); }
	}

	static KeyFilterDesign unpack(const std::array<float, Fields>& v) noexcept
	{
		return {{v[0], v[1], v[2], v[3], v[4]}, {v[6], v[7], v[8], v[9], v[10]}, v[5] != 0.f, v[11] != 0.f};
	}

	std::array<std::atomic<float>, Fields> m_fields{};
	std::atomic<unsigned> m_version{0};  //!< odd while a design is being written
	std::atomic<bool> m_dirty{false};    //!< a writer found the lock taken: design again
	unsigned m_taken = 0;                //!< the version the audio thread last adopted (audio thread only)
};

//! The audio thread's key filter: the design it adopted and the state only it touches.
class KeyFilter
{
public:
	//! Audio thread, before a block: adopts a newer design from @a mailbox. A half switched on starts
	//! from rest (its state froze while it was off); a half that stays on keeps its state.
	void follow(KeyFilterMailbox& mailbox) noexcept
	{
		KeyFilterDesign next;
		if (!mailbox.take(next)) { return; }
		if (next.hpOn && !m_hpOn) { m_hp.reset(); }
		if (next.lpOn && !m_lpOn) { m_lp.reset(); }
		if (next.hpOn) { m_hp.set(next.hp); }
		if (next.lpOn) { m_lp.set(next.lp); }
		m_hpOn = next.hpOn;
		m_lpOn = next.lpOn;
	}

	//! The detector's input @a x of channel @a ch through the filter (a no-op while both halves are off).
	float process(float x, int ch) noexcept
	{
		if (m_hpOn) { x = m_hp.run(x, ch); }
		return m_lpOn ? m_lp.run(x, ch) : x;
	}

private:
	BiquadSection<2> m_hp;
	BiquadSection<2> m_lp;
	bool m_hpOn = false;
	bool m_lpOn = false;
};

} // namespace lmms

#endif // LMMS_COMPRESSOR_KEY_FILTER_H
