/*
 * MasterLoudnessTap.h - the PASSIVE loudness tap on the master output
 *                       (feature row 24, the live half).
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

#ifndef LMMS_MASTER_LOUDNESS_TAP_H
#define LMMS_MASTER_LOUDNESS_TAP_H

#include <atomic>
#include <cstdint>

#include "LmmsTypes.h"
#include "LufsMeter.h"
#include "SampleFrame.h"
#include "lmms_export.h"

namespace lmms
{

/*! The LIVE loudness readout of the master mix: ITU-R BS.1770-4 / EBU R128
 *  integrated, momentary and short-term loudness plus true peak, measured from
 *  the audio periods the engine is producing right now.
 *
 *  WHAT THIS IS, AND WHAT IT IS NOT. Feature row 24 of
 *  docs/FEATURE-LIST-0.3.0.md boards a loudness METER; the measurement core
 *  (`LufsMeter`) and its offline consumer (`LoudnessReport`, which the render
 *  path feeds) were merged by earlier lanes, and what was missing was any way
 *  to observe the audio the engine is playing - the audit's "in the tree but not
 *  drivable through the socket" list. This class is that observation point and
 *  nothing else: it owns ONE `LufsMeter`, feeds it the master mix of every
 *  period, and publishes the four values as plain lock-free snapshots. It does
 *  not fork the DSP: the measurement is `LufsMeter::processBlock` and every
 *  number here comes out of `LufsMeter`'s own getters.
 *
 *  THE REALTIME CONTRACT (AGENTS.md rule 4). `feed()` is called from the audio
 *  thread, once per rendered period, and it
 *    - allocates nothing (the meter is fixed-size and was constructed once, off
 *      the audio path, at engine construction time),
 *    - locks nothing (every field it touches is a plain member or an atomic),
 *    - grows nothing (no queue, no history, no per-block record),
 *    - and only READS the frames it is given: `feed()` takes `const SampleFrame*`
 *      and hands the same pointer to a `const`-taking measurement, so the bytes
 *      that reach the device (or the render's file) are the bytes that were
 *      measured, unchanged. That is the passivity property the registered proof
 *      asserts (`tests/src/core/MeterTapTest.cpp`: a buffer hashed before and
 *      after being fed through an armed tap is bit-identical).
 *
 *  THE THREADING CONTRACT, stated precisely because a meter is exactly the kind
 *  of component where hand-waving is normal. The audio thread is the ONLY
 *  writer of the meter's state. It touches the meter only between the
 *  `m_inFlight` increment and decrement of one `feed()` call, and only while
 *  `enabled()` is true. The control thread (arm/disarm/enable) never touches the
 *  meter while the tap is enabled: `setEnabled()` transitions through disabled
 *  and waits, bounded, for `m_inFlight` to fall to zero before it resets
 *  anything, so a period currently being measured completes against the meter
 *  object it started with - the object is never freed or replaced, only reset in
 *  place. The residual window is the one every lock-free audio hand-off has: a
 *  block whose `enabled()` read happened before the transition can still be
 *  counted. Its worst case is one period of samples landing in a measurement
 *  that was just reset; it cannot corrupt memory (the meter writes only its own
 *  fixed-size arrays), and it cannot change what is written to a file or a
 *  device (the tap reads).
 *
 *  READERS. `snapshot()` may be called from any thread at any time. Every field
 *  it returns was written by the audio thread with a relaxed store after the
 *  values it summarises; a poll can therefore observe a value from just before
 *  or just after the period it lands in - which is what a meter is - and it can
 *  never observe a torn value (each is one atomic of at most 8 bytes).
 */
class LMMS_EXPORT MasterLoudnessTap
{
public:
	//! Everything the tap publishes, as one consistent-enough snapshot.
	//! The four loudness/peak values carry LufsMeter's sentinel
	//! (`LufsMeter::MinusInfinity`) while they have no value yet; the command
	//! surface renders that as JSON null rather than as a plausible number.
	struct Snapshot
	{
		bool enabled = false;
		sample_rate_t sampleRate = 48000;
		ch_cnt_t channels = DEFAULT_CHANNELS;
		//! Periods fed since the last reset(). Counted by the tap itself, so
		//! "is this tap actually being fed" is answerable from outside.
		std::uint64_t blocksFed = 0;
		std::uint64_t framesFed = 0;
		float integratedLufs = LufsMeter::MinusInfinity;
		float momentaryLufs = LufsMeter::MinusInfinity;
		float shortTermLufs = LufsMeter::MinusInfinity;
		//! Loudest short-term window since the last reset() (the meter itself
		//! only holds the current 3 s window; a report wants the worst case).
		float shortTermMaxLufs = LufsMeter::MinusInfinity;
		float truePeakDbtp = LufsMeter::MinusInfinity;
	};

