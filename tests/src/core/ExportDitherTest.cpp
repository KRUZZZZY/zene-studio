/*
 * ExportDitherTest.cpp - the TPDF export dither itself (include/ExportDither.h):
 *                        its distribution, its effect on the quantiser, and the
 *                        defaults that keep it out of the way.
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

/*! What this file proves, and why each assertion is the one it is.
 *
 *  A dither is not "a function that ran": it is a claimed DISTRIBUTION and a
 *  claimed EFFECT, and both are asserted here statistically, with an inverted
 *  control - a check that cannot fail is not a check.
 *
 *  1. THE DISTRIBUTION. TPDF means the offset is the sum of two uniforms, so
 *     its variance is 1/6 LSB^2 - and NOT 1/12 LSB^2, which is what a single
 *     uniform (RPDF) would give. The variance assertion therefore separates
 *     TPDF from RPDF, which is the whole reason TPDF is the one implemented.
 *
 *  2. THE EFFECT, as a quantity with a closed form. Without dither the
 *     quantisation error is ONE deterministic value per input level - a
 *     function of the signal, i.e. distortion. With TPDF dither it becomes a
 *     distribution, and the probability that the quantiser's decision changes
 *     is exactly u^2/2 for a level sitting u LSB above the step below it. The
 *     assertion is that prediction, not a shape. (The engine's float -> int
 *     conversion TRUNCATES, so the textbook "total error LSB^2/4" figure for a
 *     ROUNDING quantiser is deliberately NOT asserted here; asserting it would
 *     be a fabricated claim about this engine.)
 *
 *  3. OFF BY DEFAULT. This release's reproducibility claim is byte-identical
 *     renders, so the default is asserted twice: as a value (OutputSettings,
 *     ExportRenderSettings) and - in ExportWavDitherTest.cpp - as BYTES.
 *
 *  Every number the test measures is printed as a DITHER_EVIDENCE line so a
 *  reader can check it by hand.
 *
 *  The wiring half (does the dither reach a WAV?) is
 *  tests/src/core/ExportWavDitherTest.cpp, split out to keep both files inside
 *  the 500-line ratchet.
 */

#include <QtTest>

#include <cmath>
#include <cstdio>
#include <vector>

#include "AudioEngine.h"
#include "ExportDither.h"
#include "ExportRenderSettings.h"
#include "OutputSettings.h"
#include "SampleFrame.h"
#include "SrcQuality.h"

using namespace lmms;

namespace
{

//! The 16-bit quantiser the export path actually uses: clip to full scale,
//! scale by the engine's own multiplier, and cast (which TRUNCATES - it does
//! not round; src/core/audio/AudioDevice.cpp convertToS16).
int_sample_t quantise16(float value)
{
	const float clipped = AudioEngine::clip(value);
	return static_cast<int_sample_t>(clipped * OUTPUT_SAMPLE_MULTIPLIER);
}

//! The error the quantiser leaves behind, in the float domain.
double quantisationError(float value)
{
	return static_cast<double>(quantise16(value)) / OUTPUT_SAMPLE_MULTIPLIER
		- static_cast<double>(value);
}

double mean(const std::vector<double>& values)
{
	double sum = 0.0;
	for (double v : values) { sum += v; }
	return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}

double variance(const std::vector<double>& values)
{
	const double m = mean(values);
	double sum = 0.0;
	for (double v : values) { sum += (v - m) * (v - m); }
	return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}

//! Pearson correlation; 0 when either series is constant (nothing to correlate).
double correlation(const std::vector<double>& a, const std::vector<double>& b)
{
	const double ma = mean(a);
	const double mb = mean(b);
	double num = 0.0;
	double da = 0.0;
	double db = 0.0;
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		num += (a[i] - ma) * (b[i] - mb);
		da += (a[i] - ma) * (a[i] - ma);
		db += (b[i] - mb) * (b[i] - mb);
	}
	if (da <= 0.0 || db <= 0.0) { return 0.0; }
	return num / std::sqrt(da * db);
}

//! Lag-1 autocorrelation of one series against itself, shifted. Used to show
//! the generator is not repeating - successive offsets are independent.
double lagOneAutocorrelation(const std::vector<double>& values)
{
	std::vector<double> shifted(values.begin(), values.end());
	shifted.erase(shifted.begin());
	std::vector<double> head(values.begin(), values.end() - 1);
	return correlation(head, shifted);
}

