/*
 * ExportNoiseShaperTest.cpp - noise-shaped TPDF requantisation and the dither mode
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

//! Owner decision 13: dither becomes a mode, and 'noise_shaped' is the new one.
//!
//!  * the shaper is deterministic (same seed, same bytes) and reset() reproduces;
//!  * it SHAPES: against a flat-TPDF reference quantiser drawing from the same
//!    generator, the error energy in the low band falls (the NTF predicts -12 dB at
//!    DC; the test asks for more than half the energy gone) and the high-band energy
//!    rises - the noise moved, it did not disappear;
//!  * the error has no DC bias, and silence stays within a few LSB but is dithered;
//!  * the byte-swapped layout is the plain one swapped;
//!  * the pre-mode bool API maps exactly: setDither(true) is TPDF, any mode but Off
//!    reads back as dither() == true, reset() is Off.
//!
//! TPDF's own bytes being unchanged is ExportWavDitherTest's subject, not this file's.

#include <QtTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "ExportDither.h"
#include "ExportDitherMode.h"
#include "ExportNoiseShaper.h"
#include "ExportRenderSettings.h"

using namespace lmms;

namespace
{

constexpr int kRate = 48000;
constexpr int kSamples = 48000;
constexpr float kScale = 32767.0f;

std::vector<float> quietSine()
{
	std::vector<float> x(kSamples);
	const double amplitude = std::pow(10.0, -60.0 / 20.0);
	for (int n = 0; n < kSamples; ++n)
	{
		x[n] = static_cast<float>(amplitude * std::sin(2.0 * M_PI * 997.0 * n / kRate));
	}
	return x;
}

//! The requantisation error in LSB for each sample of @a x under the shaper.
std::vector<double> shapedError(const std::vector<float>& x)
{
	ExportNoiseShaper shaper(1);
	std::vector<double> e(x.size());
	for (std::size_t n = 0; n < x.size(); ++n)
	{
		e[n] = shaper.quantise16(x[n], 0) - static_cast<double>(x[n]) * kScale;
	}
	return e;
}

//! The same error under FLAT TPDF with round-to-nearest - the shaper minus the shaping.
std::vector<double> flatError(const std::vector<float>& x)
{
	ExportDither dither;
	std::vector<double> e(x.size());
	for (std::size_t n = 0; n < x.size(); ++n)
	{
		const double target = static_cast<double>(x[n]) * kScale;
		e[n] = static_cast<double>(std::lround(target + dither.nextOffset(1.0f))) - target;
	}
	return e;
}

//! Energy below ~1.5 kHz: a 32-tap moving average, then the mean square.
double lowBandEnergy(const std::vector<double>& e)
{
	constexpr int taps = 32;
	double sum = 0.0, energy = 0.0;
	for (std::size_t n = 0; n < e.size(); ++n)
	{
		sum += e[n];
		if (n >= taps) { sum -= e[n - taps]; }
		if (n >= taps) { const double avg = sum / taps; energy += avg * avg; }
	}
	return energy / static_cast<double>(e.size() - taps);
}

//! Energy weighted toward Nyquist: the mean square of the first difference.
double highBandEnergy(const std::vector<double>& e)
{
	double energy = 0.0;
	for (std::size_t n = 1; n < e.size(); ++n) { const double d = e[n] - e[n - 1]; energy += d * d; }
	return energy / static_cast<double>(e.size() - 1);
}

} // namespace

class ExportNoiseShaperTest : public QObject
{
	Q_OBJECT

private slots:
//! Cluster new-p20-45 pin: the feedback error is measured against the shaped
//! target - `rounded - shaped` - i.e. the dither offset is part of the error
//! the loop shapes, so the documented NTF (1.25x DC = -12 dB, 3.71x Nyquist)
//! applies to the total noise. A reference model that replays the identical
//! float arithmetic (ExportNoiseShaper::Coefficients, the same seeded
//! ExportDither, the same clamp) must reproduce the shaper bit-exactly,
//! while the alternative convention (`rounded - dithered`) diverges. The
//! spectral test above already requires the <-3 dB low-band suppression
//! this convention delivers.
void theFeedbackShapesTheDitherOffsetAsDocumented()
{
	const std::vector<float> x = quietSine();
	ExportNoiseShaper shaper(1);
	ExportDither referenceDither;
	std::array<float, 3> history{0.f, 0.f, 0.f};
	int divergedFromAlternative = 0;
	for (float v : x)
	{
		const std::int16_t got = shaper.quantise16(v, 0);
		// Identical arithmetic to ExportNoiseShaper::quantise16: the
		// inputs used here are strictly inside (-1, 1) so AudioEngine::clip
		// is the identity on them.
		const float target = v * kScale;
		const float shaped = target - (ExportNoiseShaper::Coefficients[0] * history[0]
			+ ExportNoiseShaper::Coefficients[1] * history[1]
			+ ExportNoiseShaper::Coefficients[2] * history[2]);
		const float dithered = shaped + referenceDither.nextOffset(1.0f);
		const long rounded = std::clamp(std::lround(dithered), -32768L, 32767L);
		const float error = std::clamp(static_cast<float>(rounded) - shaped,
			-ExportNoiseShaper::MaxFeedbackLsb, ExportNoiseShaper::MaxFeedbackLsb);
		QCOMPARE(got, static_cast<std::int16_t>(rounded));
		const long alternative = std::clamp(std::lround(dithered), -32768L, 32767L);
		const float alternativeError = std::clamp(static_cast<float>(alternative) - dithered,
			-ExportNoiseShaper::MaxFeedbackLsb, ExportNoiseShaper::MaxFeedbackLsb);
		divergedFromAlternative += (alternativeError != error);
		history = {error, history[0], history[1]};
	}
	QVERIFY2(divergedFromAlternative > 0, "the alternative feedback convention never diverged");
}

//! Cluster new-p10-123: a non-finite sample must not poison the rest of the
//! channel. AudioEngine::clip passes NaN straight through (its `s > 1` /
//! `s < -1` guards are both false for NaN), so `lround` receives NaN - in
//! this build it returns the long minimum and the value is clamped to
//! -32768 - and `rounded - shaped` is NaN, which then lives in the channel's
//! error history and turns later samples into NaN as well. The guarded unit
//! below discards the non-finite sample as silence and resets that channel's
//! history; the dither draws nothing while discarding, so later samples
//! reproduce a fresh run sample for sample.
void aNonFiniteSampleDoesNotPoisonLaterSamples()
{
	ExportNoiseShaper victim(1), reference(1);
	const float nan = std::numeric_limits<float>::quiet_NaN();
	QVERIFY2(victim.quantise16(nan, 0) == 0, "a NaN sample quantised to a non-silence value");
	QVERIFY2(victim.quantise16(std::numeric_limits<float>::infinity(), 0) == reference.quantise16(std::numeric_limits<float>::infinity(), 0),
		"Inf does not match the plain clipped path");
	for (int n = 0; n < 64; ++n)
	{
		const std::int16_t got = victim.quantise16(0.25f, 0);
		const std::int16_t want = reference.quantise16(0.25f, 0);
		// Fresh-run equality requires the NaN to have advanced neither the
		// history nor the dither; a poisoned run instead pins to -32768.
		QVERIFY2(got == want, qPrintable(QStringLiteral(
			"sample %1 after NaN: got %2, fresh reference %3 (poisoned history?)").arg(n).arg(got).arg(want)));
	}

	// quantiseFrames routes through quantise16, so the frame path must
	// carry the same guard: a NaN frame is silence and the next frame
	// must not be pinned.
	ExportNoiseShaper frames(1), framesRef(1);
	std::vector<SampleFrame> in(4);
	for (int n = 0; n < 4; ++n) { in[n] = SampleFrame(0.25f, 0.0f); }
	in[0] = SampleFrame(nan, nan);
	std::vector<int_sample_t> out(4, 12345), refOut(4, 12345);
	frames.quantiseFrames(in.data(), 4, 1, out.data(), false);
	framesRef.quantiseFrames(std::vector<SampleFrame>(4, SampleFrame(0.25f, 0.0f)).data(), 4, 1, refOut.data(), false);
	QVERIFY2(out[0] == 0, "the NaN frame was not written as silence");
	for (int n = 1; n < 4; ++n)
	{
		QVERIFY2(out[n] == refOut[n - 1], qPrintable(QStringLiteral(
			"frame %1 after NaN: got %2, fresh reference %3").arg(n).arg(out[n]).arg(refOut[n - 1])));
	}
}

	void theSameSeedGivesTheSameSamplesAndResetReproduces()
	{
		const std::vector<float> x = quietSine();
		ExportNoiseShaper a(1), b(1);
		std::vector<std::int16_t> first;
		for (float v : x) { const auto q = a.quantise16(v, 0); QCOMPARE(q, b.quantise16(v, 0)); first.push_back(q); }
		a.reset();
		for (std::size_t n = 0; n < x.size(); ++n) { QCOMPARE(a.quantise16(x[n], 0), first[n]); }
	}

	void theNoiseMovesOutOfTheLowBand()
	{
		const std::vector<float> x = quietSine();
		const std::vector<double> shaped = shapedError(x);
		const std::vector<double> flat = flatError(x);
		const double lowShaped = lowBandEnergy(shaped), lowFlat = lowBandEnergy(flat);
		const double highShaped = highBandEnergy(shaped), highFlat = highBandEnergy(flat);
		std::printf("EVIDENCE low band: flat %.6g shaped %.6g (%.1f dB); high band: flat %.6g shaped %.6g (%.1f dB)\n",
			lowFlat, lowShaped, 10.0 * std::log10(lowShaped / lowFlat),
			highFlat, highShaped, 10.0 * std::log10(highShaped / highFlat));
		QVERIFY2(lowShaped < 0.5 * lowFlat, "noise shaping did not lower the low-band error energy by 3 dB");
		QVERIFY2(highShaped > highFlat, "noise shaping did not move energy toward the high band");
	}

	void theErrorHasNoDcBias()
	{
		const std::vector<double> e = shapedError(quietSine());
		double mean = 0.0;
		for (double v : e) { mean += v; }
		mean /= static_cast<double>(e.size());
		QVERIFY2(std::abs(mean) < 0.05, qPrintable(QStringLiteral("mean error %1 LSB").arg(mean)));
	}

	void silenceIsDitheredButStaysSmall()
	{
		ExportNoiseShaper shaper(1);
		int nonZero = 0;
		for (int n = 0; n < 4800; ++n)
		{
			const int q = shaper.quantise16(0.0f, 0);
			QVERIFY2(std::abs(q) <= 6, qPrintable(QStringLiteral("silence quantised to %1").arg(q)));
			nonZero += q != 0;
		}
		QVERIFY2(nonZero > 0, "silence produced no dither at all");
	}

	void theSwappedLayoutIsThePlainOneSwapped()
	{
		std::vector<SampleFrame> frames(64);
		for (int n = 0; n < 64; ++n) { frames[n] = SampleFrame(0.3f * std::sin(n * 0.2f), -0.2f); }
		std::vector<int_sample_t> plain(128), swapped(128);
		ExportNoiseShaper a(2), b(2);
		a.quantiseFrames(frames.data(), 64, 2, plain.data(), false);
		b.quantiseFrames(frames.data(), 64, 2, swapped.data(), true);
		for (int i = 0; i < 128; ++i)
		{
			const auto p = static_cast<std::uint16_t>(plain[i]);
			QCOMPARE(static_cast<std::uint16_t>(swapped[i]), static_cast<std::uint16_t>((p & 0xff) << 8 | p >> 8));
		}
	}

	//! Cluster 5048: the error history is sized by the constructor's channel
	//! count, but quantise16 indexed m_error[channel] and quantiseFrames looped a
	//! caller-supplied width with no check. Constructing a mono shaper and asking
	//! for two channels read past the vector - undefined behaviour that turned
	//! into whatever heap bytes followed it. An unavailable channel must be
	//! silence instead, and the extra slots must be written as silence too.
	void aChannelOutsideTheErrorHistoryIsSilence()
	{
		ExportNoiseShaper mono(1);
		for (int channel : {-1, 1, 2, 99})
		{
			const std::int16_t value = mono.quantise16(0.5f, channel);
			QVERIFY2(value == 0, qPrintable(QStringLiteral(
				"channel %1 outside a 1-channel history quantised to %2, not silence")
				.arg(channel).arg(value)));
		}

		std::vector<SampleFrame> frames(32, SampleFrame(0.5f, -0.5f));
		std::vector<int_sample_t> out(64, 12345);
		mono.quantiseFrames(frames.data(), 32, 2, out.data(), false);
		// Channel 0 is in range and must still be shaped ...
		QVERIFY2(out[0] != 12345, "the in-range channel was not written at all");
		// ... every channel past the allocated history must be exact silence.
		for (int n = 0; n < 32; ++n)
		{
			const auto second = static_cast<std::uint16_t>(out[n * 2 + 1]);
			QVERIFY2(second == 0, qPrintable(QStringLiteral(
				"frame %1 channel 1 (outside a 1-channel history) wrote %2 instead of silence")
				.arg(n).arg(second)));
		}
	}

	void theBoolApiMapsOntoTheModes()
	{
		ExportRenderSettings::reset();
		QCOMPARE(ExportRenderSettings::ditherMode(), DitherMode::Off);
		ExportRenderSettings::setDither(true);
		QCOMPARE(ExportRenderSettings::ditherMode(), DitherMode::Tpdf);
		ExportRenderSettings::setDitherMode(DitherMode::NoiseShaped);
		QVERIFY(ExportRenderSettings::dither());
		ExportRenderSettings::setDither(false);
		QCOMPARE(ExportRenderSettings::ditherMode(), DitherMode::Off);
		for (const QString& name : ditherModeNames())
		{
			DitherMode mode = DitherMode::Off;
			QVERIFY(ditherModeFromName(name, &mode));
			QCOMPARE(ditherModeName(mode), name);
		}
		DitherMode untouched = DitherMode::Tpdf;
		QVERIFY(!ditherModeFromName(QStringLiteral("rpdf"), &untouched));
		QCOMPARE(untouched, DitherMode::Tpdf);
		// A null out pointer is refused, not dereferenced: a valid name used to
		// write through it (ExportDitherMode.h's *mode store).
		for (const QString& name : ditherModeNames())
		{
			QVERIFY(!ditherModeFromName(name, nullptr));
		}
		QVERIFY(!ditherModeFromName(QStringLiteral("rpdf"), nullptr));
		ExportRenderSettings::reset();
	}
};

QTEST_GUILESS_MAIN(ExportNoiseShaperTest)
#include "ExportNoiseShaperTest.moc"
