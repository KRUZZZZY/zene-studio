/*
 * LufsMeterTest.cpp - compliance tests for the ITU-R BS.1770-4 / EBU R128
 *                     loudness and true-peak meter
 *
 * Copyright (c) 2026 Zene Studio developers
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

#include "LufsMeter.h"

#include <QtTest>

#include <array>
#include <cmath>
#include <complex>
#include <vector>

#include "AllocationProbe.h"

using lmms::LufsMeter;
using lmms::SampleFrame;
using lmms::sample_t;

namespace
{

constexpr int SampleRate = 48000;
constexpr double Pi = 3.14159265358979323846;

//! EBU Tech 3341 test signals are 1 kHz sines "in phase in both channels, peak
//! level of each channel" the stated number of dB below full scale.
std::vector<SampleFrame> makeSine(double peakDbfs, double seconds, double frequencyHz = 1000.0, double phase = 0.0)
{
	const double amplitude = std::pow(10.0, peakDbfs / 20.0);
	const auto frames = static_cast<std::size_t>(seconds * SampleRate);
	std::vector<SampleFrame> buffer(frames);
	for (std::size_t i = 0; i < frames; ++i)
	{
		const auto value = static_cast<sample_t>(
			amplitude * std::sin(2.0 * Pi * frequencyHz * static_cast<double>(i) / SampleRate + phase));
		buffer[i] = SampleFrame(value, value);
	}
	return buffer;
}

//! Digital silence - not a very quiet tone, but every sample at exactly 0.0.
std::vector<SampleFrame> makeSilence(double seconds)
{
	return std::vector<SampleFrame>(static_cast<std::size_t>(seconds * SampleRate), SampleFrame(0.0f, 0.0f));
}

//! Full-scale square wave: the sample peak is exactly 1.0 while the waveform
//! between the samples overshoots it.
std::vector<SampleFrame> makeSquare(double seconds, double frequencyHz)
{
	const auto frames = static_cast<std::size_t>(seconds * SampleRate);
	std::vector<SampleFrame> buffer(frames);
	for (std::size_t i = 0; i < frames; ++i)
	{
		const double value = std::sin(2.0 * Pi * frequencyHz * static_cast<double>(i) / SampleRate) >= 0.0 ? 1.0 : -1.0;
		buffer[i] = SampleFrame(static_cast<sample_t>(value), static_cast<sample_t>(value));
	}
	return buffer;
}

std::vector<SampleFrame> concat(const std::vector<SampleFrame>& first, const std::vector<SampleFrame>& second)
{
	std::vector<SampleFrame> joined = first;
	joined.insert(joined.end(), second.begin(), second.end());
	return joined;
}

void feed(LufsMeter& meter, const std::vector<SampleFrame>& buffer)
{
	meter.processBlock(buffer.data(), buffer.size());
}

double samplePeakDbfs(const std::vector<SampleFrame>& buffer)
{
	double peak = 0.0;
	for (const auto& frame : buffer)
	{
		peak = std::max(peak, std::fabs(static_cast<double>(frame.left())));
	}
	return 20.0 * std::log10(peak);
}

//! Magnitude response of one biquad section at \p frequencyHz, in dB.
double gainDb(const LufsMeter::Biquad& section, double frequencyHz)
{
	const std::complex<double> z = std::exp(std::complex<double>(0.0, -2.0 * Pi * frequencyHz / SampleRate));
	const std::complex<double> numerator = section.b0 + section.b1 * z + section.b2 * z * z;
	const std::complex<double> denominator = 1.0 + section.a1 * z + section.a2 * z * z;
	return 20.0 * std::log10(std::abs(numerator / denominator));
}

//! Message for a tolerance failure: the measured value has to be in it, or a
//! red run cannot be diagnosed.
QString describe(double actual, double expected, double tolerance)
{
	return QStringLiteral("measured %1, expected %2 +/- %3")
		.arg(actual, 0, 'f', 4).arg(expected, 0, 'f', 4).arg(tolerance, 0, 'f', 4);
}

//! One planar channel of a six-channel (5.1) signal, all channels carrying the
//! same -23 dBFS tone unless \p levelDbfs says otherwise.
std::vector<sample_t> planarChannel(double levelDbfs, std::size_t frames)
{
	const auto sine = makeSine(levelDbfs, static_cast<double>(frames) / SampleRate);
	std::vector<sample_t> channel;
	channel.reserve(sine.size());
	for (const auto& frame : sine)
	{
		channel.push_back(frame.left());
	}
	return channel;
}

} // namespace

class LufsMeterTest : public QObject
{
	Q_OBJECT
private slots:
	//! The 48 kHz coefficient rows the meter derives must be the published
	//! BS.1770-4 Table 1 rows: that is what makes the measurement the
	//! standard's, not an approximation of it.
	void kWeightingMatchesThePublishedTable()
	{
		LufsMeter::Biquad preFilter;
		LufsMeter::Biquad rlbHighPass;
		LufsMeter::kWeightingCoefficients(SampleRate, preFilter, rlbHighPass);

		// BS.1770-4 Table 1 (48 kHz, 15 significant digits as published).
		const double publishedPreB[3] = { 1.53512485958697, -2.69169618940638, 1.19839281085285 };
		const double publishedPreA[2] = { -1.69065929318241, 0.73248077421585 };
		const double publishedRlbA[2] = { -1.99004745483398, 0.99007225036621 };

		QVERIFY2(std::fabs(preFilter.b0 - publishedPreB[0]) < 1e-9, qPrintable(describe(preFilter.b0, publishedPreB[0], 1e-9)));
		QVERIFY2(std::fabs(preFilter.b1 - publishedPreB[1]) < 1e-9, qPrintable(describe(preFilter.b1, publishedPreB[1], 1e-9)));
		QVERIFY2(std::fabs(preFilter.b2 - publishedPreB[2]) < 1e-9, qPrintable(describe(preFilter.b2, publishedPreB[2], 1e-9)));
		QVERIFY2(std::fabs(preFilter.a1 - publishedPreA[0]) < 1e-9, qPrintable(describe(preFilter.a1, publishedPreA[0], 1e-9)));
		QVERIFY2(std::fabs(preFilter.a2 - publishedPreA[1]) < 1e-9, qPrintable(describe(preFilter.a2, publishedPreA[1], 1e-9)));
		QVERIFY2(std::fabs(rlbHighPass.b0 - 1.0) < 1e-12, qPrintable(describe(rlbHighPass.b0, 1.0, 1e-12)));
		QVERIFY2(std::fabs(rlbHighPass.b1 + 2.0) < 1e-12, qPrintable(describe(rlbHighPass.b1, -2.0, 1e-12)));
		QVERIFY2(std::fabs(rlbHighPass.b2 - 1.0) < 1e-12, qPrintable(describe(rlbHighPass.b2, 1.0, 1e-12)));
		QVERIFY2(std::fabs(rlbHighPass.a1 - publishedRlbA[0]) < 1e-9, qPrintable(describe(rlbHighPass.a1, publishedRlbA[0], 1e-9)));
		QVERIFY2(std::fabs(rlbHighPass.a2 - publishedRlbA[1]) < 1e-9, qPrintable(describe(rlbHighPass.a2, publishedRlbA[1], 1e-9)));
	}

	//! The -0.691 of the loudness formula is not a free constant: it is the
	//! K-weighting gain at the recommendation's 997 Hz reference frequency, so a
	//! 997 Hz tone reads its own level. Checks both stages at once, which a
	//! coefficient comparison alone cannot.
	void kWeightingGainAt997HzIsTheCalibrationOffset()
	{
		LufsMeter::Biquad preFilter;
		LufsMeter::Biquad rlbHighPass;
		LufsMeter::kWeightingCoefficients(SampleRate, preFilter, rlbHighPass);

		const double gain = gainDb(preFilter, 997.0) + gainDb(rlbHighPass, 997.0);
		QVERIFY2(std::fabs(gain - 0.691) < 0.005, qPrintable(describe(gain, 0.691, 0.005)));
	}

	//! EBU Tech 3341 cases 1 and 2: a stereo 1 kHz sine at -23 / -33 dBFS (peak
	//! per channel) reads -23.0 / -33.0 LUFS integrated. The two readings come
	//! out of the same code path 10 LU apart, so a comparator that always
	//! returns the same value fails one of them.
	void integrationLoudnessReadsTheTestSignalLevel()
	{
		LufsMeter loudMeter(SampleRate, 2);
		feed(loudMeter, makeSine(-23.0, 20.0));
		const float loud = loudMeter.integratedLufs();

		LufsMeter quietMeter(SampleRate, 2);
		feed(quietMeter, makeSine(-33.0, 20.0));
		const float quiet = quietMeter.integratedLufs();

		QVERIFY2(std::fabs(loud - -23.0) <= 0.1, qPrintable(describe(loud, -23.0, 0.1)));
		QVERIFY2(std::fabs(quiet - -33.0) <= 0.1, qPrintable(describe(quiet, -33.0, 0.1)));
		QVERIFY2(std::fabs((loud - quiet) - 10.0) <= 0.02, qPrintable(describe(loud - quiet, 10.0, 0.02)));
		QVERIFY2(std::fabs(loudMeter.momentaryLufs() - -23.0) <= 0.1, qPrintable(describe(loudMeter.momentaryLufs(), -23.0, 0.1)));
		QVERIFY2(std::fabs(loudMeter.shortTermLufs() - -23.0) <= 0.1, qPrintable(describe(loudMeter.shortTermLufs(), -23.0, 0.1)));
	}

	//! EBU Tech 3341 case 3 / the relative gate: 10 s at -36 dBFS followed by
	//! 20 s at -23 dBFS reads -23.0 LUFS, not the -24.65 LUFS the ungated mean
	//! would give. The control is a constant tone at that -24.65 dBFS level: it
	//! measures -24.65 through the same path, 1.6 LU away, so the gate
	//! demonstrably threw the quiet passage away rather than a comparator
	//! hiding the error.
	void relativeGateExcludesTheQuietPassage()
	{
		LufsMeter gated(SampleRate, 2);
		feed(gated, concat(makeSine(-36.0, 10.0), makeSine(-23.0, 20.0)));
		const float gatedReading = gated.integratedLufs();

		LufsMeter control(SampleRate, 2);
		feed(control, makeSine(-24.65, 30.0));
		const float controlReading = control.integratedLufs();

		QVERIFY2(std::fabs(gatedReading - -23.0) <= 0.1, qPrintable(describe(gatedReading, -23.0, 0.1)));
		QVERIFY2(std::fabs(controlReading - -24.65) <= 0.1, qPrintable(describe(controlReading, -24.65, 0.1)));
		QVERIFY2(gatedReading - controlReading > 1.5, qPrintable(describe(gatedReading - controlReading, 1.6, 0.1)));
	}

	//! BS.1770-4 sums the weighted channels, it does not average them: the same
	//! -23 dBFS tone in the left channel alone reads -26.0 LUFS, 3.01 LU below
	//! the two-channel reading.
	void singleChannelSignalIsThreeLuQuieter()
	{
		LufsMeter meter(SampleRate, 2);
		auto buffer = makeSine(-23.0, 20.0);
		for (auto& frame : buffer)
		{
			frame.setRight(0.0f);
		}
		feed(meter, buffer);

		QVERIFY2(std::fabs(meter.integratedLufs() - -26.0) <= 0.1, qPrintable(describe(meter.integratedLufs(), -26.0, 0.1)));
	}

	//! The planar entry point and the 5.1 table: L, R and C weigh 1.0, Ls and Rs
	//! 1.41 (+1.49 dB on the energy) and the LFE channel is not measured - a
	//! tone in the LFE alone never reaches the gate, and adding the same tone to
	//! the LFE does not move a measurement.
	void planarFeedFollowsTheFiveOneWeightings()
	{
		const auto frames = static_cast<std::size_t>(5.0 * SampleRate);
		const auto tone = planarChannel(-23.0, frames);
		// -100 dBFS on the idle channels: 77 LU below the tone, so nothing but
		// the tone can ever reach the absolute gate.
		const auto veryQuiet = planarChannel(-100.0, frames);

		// Only the left channel carries the tone, then only the left surround.
		std::array<std::vector<sample_t>, 6> leftOnly{ veryQuiet, veryQuiet, veryQuiet, veryQuiet, veryQuiet, veryQuiet };
		leftOnly[0] = tone;
		std::array<std::vector<sample_t>, 6> surroundOnly{ veryQuiet, veryQuiet, veryQuiet, veryQuiet, veryQuiet, veryQuiet };
		surroundOnly[4] = tone;
		// The left channel plus an identical tone in the LFE.
		std::array<std::vector<sample_t>, 6> leftAndLfe = leftOnly;
		leftAndLfe[3] = tone;
		// The LFE alone.
		std::array<std::vector<sample_t>, 6> lfeOnly{ veryQuiet, veryQuiet, veryQuiet, veryQuiet, veryQuiet, veryQuiet };
		lfeOnly[3] = tone;

		const auto measure = [frames](std::array<std::vector<sample_t>, 6>& channels) {
			std::array<const sample_t*, 6> pointers{};
			for (std::size_t channel = 0; channel < pointers.size(); ++channel)
			{
				pointers[channel] = channels[channel].data();
			}
			LufsMeter meter(SampleRate, 6);
			meter.processPlanar(pointers.data(), 6, frames);
			return meter.integratedLufs();
		};

		const float left = measure(leftOnly);
		const float leftAndLfeReading = measure(leftAndLfe);
		const float surround = measure(surroundOnly);
		const float lfe = measure(lfeOnly);

		// One weighted channel of a 5.1 layout sums to that channel's energy,
		// exactly as the single-channel stereo case above does: -26.0 LUFS.
		QVERIFY2(std::fabs(left - -26.0) <= 0.1, qPrintable(describe(left, -26.0, 0.1)));
		QVERIFY2(leftAndLfeReading == left, "the LFE channel of a 5.1 layout must not be measured");
		QVERIFY2(std::fabs(surround - (left + 1.49)) <= 0.01, qPrintable(describe(surround - left, 1.49, 0.01)));
		QVERIFY2(lfe == LufsMeter::MinusInfinity, "a tone in the LFE alone must never reach the gate");
	}

	//! Momentary waits for 400 ms, short-term for 3 s, and feeding the same
	//! signal in small blocks instead of one buffer must not move the numbers.
	void windowsFillInOrderAndBlockSizeDoesNotMatter()
	{
		LufsMeter meter(SampleRate, 2);
		feed(meter, makeSine(-23.0, 0.4));
		QVERIFY2(std::fabs(meter.momentaryLufs() - -23.0) <= 0.1, qPrintable(describe(meter.momentaryLufs(), -23.0, 0.1)));
		QVERIFY2(meter.shortTermLufs() == LufsMeter::MinusInfinity, "short-term has to wait for its 3 s window");

		feed(meter, makeSine(-23.0, 3.0));
		QVERIFY2(std::fabs(meter.shortTermLufs() - -23.0) <= 0.1, qPrintable(describe(meter.shortTermLufs(), -23.0, 0.1)));

		const auto wholeSignal = makeSine(-23.0, 5.0);
		LufsMeter blockwise(SampleRate, 2);
		for (std::size_t offset = 0; offset + 512 <= wholeSignal.size(); offset += 512)
		{
			blockwise.processBlock(wholeSignal.data() + offset, 512);
		}
		LufsMeter oneShot(SampleRate, 2);
		feed(oneShot, wholeSignal);
		QVERIFY2(std::fabs(blockwise.integratedLufs() - oneShot.integratedLufs()) <= 0.001,
			qPrintable(describe(blockwise.integratedLufs(), oneShot.integratedLufs(), 0.001)));
	}

	//! Digital silence has no loudness: the sentinel comes back, and silence fed
	//! after a real measurement leaves the running integrated value untouched.
	void silenceReadsMinusInfinityAndKeepsTheRunningValue()
	{
		LufsMeter meter(SampleRate, 2);
		feed(meter, makeSine(-100.0, 2.0));
		QVERIFY2(meter.integratedLufs() == LufsMeter::MinusInfinity, "2 s of -100 dBFS must not pass the absolute gate");
		QVERIFY2(meter.truePeakDbtp() < -90.0f, qPrintable(describe(meter.truePeakDbtp(), -100.0, 10.0)));

		feed(meter, makeSine(-23.0, 20.0));
		const float measured = meter.integratedLufs();
		const float peak = meter.truePeakDbtp();
		QVERIFY2(std::fabs(measured - -23.0) <= 0.1, qPrintable(describe(measured, -23.0, 0.1)));

		feed(meter, makeSilence(4.0));
		// The 400 ms blocks that straddle the end of the tone are part of the
		// measurement, so the running value may fall a little (here by 0.03 LU);
		// it must stay a measurement of the same signal, not fall apart. A lower
		// LUFS number is a quieter reading, hence the direction of the compare.
		QVERIFY2(meter.integratedLufs() < measured, "the straddling silent blocks may only pull the running value down");
		QVERIFY2(measured - meter.integratedLufs() <= 0.1, qPrintable(describe(meter.integratedLufs(), measured, 0.1)));
		QVERIFY2(meter.momentaryLufs() == LufsMeter::MinusInfinity, "400 ms of silence has no momentary loudness");
		// The 3 s window still holds the K-weighting filters' exponentially
		// decaying tail hundreds of dB down, so it is not the sentinel but it is
		// far below anything a signal can be.
		QVERIFY2(meter.shortTermLufs() < -300.0f, qPrintable(describe(meter.shortTermLufs(), -2000.0, 1700.0)));
		QVERIFY2(meter.truePeakDbtp() == peak, "silence must not change the peak");

		meter.reset();
		QVERIFY2(meter.integratedLufs() == LufsMeter::MinusInfinity, "reset must clear the measurement");
		QVERIFY2(meter.truePeakDbtp() == LufsMeter::MinusInfinity, "reset must clear the peak");
	}

	//! True peak: a full-scale sine at a quarter of the sample rate, 45 degrees
	//! off the sample instants, has a sample peak of -3.01 dBFS - a peak-sample
	//! reader under-reads the 0 dBTP waveform by 3 LU. The square wave is the
	//! other direction: its samples sit at full scale and the waveform between
	//! them overshoots to about +2 dBTP.
	void truePeakOversamplesTheSignal()
	{
		LufsMeter meter(SampleRate, 2);
		const auto interSampleRock = makeSine(0.0, 1.0, SampleRate / 4.0, Pi / 4.0);
		feed(meter, interSampleRock);

		const double samplePeak = samplePeakDbfs(interSampleRock);
		const double truePeak = meter.truePeakDbtp();
		QVERIFY2(std::fabs(samplePeak - -3.01) <= 0.02, qPrintable(describe(samplePeak, -3.01, 0.02)));
		QVERIFY2(std::fabs(truePeak - 0.0) <= 0.2, qPrintable(describe(truePeak, 0.0, 0.2)));
		QVERIFY2(truePeak - samplePeak > 2.5, qPrintable(describe(truePeak - samplePeak, 3.0, 0.5)));

		LufsMeter squareMeter(SampleRate, 2);
		const auto square = makeSquare(1.0, 1000.0);
		feed(squareMeter, square);
		const double squareSamplePeak = samplePeakDbfs(square);
		QVERIFY2(std::fabs(squareSamplePeak - 0.0) <= 0.01, qPrintable(describe(squareSamplePeak, 0.0, 0.01)));
		QVERIFY2(squareMeter.truePeakDbtp() > 1.0, qPrintable(describe(squareMeter.truePeakDbtp(), 2.0, 1.0)));
		QVERIFY2(squareMeter.truePeakDbtp() < 3.0, qPrintable(describe(squareMeter.truePeakDbtp(), 2.0, 1.0)));
	}

	//! Program rule (AGENTS.md): a method that can run on the audio thread must
	//! not allocate. The probe counts every allocation on this thread, so the
	//! measured block loop has to leave it at zero.
	void processingABlockAllocatesNothing()
	{
		LufsMeter meter(SampleRate, 2);
		const auto buffer = makeSine(-23.0, 0.5);
		feed(meter, buffer); // warm-up outside the probe: nothing is allocated later

		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		for (int block = 0; block < 64; ++block)
		{
			meter.processBlock(buffer.data(), buffer.size());
			const auto reading = meter.read();
			QVERIFY(reading.integratedLufs <= 0.0f);
		}
		lmms::test::tlCountAllocations = false;

		QCOMPARE(static_cast<qulonglong>(lmms::test::tlAllocationCount), static_cast<qulonglong>(0));
	}
};

QTEST_GUILESS_MAIN(LufsMeterTest)
#include "LufsMeterTest.moc"