void evidence(const char* label, double a, double b = 0.0)
{
	std::printf("DITHER_EVIDENCE %-52s %14.9f %14.9f\n", label, a, b);
	std::fflush(stdout);
}

} // namespace

class ExportDitherTest : public QObject
{
	Q_OBJECT

private slots:

	void initTestCase()
	{
		ExportRenderSettings::reset();
	}

	void cleanupTestCase()
	{
		ExportRenderSettings::reset();
	}

	// -----------------------------------------------------------------------
	// 1. The distribution: TPDF, and specifically NOT RPDF.
	// -----------------------------------------------------------------------

	void theOffsetsAreTriangularNotUniform()
	{
		constexpr std::size_t count = 200000;
		const float lsb = ExportDither::lsbForBitDepth(16);
		QVERIFY2(lsb > 0.0f, "16-bit must have a quantisation step");

		ExportDither dither;
		std::vector<double> offsets(count);
		double maxAbs = 0.0;
		for (std::size_t i = 0; i < count; ++i)
		{
			const double offset = static_cast<double>(dither.nextOffset(lsb));
			offsets[i] = offset;
			maxAbs = std::max(maxAbs, std::fabs(offset));
		}

		const double m = mean(offsets);
		const double v = variance(offsets);
		const double expectedVar = static_cast<double>(lsb) * static_cast<double>(lsb) / 6.0;
		const double rpdfVar = static_cast<double>(lsb) * static_cast<double>(lsb) / 12.0;
		const double autocorrelation = lagOneAutocorrelation(offsets);
		evidence("offset mean (LSB)", m / lsb);
		evidence("offset variance / LSB^2", v / (static_cast<double>(lsb) * lsb));
		evidence("offset variance vs TPDF 1/6", v / expectedVar);
		evidence("offset variance vs RPDF 1/12", v / rpdfVar);
		evidence("offset lag-1 autocorrelation", autocorrelation);
		evidence("offset max |value| (LSB)", maxAbs / lsb);

		// Zero mean: the dither must not shift the programme material.
		QVERIFY2(std::fabs(m) < 0.02 * lsb,
			qPrintable(QStringLiteral("TPDF mean %1 LSB is not ~0").arg(m / lsb)));

		// 1/6 LSB^2, within 5 % of 200000 samples' sampling error (~0.3 %).
		QVERIFY2(std::fabs(v / expectedVar - 1.0) < 0.05,
			qPrintable(QStringLiteral("TPDF variance is %1x the 1/6 LSB^2 it should be")
				.arg(v / expectedVar)));

		// THE DISCRIMINATOR: a single uniform (RPDF) would give half this
		// variance. If the implementation ever degrades to RPDF, this fails.
		QVERIFY2(v > 1.5 * rpdfVar,
			qPrintable(QStringLiteral("variance %1 LSB^2 is RPDF-shaped (1/12), not TPDF (1/6)")
				.arg(v / (static_cast<double>(lsb) * lsb))));

		// It is a DITHER, not a noise generator: bounded by 1 LSB.
		QVERIFY2(maxAbs <= lsb * 1.0000001, "a TPDF offset must be within +/- 1 LSB");
		QVERIFY2(maxAbs > 0.95 * lsb, "the offsets never approach the +/- 1 LSB bound");

		// Successive offsets are independent.
		QVERIFY2(std::fabs(autocorrelation) < 0.02,
			qPrintable(QStringLiteral("lag-1 autocorrelation %1: the generator is repeating")
				.arg(autocorrelation)));
	}

	void theQuantisationStepMatchesTheEngineQuantiser()
	{
		// The multiplier the float -> int conversion uses, so a dither drawn
		// against a different step would be at the wrong level.
		QCOMPARE(ExportDither::lsbForBitDepth(16), 1.0f / OUTPUT_SAMPLE_MULTIPLIER);
		QCOMPARE(ExportDither::lsbForBitDepth(24), 1.0f / 8388607.0f);
		// 32-bit WAV is IEEE float: no quantisation step, so nothing to dither.
		QCOMPARE(ExportDither::lsbForBitDepth(32), 0.0f);
		QCOMPARE(ExportDither::lsbForBitDepth(0), 0.0f);
		evidence("lsb(16) * 32767", ExportDither::lsbForBitDepth(16) * 32767.0f);
	}