	/*! @param sampleRate   The engine's PROCESSING rate, which is the rate of
	 *                      every period this tap is fed: the audio device
	 *                      resamples to its own rate on the way out, so the
	 *                      master mix is always at the engine's rate.
	 * @param channelCount  Channels per frame of the master mix (stereo in
	 *                      every configuration this build produces). */
	explicit MasterLoudnessTap(sample_rate_t sampleRate = 48000,
		ch_cnt_t channelCount = DEFAULT_CHANNELS);

	MasterLoudnessTap(const MasterLoudnessTap&) = delete;
	MasterLoudnessTap& operator=(const MasterLoudnessTap&) = delete;

	//! Whether the tap is currently being fed. Cheap; any thread.
	bool enabled() const noexcept { return m_enabled.load(std::memory_order_acquire); }
	sample_rate_t sampleRate() const noexcept { return m_meter.sampleRate(); }
	ch_cnt_t channelCount() const noexcept { return m_meter.channelCount(); }

	/*! Enables or disables the tap. CONTROL THREAD.
	 *
	 *  Enabling a DISABLED tap starts a FRESH measurement: the accumulated
	 *  integrated loudness and true peak of whatever was measured before are
	 *  dropped, so "arm, play the section, read" reports that section. Enabling
	 *  an already-enabled tap changes nothing (the measurement continues).
	 *
	 *  Disabling keeps the last reading readable - `snapshot()` still answers
	 *  with what was measured - so a caller can disarm and then read the result.
	 *
	 *  Allocates nothing, and never replaces the meter object: the algorithm is
	 *  designed so the tap can be armed and disarmed while the transport is
	 *  running.
	 */
	void setEnabled(bool enabled) noexcept;

	/*! Drops the accumulated measurement and starts a new one, keeping the
	 *  current enabled state. CONTROL THREAD. Returns false when the tap did not
	 *  go quiet within the bounded wait (in which case NOTHING was reset and the
	 *  caller can say so instead of reporting a fresh measurement), true
	 *  otherwise.
	 */
	bool reset() noexcept;

	/*! Feeds one period of the master mix. AUDIO THREAD. Reads \p frames only;
	 *  allocates nothing, locks nothing. A disabled tap does nothing at all.
	 */
	void feed(const SampleFrame* frames, f_cnt_t frameCount) noexcept;

	//! Snapshot of everything the tap knows. Any thread, any time.
	Snapshot snapshot() const noexcept;

	//! The bound on how long reset()/setEnabled() wait for the audio thread to
	//! leave the tap before touching the meter, in milliseconds. Generous: one
	//! audio period is a few milliseconds at every supported buffer size.
	static constexpr int QuietWaitMs = 250;

private:
	//! Publishes the meter's four values (read on the audio thread) into the
	//! atomics a reader sees. AUDIO THREAD.
	void publish() noexcept;
	//! Waits, bounded by QuietWaitMs, for the audio thread to leave the tap.
	//! CONTROL THREAD (never the audio thread). Returns false on timeout.
	bool waitForQuiet() noexcept;

	//! The one measurement core (constructed with the engine's rate and channel
	//! count; never replaced, so a feed in flight always completes against a
	//! live object).
	LufsMeter m_meter;

	std::atomic<bool> m_enabled{false};
	//! Number of feed() calls inside the meter right now. The control thread's
	//! quiesce condition; incremented and decremented by the audio thread only.
	std::atomic<int> m_inFlight{0};

	std::atomic<std::uint64_t> m_blocksFed{0};
	std::atomic<std::uint64_t> m_framesFed{0};
	std::atomic<float> m_integratedLufs{LufsMeter::MinusInfinity};
	std::atomic<float> m_momentaryLufs{LufsMeter::MinusInfinity};
	std::atomic<float> m_shortTermLufs{LufsMeter::MinusInfinity};
	std::atomic<float> m_shortTermMaxLufs{LufsMeter::MinusInfinity};
	std::atomic<float> m_truePeakDbtp{LufsMeter::MinusInfinity};

	//! The loudest short-term window so far. Written by the audio thread only
	//! (the same rule LoudnessReport::m_shortTermMax follows on the render
	//! path), published through m_shortTermMaxLufs.
	float m_shortTermMax = LufsMeter::MinusInfinity;
};

} // namespace lmms

#endif // LMMS_MASTER_LOUDNESS_TAP_H
