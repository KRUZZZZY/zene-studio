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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace lmms
{

namespace
{

//! How long the control thread sleeps between two looks at the in-flight
//! counter. Short enough that disarming feels immediate, long enough not to
//! spin a core while an audio period finishes.
constexpr int QuietPollMicroseconds = 200;

#ifdef LMMS_TESTING
std::atomic<int> nextQuietWaitResultForTesting{-1};
#endif

} // namespace

#ifdef LMMS_TESTING
void setMasterLoudnessTapNextQuietWaitResultForTesting(bool quiet) noexcept
{
	nextQuietWaitResultForTesting.store(quiet ? 1 : 0, std::memory_order_relaxed);
}
#endif

MasterLoudnessTap::MasterLoudnessTap(sample_rate_t sampleRate, ch_cnt_t channelCount) :
	m_meter(sampleRate, channelCount)
{
	// Everything the tap owns is fixed-size and was just constructed: no
	// allocation happens after this constructor, which is what lets feed() run
	// on the audio thread (AGENTS.md rule 4).
}

bool MasterLoudnessTap::setEnabled(bool enabled) noexcept
{
	const bool wasEnabled = m_enabled.load(std::memory_order_seq_cst);
	m_enabled.store(false, std::memory_order_seq_cst);

	if (!waitForQuiet())
	{
		// Keep failed state changes atomic from a caller's point of view. The
		// false return warns that an in-flight feed may still publish after this
		// call, so the caller must not treat its snapshot as final.
		m_enabled.store(wasEnabled, std::memory_order_release);
		return false;
	}

	// ARMING (or RE-ARMING) STARTS A MEASUREMENT, every time. Clearing is safe
	// only after the audio thread has left the tap.
	if (enabled) { clearMeasurement(); }
	m_enabled.store(enabled, std::memory_order_release);
	return true;
}

bool MasterLoudnessTap::reset() noexcept
{
	const bool wasEnabled = enabled();
	if (wasEnabled) { m_enabled.store(false, std::memory_order_seq_cst); }
	if (!waitForQuiet())
	{
		// Nothing was reset: saying so is the point. A caller that is told false
		// knows the reading it is about to see is the OLD measurement, not a
		// fresh one.
		if (wasEnabled) { m_enabled.store(true, std::memory_order_release); }
		return false;
	}

	clearMeasurement();
	if (wasEnabled) { m_enabled.store(true, std::memory_order_release); }
	return true;
}

//! Drops everything measured: the meter's own state and every published value.
//! CONTROL THREAD, and only ever with the tap quiet (the callers above are the
//! only two, and both wait first).
void MasterLoudnessTap::clearMeasurement() noexcept
{
	m_meter.reset();
	m_shortTermMax = LufsMeter::MinusInfinity;
	m_blocksFed.store(0, std::memory_order_relaxed);
	m_framesFed.store(0, std::memory_order_relaxed);
	m_integratedLufs.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);
	m_momentaryLufs.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);
	m_shortTermLufs.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);
	m_shortTermMaxLufs.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);
	m_truePeakDbtp.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);
	m_loudnessRangeLu.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);
	m_peakToLoudnessRatioDb.store(LufsMeter::MinusInfinity, std::memory_order_relaxed);
	m_samplePeak.store(0.f, std::memory_order_relaxed);
}

void MasterLoudnessTap::feed(const SampleFrame* frames, f_cnt_t frameCount) noexcept
{
	if (frames == nullptr || frameCount == 0) { return; }

	// Counted BEFORE the enabled check: a feed that read "enabled" and was then
	// preempted would otherwise enter the meter after a disarm had already seen
	// zero in flight and started clearing it. Both sides are seq_cst (the store
	// of false and waitForQuiet's load too): store-then-load on each side.
	m_inFlight.fetch_add(1, std::memory_order_seq_cst);
	// Q050-100: the sample peak is measured even while the loudness meter is disarmed.
	// A peak watch must see the master before anyone arms the LUFS tap, and this loop
	// is the same fixed work the loudness meter's own peak scan does: no allocation,
	// no lock and no write to the samples.
	float peak = 0.f;
	for (f_cnt_t frame = 0; frame < frameCount; ++frame)
	{
		peak = std::max(peak, std::max(std::abs(frames[frame][0]), std::abs(frames[frame][1])));
	}
	const float previousPeak = m_samplePeak.load(std::memory_order_relaxed);
	if (peak > previousPeak) { m_samplePeak.store(peak, std::memory_order_relaxed); }
	if (!m_enabled.load(std::memory_order_seq_cst))
	{
		m_inFlight.fetch_sub(1, std::memory_order_release);
		return;
	}

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
	// The two distribution measures are derived, not incremental: they are read
	// off the meter's histograms, so they cost nothing to publish here.
	m_loudnessRangeLu.store(m_meter.loudnessRangeLu(), std::memory_order_relaxed);
	m_peakToLoudnessRatioDb.store(m_meter.peakToLoudnessRatioDb(), std::memory_order_relaxed);

	if (reading.shortTermLufs > m_shortTermMax) { m_shortTermMax = reading.shortTermLufs; }
	m_shortTermMaxLufs.store(m_shortTermMax, std::memory_order_relaxed);
}

bool MasterLoudnessTap::waitForQuiet() noexcept
{
#ifdef LMMS_TESTING
	const int forcedResult = nextQuietWaitResultForTesting.exchange(-1,
		std::memory_order_relaxed);
	if (forcedResult >= 0) { return forcedResult != 0; }
#endif

	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(QuietWaitMs);
	while (m_inFlight.load(std::memory_order_seq_cst) != 0)
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
	out.loudnessRangeLu = m_loudnessRangeLu.load(std::memory_order_relaxed);
	out.peakToLoudnessRatioDb = m_peakToLoudnessRatioDb.load(std::memory_order_relaxed);
	out.samplePeak = m_samplePeak.load(std::memory_order_relaxed);
	return out;
}

} // namespace lmms