	// -----------------------------------------------------------------------
	// 2. The effect, against a closed-form prediction.
	// -----------------------------------------------------------------------

	/*! The engine's float -> int conversion TRUNCATES toward zero
	 *  (`AudioDevice::convertToS16`), it does not round. What is exact for that
	 *  quantiser is the probability that the dither changes the decision:
	 *
	 *  Take a constant input u LSB above the level below it, 0 < u < 1. Without
	 *  dither the output is exactly that level, so the quantisation error is ONE
	 *  deterministic value that is a function of u - the distortion. With a TPDF
	 *  offset d on [-1, +1) LSB, the value truncates UP by one step exactly when
	 *  u + d >= 1, i.e. when d >= 1 - u, whose probability is (1-(1-u))^2/2 =
	 *  u^2/2 (the triangular CDF). A downward flip needs d <= -(1+u), below the
	 *  dither's own bound, so it cannot happen.
	 *
	 *  The test asserts, level by level: the undithered error is a single value;
	 *  the dithered error is NOT; and the observed flip rate matches u^2/2. A
	 *  dither that was not applied, or applied at the wrong amplitude, misses
	 *  that by orders of magnitude rather than by a tolerance.
	 */
	void theDitherTurnsADeterministicErrorIntoADistribution()
	{
		constexpr int levels = 16;
		constexpr std::size_t repeats = 4096;
		const float lsb = ExportDither::lsbForBitDepth(16);

		double widestFlipError = 0.0;
		double plainMin = 1e9;
		double plainMax = -1e9;
		int levelsWithDistribution = 0;

		for (int step = 0; step < levels; ++step)
		{
			// u in (0, 1): the level's position above the quantisation step
			// below it, in LSB.
			const double u = (static_cast<double>(step) + 0.5) / static_cast<double>(levels);
			const float level = static_cast<float>(u) * lsb;
			const auto plainValue = quantise16(level);
			const double plainError = quantisationError(level);
			plainMin = std::min(plainMin, plainError);
			plainMax = std::max(plainMax, plainError);

			// The undithered error: ONE value, at this level and at every
			// repetition, because nothing in the path is random.
			std::vector<double> plain(repeats, plainError);
			QCOMPARE(variance(plain), 0.0);

			// The same quantiser, dither drawn and added first - the order
			// AudioFileWave::writeBuffer uses.
			ExportDither dither;
			std::size_t flips = 0;
			std::vector<double> ditheredError(repeats);
			for (std::size_t i = 0; i < repeats; ++i)
			{
				const auto value = quantise16(level + dither.nextOffset(lsb));
				if (value != plainValue) { ++flips; }
				ditheredError[i] = static_cast<double>(value) / OUTPUT_SAMPLE_MULTIPLIER
					- static_cast<double>(level);
			}
			const double observed = static_cast<double>(flips) / static_cast<double>(repeats);
			const double predicted = u * u / 2.0;

			if (predicted >= 0.10)
			{
				++levelsWithDistribution;
				// A real distribution, not a single value: the property the
				// undithered error does not have.
				QVERIFY2(variance(ditheredError) > 0.0,
					qPrintable(QStringLiteral("at u = %1 the dithered error is still a single "
						"value").arg(u)));
				const double error = std::fabs(observed - predicted);
				widestFlipError = std::max(widestFlipError, error);
				evidence("flip rate: predicted u^2/2 vs observed", predicted, observed);
				QVERIFY2(error < 0.05,
					qPrintable(QStringLiteral("at u = %1 the dither flipped the quantiser %2 of "
						"the time; TPDF predicts u^2/2 = %3")
						.arg(u).arg(observed).arg(predicted)));
			}
			else
			{
				// Below the prediction threshold the honest statement is the
				// weak one: near a step boundary a +/-1 LSB dither is too small
				// to reach the next decision, so the error stays deterministic.
				// Asserted as a bound, not smoothed over.
				QVERIFY2(observed <= predicted + 0.02,
					qPrintable(QStringLiteral("at u = %1 the flip rate %2 exceeded the "
						"predicted %3").arg(u).arg(observed).arg(predicted)));
			}
		}

		const double plainSpread = plainMax - plainMin;
		evidence("undithered error sweep across one LSB / LSB", plainSpread / lsb);
		evidence("levels with a real dithered distribution",
			static_cast<double>(levelsWithDistribution));
		evidence("worst flip-rate error vs u^2/2", widestFlipError);

		// The undithered error is a function of the signal over a full LSB of
		// it - which is the distortion a listener hears as granulation.
		QVERIFY2(plainSpread > 0.9 * static_cast<double>(lsb),
			qPrintable(QStringLiteral("the undithered error only sweeps %1 LSB across a full "
				"step").arg(plainSpread / lsb)));
		// ... and the dither gives it a distribution at most levels, not at none.
		QVERIFY2(levelsWithDistribution >= levels / 2,
			"the dither left the quantiser deterministic at most levels");
	}

