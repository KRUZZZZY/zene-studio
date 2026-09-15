/*
 * MeterTapTest.cpp - the LIVE loudness tap's contract: what an armed tap reads,
 *                    what a disarmed one reads, that a loud signal reads
 *                    proportionally higher, that silence reads the sentinel, and
 *                    that feeding a buffer through the tap does not change one
 *                    byte of it.
 *
 * Feature row 24 of docs/FEATURE-LIST-0.3.0.md ("LUFS / loudness metering"):
 * this is the registered proof of the LIVE half of the `meter.*` group
 * (include/MasterLoudnessTap.h, src/core/MasterLoudnessTap.cpp, which
 * AudioEngine feeds the master mix to). The offline half's proof is the
 * registered ctest ControlMeterCommands (tests/control-meter-commands.py),
 * which drives meter.get_state / meter.arm / meter.measure_file over
 * --control-socket; the byte-identity of a RENDER with the tap armed is also
 * asserted there, at the file level.
 *
 * THE THREE NEGATIVE CONTROLS this file carries, because a meter that is fed
 * nothing and a meter that is fed silence are indistinguishable from the
 * outside and both read plausibly if the code is wrong:
 *
 *   1. SILENCE READS THE SENTINEL. A tap fed digital silence reports
 *      -infinity (LufsMeter::MinusInfinity) for every value, never a number -
 *      and a DISARMED tap does too, because it is fed nothing at all. The
 *      command surface renders that as JSON null.
 *   2. A LOUDER SIGNAL READS PROPORTIONALLY HIGHER. 10 dB more input is
 *      10 LU more output, inside the ±0.1 LU the EBU compliance suite uses; a
 *      component that reports a constant, or one that is fed the wrong signal,
 *      cannot pass this.
 *   3. THE TAP IS PASSIVE. A buffer hashed BEFORE it is fed through an armed
 *      tap and hashed AFTER is bit-identical, so the samples that reach the
 *      device are the samples that were measured. This is the same claim the
 *      render path's tap makes (docs/LUFS-WIRING.md section 4.3) one level
 *      down, where the taps live.
 *
 * Plus the two realtime-rule assertions the tree's gold standard uses: feeding
 * 64 blocks through an armed tap allocates NOTHING (AllocationProbe), and a
 * disarmed tap measurers nothing at all (its counters do not move), which is
 * what makes "an unarmed engine renders exactly as before" true at this seam.
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

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "AllocationProbe.h"

using lmms::LufsMeter;
using lmms::MasterLoudnessTap;
using lmms::SampleFrame;
using lmms::sample_t;

namespace
{

constexpr int SampleRate = 48000;
constexpr double Pi = 3.14159265358979323846;
//! EBU Tech 3341 case 1 and case 2 levels, in dB below full scale.
constexpr double Case1Dbfs = -23.0;
constexpr double Case2Dbfs = -33.0;
//! The tolerance the EBU compliance suite uses, in LU.
constexpr double ToleranceLu = 0.1;

//! A 1 kHz sine in both channels, each channel's peak \a peakDbfs below full
//! scale: the EBU Tech 3341 test signal the meter's own compliance vectors use,
//! so a reading here is comparable with LufsMeterTest's.
std::vector<SampleFrame> makeSine(double peakDbfs, double seconds)
{
	const double amplitude = std::pow(10.0, peakDbfs / 20.0);
	const auto frames = static_cast<std::size_t>(seconds * SampleRate);
	std::vector<SampleFrame> buffer(frames);
	for (std::size_t i = 0; i < frames; ++i)
	{
		const auto value = static_cast<sample_t>(
			amplitude * std::sin(2.0 * Pi * 1000.0 * static_cast<double>(i) / SampleRate));
		buffer[i] = SampleFrame(value, value);
	}
	return buffer;
}

std::vector<SampleFrame> makeSilence(double seconds)
{
	return std::vector<SampleFrame>(static_cast<std::size_t>(seconds * SampleRate));
}

//! A FNV-1a hash of the buffer's bytes. What matters is that it is computed from
//! the memory itself: two hashes that agree mean not one sample moved. `feed()`
//! takes `const SampleFrame*`, so this is the assertion that the const is real.
std::uint64_t hashFrames(const std::vector<SampleFrame>& frames)
{
	std::uint64_t hash = 1469598103934665603ull;
	const auto* bytes = reinterpret_cast<const unsigned char*>(frames.data());
	const std::size_t size = frames.size() * sizeof(SampleFrame);
	for (std::size_t i = 0; i < size; ++i)
	{
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

//! Feeds \a buffer in one-block-per-call slices of \a blockFrames, the shape the
//! engine's renderStageMix() calls in.
void feedInBlocks(MasterLoudnessTap& tap, const std::vector<SampleFrame>& buffer, int blockFrames)
{
	for (std::size_t i = 0; i < buffer.size(); i += static_cast<std::size_t>(blockFrames))
	{
		const auto count = static_cast<lmms::f_cnt_t>(
			std::min<std::size_t>(blockFrames, buffer.size() - i));
		tap.feed(buffer.data() + i, count);
	}
}

} // namespace

class MeterTapTest : public QObject
{
	Q_OBJECT

private slots:
	//! A tap that has never been armed measures nothing, and says so with the
	//! sentinel rather than with a number: this is the "off by default" half of
	//! the contract, and it is what makes an unarmed engine's audio path a
	//! single relaxed atomic load.
	void aDisarmedTapMeasuresNothing()
	{
		MasterLoudnessTap tap(SampleRate, DEFAULT_CHANNELS);
		QVERIFY(!tap.enabled());

		const std::vector<SampleFrame> signal = makeSine(Case1Dbfs, 1.0);
		feedInBlocks(tap, signal, 256);

		const MasterLoudnessTap::Snapshot after = tap.snapshot();
		QCOMPARE(after.blocksFed, 0ull);
		QCOMPARE(after.framesFed, 0ull);
		QCOMPARE(after.integratedLufs, LufsMeter::MinusInfinity);
		QCOMPARE(after.truePeakDbtp, LufsMeter::MinusInfinity);
	}

	//! NEGATIVE CONTROL 1: digital silence is not a plausible number. The meter
	//! gates everything below -70 LUFS, so nothing survives to be averaged and
	//! every value stays at the sentinel - including the true peak, which is
	//! measured from the samples themselves.
	void silenceReadsTheSentinel()
	{
		MasterLoudnessTap tap(SampleRate, DEFAULT_CHANNELS);
		tap.setEnabled(true);
		QVERIFY(tap.enabled());

		const std::vector<SampleFrame> silence = makeSilence(5.0);
		feedInBlocks(tap, silence, 512);

		const MasterLoudnessTap::Snapshot reading = tap.snapshot();
		QVERIFY(reading.blocksFed > 0);
		QCOMPARE(reading.integratedLufs, LufsMeter::MinusInfinity);
		QCOMPARE(reading.momentaryLufs, LufsMeter::MinusInfinity);
		QCOMPARE(reading.shortTermLufs, LufsMeter::MinusInfinity);
		QCOMPARE(reading.shortTermMaxLufs, LufsMeter::MinusInfinity);
		QCOMPARE(reading.truePeakDbtp, LufsMeter::MinusInfinity);
	}

	//! The armed tap reads the level it was fed, and fills its windows in the
	//! documented order (short-term needs 3 s, and its worst case - the value a
	//! delivery spec asks for - is what shortTermMaxLufs reports).
	void anArmedTapReadsTheLevelItWasFed()
	{
		MasterLoudnessTap tap(SampleRate, DEFAULT_CHANNELS);
		tap.setEnabled(true);

		const std::vector<SampleFrame> signal = makeSine(Case1Dbfs, 5.0);
		feedInBlocks(tap, signal, 512);

		const MasterLoudnessTap::Snapshot reading = tap.snapshot();
		QVERIFY(std::fabs(reading.integratedLufs - Case1Dbfs) <= ToleranceLu);
		QVERIFY(std::fabs(reading.shortTermLufs - Case1Dbfs) <= ToleranceLu);
		QVERIFY(std::fabs(reading.shortTermMaxLufs - Case1Dbfs) <= ToleranceLu);
		QVERIFY(reading.shortTermMaxLufs >= reading.shortTermLufs - 0.001f);
		// True peak of a sine whose samples sit at -23 dBFS: the 4x
		// interpolator may find up to ~0.08 dB more between the samples, never
		// less than the sample peak.
		QVERIFY(reading.truePeakDbtp >= Case1Dbfs - 0.01f);
		QVERIFY(reading.truePeakDbtp <= Case1Dbfs + 0.2f);
	}

	//! NEGATIVE CONTROL 2: 10 dB more input is 10 LU more output. Run as ONE
	//! test that measures both levels through the same object, so a tap that
	//! reported a constant, or that was never fed, fails on the difference - and
	//! re-arming between the two measurements also proves that arming DISCARDS
	//! the previous measurement (the documented "arming starts a measurement").
	void aLouderSignalReadsProportionallyHigher()
	{
		MasterLoudnessTap tap(SampleRate, DEFAULT_CHANNELS);
		tap.setEnabled(true);
		const std::vector<SampleFrame> loud = makeSine(Case1Dbfs, 5.0);
		feedInBlocks(tap, loud, 512);
		const float loudReading = tap.snapshot().integratedLufs;

		// Disarm, then arm again: the fresh measurement must start from the
		// sentinel, not from the loud reading.
		tap.setEnabled(false);
		tap.setEnabled(true);
		const MasterLoudnessTap::Snapshot fresh = tap.snapshot();
		QCOMPARE(fresh.integratedLufs, LufsMeter::MinusInfinity);
		QCOMPARE(fresh.truePeakDbtp, LufsMeter::MinusInfinity);

		const std::vector<SampleFrame> quiet = makeSine(Case2Dbfs, 5.0);
		feedInBlocks(tap, quiet, 512);
		const float quietReading = tap.snapshot().integratedLufs;

		QVERIFY(std::fabs(quietReading - Case2Dbfs) <= ToleranceLu);
		// The two signals are 10 dB apart by construction (EBU cases 1 and 2),
		// and loudness follows level one for one.
		QVERIFY(std::fabs((loudReading - quietReading) - 10.0f) <= ToleranceLu);
	}

	//! NEGATIVE CONTROL 3: the tap is PASSIVE. The buffer's bytes are hashed,
	//! then fed through an armed tap (true peak and all), then hashed again: not
	//! one sample may differ. A tap that wrote to its input - a filter that
	//! processed in place, a normalisation, a metering that stored back - fails
	//! here, and that failure would be an audible change to every render.
	void feedingAnArmedTapDoesNotChangeOneByte()
	{
		MasterLoudnessTap tap(SampleRate, DEFAULT_CHANNELS);
		tap.setEnabled(true);

		const std::vector<SampleFrame> signal = makeSine(Case1Dbfs, 2.0);
		const std::uint64_t before = hashFrames(signal);
		feedInBlocks(tap, signal, 512);
		const std::uint64_t after = hashFrames(signal);

		QCOMPARE(after, before);
		// And the reading is live, so the passivity is not the passivity of a
		// tap that did nothing.
		QVERIFY(tap.snapshot().blocksFed > 0);
		QVERIFY(std::fabs(tap.snapshot().integratedLufs - Case1Dbfs) <= ToleranceLu);
	}

	//! The realtime rule (AGENTS.md rule 4): feeding an armed tap allocates
	//! nothing. The meter is constructed once with the tap, so there is no
	//! per-block allocation to hide behind a cache.
	void feedingAnArmedTapAllocatesNothing()
	{
		MasterLoudnessTap tap(SampleRate, DEFAULT_CHANNELS);
		const std::vector<SampleFrame> signal = makeSine(Case1Dbfs, 1.0);

		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		tap.setEnabled(true);
		for (int block = 0; block < 64; ++block)
		{
			tap.feed(signal.data(), static_cast<lmms::f_cnt_t>(signal.size()));
			const MasterLoudnessTap::Snapshot reading = tap.snapshot();
			Q_UNUSED(reading);
		}
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;

		QCOMPARE(allocations, 0ull);
	}

	//! Disarming stops the measurement without losing the reading, and the
	//! counters freeze where they were: a caller that disarms to stop measuring
	//! and then reads still sees the section it measured.
	void disarmingKeepsTheLastReadingAndStopsCounting()
	{
		MasterLoudnessTap tap(SampleRate, DEFAULT_CHANNELS);
		tap.setEnabled(true);
		const std::vector<SampleFrame> signal = makeSine(Case1Dbfs, 4.0);
		feedInBlocks(tap, signal, 512);
		const MasterLoudnessTap::Snapshot measured = tap.snapshot();

		tap.setEnabled(false);
		feedInBlocks(tap, signal, 512);
		const MasterLoudnessTap::Snapshot after = tap.snapshot();

		QVERIFY(!after.enabled);
		QCOMPARE(after.blocksFed, measured.blocksFed);
		QCOMPARE(after.framesFed, measured.framesFed);
		QCOMPARE(after.integratedLufs, measured.integratedLufs);
		QVERIFY(std::fabs(after.integratedLufs - Case1Dbfs) <= ToleranceLu);
	}

	//! reset() starts a new measurement while leaving the tap armed, and it says
	//! so (true) rather than silently keeping the old one. It is the third way
	//! to ask for a fresh window (re-arm, reset, or a fresh object), and the only
	//! one that does not change the armed flag.
	void resetStartsANewMeasurementWhileStayingArmed()
	{
		MasterLoudnessTap tap(SampleRate, DEFAULT_CHANNELS);
		tap.setEnabled(true);
		const std::vector<SampleFrame> signal = makeSine(Case1Dbfs, 4.0);
		feedInBlocks(tap, signal, 512);
		QVERIFY(std::fabs(tap.snapshot().integratedLufs - Case1Dbfs) <= ToleranceLu);

		QVERIFY(tap.reset());
		QVERIFY(tap.enabled());
		const MasterLoudnessTap::Snapshot fresh = tap.snapshot();
		QCOMPARE(fresh.blocksFed, 0ull);
		QCOMPARE(fresh.integratedLufs, LufsMeter::MinusInfinity);
	}

	//! The snapshot reports the rate and channel count the meter was built with,
	//! so a caller reading `null` can tell "the engine is at a rate I did not
	//! build the meter for" from "nothing has sounded yet".
	void theSnapshotReportsTheMetersOwnConfiguration()
	{
		MasterLoudnessTap tap(44100, DEFAULT_CHANNELS);
		const MasterLoudnessTap::Snapshot reading = tap.snapshot();
		QCOMPARE(static_cast<int>(reading.sampleRate), 44100);
		QCOMPARE(static_cast<int>(reading.channels), static_cast<int>(DEFAULT_CHANNELS));
		QVERIFY(!reading.enabled);
	}
};

QTEST_GUILESS_MAIN(MeterTapTest)
#include "MeterTapTest.moc"
