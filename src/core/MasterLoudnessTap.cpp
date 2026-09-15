/*
 * MasterLoudnessTap.cpp - the PASSIVE loudness tap on the master output
 *                         (feature row 24, the live half).
 *
 * The class's own header (include/MasterLoudnessTap.h) carries the contracts:
 * what it measures, the realtime rule, and the exact threading story. This file
 * is the implementation and repeats only what a reader of the code needs.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "MasterLoudnessTap.h"

#include <chrono>
#include <thread>

namespace lmms
{

namespace
{

//! How long the control thread sleeps between two looks at the in-flight
//! counter. Short enough that disarming feels immediate, long enough not to
//! spin a core while an audio period finishes.
constexpr int QuietPollMicroseconds = 200;

} // namespace

MasterLoudnessTap::MasterLoudnessTap(sample_rate_t sampleRate, ch_cnt_t channelCount) :
	m_meter(sampleRate, channelCount)
{
	// Everything the tap owns is fixed-size and was just constructed: no
	// allocation happens after this constructor, which is what lets feed() run
	// on the audio thread (AGENTS.md rule 4).
}

void MasterLoudnessTap::setEnabled(bool enabled) noexcept
{
	if (enabled == this->enabled())
	{
		// Enabling an armed tap continues the measurement it is running;
		// disabling a disarmed tap has nothing to do. Idempotent on purpose:
		// "arm" is a state, not an event, and a caller that re-arms by mistake
		// must not silently discard a measurement.
		return;
	}

	if (!enabled)
	{
		m_enabled.store(false, std::memory_order_release);
		// Not required for correctness (nothing touches the meter below), but
		// it makes the contract simple to state: once setEnabled(false)
		// returns, the last feed() that could have been running has finished,
		// so the reading a caller sees next is final.
		(void) waitForQuiet();
		return;
	}

	// Arming STARTS A MEASUREMENT. The tap is disarmed, so the audio thread is
	// not inside the meter; the quiesce wait is the belt to that brace (a block
	// whose enabled() read predates the disarm can still be finishing).
	if (!waitForQuiet()) { return; }
	m_meter.reset();
	m_shortTermMax = LufsMeter::MinusInfinity;
	m_blocksFed.store(0, std::memory_order_relaxed);
	m_framesFed.store(0, std::memory_order_relaxed);
	m_integratedLufs.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);
	m_momentaryLufs.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);
	m_shortTermLufs.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);
	m_shortTermMaxLufs.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);
	m_truePeakDbtp.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);
	m_enabled.store(true, std::memory_order_release);
}

bool MasterLoudnessTap::reset() noexcept
{
	const bool wasEnabled = enabled();
	if (wasEnabled) { m_enabled.store(false, std::memory_order_release); }
	if (!waitForQuiet())
	{
		// Nothing was reset: saying so is the point. A caller that is told
		// false knows the reading it is about to see is the OLD measurement,
		// not a fresh one.
		if (wasEnabled) { m_enabled.store(true, std::memory_order_release); }
		return false;
	}

	m_meter.reset();
	m_shortTermMax = LufsMeter::MinusInfinity;
	m_blocksFed.store(0, std::memory_order_relaxed);
	m_framesFed.store(0, std::memory_order_relaxed);
	m_integratedLufs.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);
	m_momentaryLufs.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);
	m_shortTermLufs.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);
	m_shortTermMaxLufs.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);
	m_truePeakDbtp.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);

	if (wasEnabled) { m_enabled.store(true, std::memory_order_release); }
	return true;
}

void MasterLoudnessTap::feed(const SampleFrame* frames, f_cnt_t frameCount) noexcept
{
	if (frames == nullptr || frameCount == 0) { return; }
	if (!m_enabled.load(std::memory_order_acquire)) { return; }

	m_inFlight.fetch_add(1, std::memory_order_acq_rel);

	// The measurement itself: LufsMeter::processBlock reads the frames and
	// nothing else (it takes them `const`), allocates nothing and locks
	// nothing. There is no copy of the audio anywhere in this path.
	m_meter.processBlock(frames, frameCount);

	m_blocksFed.fetch_add(1, std::memory_order_relaxed);
	m_framesFed.fetch_add(frameCount, std::memory_order_relaxed);
	publish();

	m_inFlight.fetch_sub(1, std::memory_order_release);
}

void MasterLoudnessTap::publish() noexcept
{
	// One read per period. `read()` computes the gated integrated value from
	// the meter's 1501-bin histogram and the two window sums; at the smallest
	// supported period (DEFAULT_BUFFER_SIZE frames) that is a few hundred
	// thousand bin looks per second - the same work the render path already
	// does per block, and no allocation either way.
	const LufsMeter::Reading reading = m_meter.read();
	m_integratedLufs.store(reading.integratedLufs, std::memory_order_relaxed);
	m_momentaryLufs.store(reading.momentaryLufs, std::memory_order_relaxed);
	m_shortTermLufs.store(reading.shortTermLufs, std::memory_order_relaxed);
	m_truePeakDbtp.store(reading.truePeakDbtp, std::memory_order_relaxed);

	if (reading.shortTermLufs > m_shortTermMax) { m_shortTermMax = reading.shortTermLufs; }
	m_shortTermMaxLufs.store(m_shortTermMax, std::memory_order_relaxed);
}

bool MasterLoudnessTap::waitForQuiet() noexcept
{
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(QuietWaitMs);
	while (m_inFlight.load(std::memory_order_acquire) != 0)
	{
		if (std::chrono::steady_clock::now() >= deadline) { return false; }
		std::this_thread::sleep_for(std::chrono::microseconds(QuietPollMicroseconds));
	}
	return true;
}

MasterLoudnessTap::Snapshot MasterLoudnessTap::snapshot() const noexcept
{
	Snapshot out;
	out.enabled = m_enabled.load(std::memory_order_acquire);
	out.sampleRate = m_meter.sampleRate();
	out.channels = m_meter.channelCount();
	out.blocksFed = m_blocksFed.load(std::memory_order_relaxed);
	out.framesFed = m_framesFed.load(std::memory_order_relaxed);
	out.integratedLufs = m_integratedLufs.load(std::memory_order_relaxed);
	out.momentaryLufs = m_momentaryLufs.load(std::memory_order_relaxed);
	out.shortTermLufs = m_shortTermLufs.load(std::memory_order_relaxed);
	out.shortTermMaxLufs = m_shortTermMaxLufs.load(std::memory_order_relaxed);
	out.truePeakDbtp = m_truePeakDbtp.load(std::memory_order_relaxed);
	return out;
}

} // namespace lmms