	// -----------------------------------------------------------------------
	// 3. Determinism - the property that keeps a dithered render reproducible.
	// -----------------------------------------------------------------------

	void theDitherIsDeterministicAndSeedableIn()
	{
		const float lsb = ExportDither::lsbForBitDepth(16);
		ExportDither first;
		ExportDither second;
		ExportDither other(ExportDither::DefaultSeed + 1);

		std::vector<float> a(1000);
		std::vector<float> c(1000);
		for (std::size_t i = 0; i < 1000; ++i)
		{
			a[i] = first.nextOffset(lsb);
			QCOMPARE(second.nextOffset(lsb), a[i]);
			c[i] = other.nextOffset(lsb);
		}
		bool anyDifferent = false;
		for (std::size_t i = 0; i < 1000; ++i)
		{
			if (a[i] != c[i]) { anyDifferent = true; break; }
		}
		QVERIFY2(anyDifferent, "a different seed must give a different sequence");

		// reseed() is an exact restart, which is what makes a re-render of the
		// same project byte-identical.
		ExportDither again;
		again.reseed();
		for (std::size_t i = 0; i < 1000; ++i)
		{
			QCOMPARE(again.nextOffset(lsb), a[i]);
		}

		// And ditherFrames() applies exactly the offsets nextOffset() draws,
		// one per channel in turn.
		std::vector<SampleFrame> frames(64, SampleFrame(0.0f, 0.0f));
		ExportDither expected;
		ExportDither applied;
		applied.ditherFrames(frames.data(), frames.size(), 16);
		for (std::size_t i = 0; i < frames.size(); ++i)
		{
			QCOMPARE(frames[i][0], expected.nextOffset(lsb));
			QCOMPARE(frames[i][1], expected.nextOffset(lsb));
		}
	}

	void aFloatFormatIsNotDithered()
	{
		// 32-bit float has no quantisation step. Dithering it would only add
		// noise to a lossless format, so the API refuses by returning 0 for the
		// step - and the operations then do nothing at all.
		std::vector<SampleFrame> frames(32, SampleFrame(0.25f, -0.25f));
		const std::vector<SampleFrame> before = frames;
		ExportDither dither;
		dither.ditherFrames(frames.data(), frames.size(), 32);
		for (std::size_t i = 0; i < frames.size(); ++i)
		{
			QCOMPARE(frames[i][0], before[i][0]);
			QCOMPARE(frames[i][1], before[i][1]);
		}
	}

	// -----------------------------------------------------------------------
	// 4. OFF BY DEFAULT - as a value.
	// -----------------------------------------------------------------------

	void theDefaultsAreOffAndLinear()
	{
		ExportRenderSettings::reset();
		QVERIFY2(!ExportRenderSettings::dither(), "the process-wide default must be dither OFF");
		QCOMPARE(ExportRenderSettings::srcQuality(), SrcQuality::Linear);

		const OutputSettings settings(44100, 160, OutputSettings::BitDepth::Depth16Bit,
			OutputSettings::StereoMode::Stereo);
		QVERIFY2(!settings.dither(),
			"OutputSettings must default to dither OFF: an always-on dither breaks the "
			"byte-identical render claim");
		QCOMPARE(settings.srcQuality(), SrcQuality::Linear);
		QCOMPARE(ExportDither::DefaultSeed, 0x5A17E2D0D1A6E001ULL);
	}
};

QTEST_GUILESS_MAIN(ExportDitherTest)
#include "ExportDitherTest.moc"
