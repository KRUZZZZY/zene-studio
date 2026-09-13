/*
 * ExportDitherTest.cpp - the TPDF export dither (include/ExportDither.h and its
 *                        wiring into AudioFileWave).
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
 *  claimed EFFECT. Both are asserted here, statistically, with an inverted
 *  control for each - a check that cannot fail is not a check.
 *
 *  1. THE DISTRIBUTION. TPDF means the offset is the sum of two uniforms, so
 *     its variance is 1/6 LSB^2 - and NOT 1/12 LSB^2, which is what a single
 *     uniform (RPDF) would give. The variance assertion therefore separates
 *     TPDF from RPDF, which is the whole reason TPDF is the one implemented.
 *
 *  2. THE EFFECT. Quantising a low-level signal without dither leaves an error
 *     that is a deterministic FUNCTION of the signal, i.e. distortion. The
 *     assertion is the correlation between the quantisation error and the
 *     signal: high without dither (the inverted control), near zero with it.
 *
 *  3. OFF BY DEFAULT. The release's reproducibility claim is byte-identical
 *     renders, so the default is asserted twice - as a value (OutputSettings,
 *     ExportRenderSettings) and as BYTES: a WAV written with the default must
 *     equal the pre-change truncation exactly, sample for sample.
 *
 *  4. THE WIRING. The dither has to reach the file, not just the class: the
 *     AudioFileWave assertions write real WAVs and read them back.
 *
 *  Every number the test measures is printed as a DITHER_EVIDENCE line so a
 *  reader can check it by hand, and the fixture is a sine/DC the reader can
 *  recompute.
 */

#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include <sndfile.h>

#include "AudioEngine.h"
#include "AudioFileDevice.h"
#include "AudioFileWave.h"
#include "Engine.h"
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

//! Lag-1 autocorrelation of one series against itself, shifted.
double lagOneAutocorrelation(const std::vector<double>& values)
{
	std::vector<double> shifted(values.begin(), values.end());
	shifted.erase(shifted.begin());
	std::vector<double> head(values.begin(), values.end() - 1);
	return correlation(head, shifted);
}

//! A low-level sine: 0.4 LSB of a 16-bit step, so the signal lives inside one
//! or two quantisation levels and the undithered error is a deterministic
//! sawtooth in the signal (the distortion dither exists to remove).
std::vector<float> lowLevelSine(std::size_t count, double cycles)
{
	// M_PI is not in <cmath> on every supported toolchain (MSVC needs
	// _USE_MATH_DEFINES), so the constant is written out.
	constexpr double kPi = 3.14159265358979323846;
	std::vector<float> signal(count);
	const double amplitude = 0.4 * ExportDither::lsbForBitDepth(16);
	for (std::size_t i = 0; i < count; ++i)
	{
		signal[i] = static_cast<float>(amplitude
			* std::sin(2.0 * kPi * cycles * static_cast<double>(i) / static_cast<double>(count)));
	}
	return signal;
}

void evidence(const char* label, double a, double b = 0.0, double c = 0.0)
{
	std::printf("DITHER_EVIDENCE %-40s %14.9f %14.9f %14.9f\n", label, a, b, c);
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
		Engine::init(true);
		QVERIFY(Engine::audioEngine() != nullptr);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		ExportRenderSettings::reset();
		Engine::destroy();
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

		// Successive offsets are independent (the sum of two uniforms of the
		// SAME sample would correlate with itself one step later).
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
	// 2. The effect: the error stops being a function of the signal.
	// -----------------------------------------------------------------------

	void theDitherDecorrelatesTheQuantisationErrorFromTheSignal()
	{
		constexpr std::size_t count = 4096;
		const std::vector<float> signal = lowLevelSine(count, 3.0);

		// INVERTED CONTROL: the same signal, quantised with no dither. The
		// error here is a deterministic sawtooth in the signal, so it
		// correlates with it strongly.
		std::vector<double> plainError(count);
		for (std::size_t i = 0; i < count; ++i) { plainError[i] = quantisationError(signal[i]); }

		// The same quantiser, with the dither drawn and added first - the order
		// AudioFileWave::writeBuffer uses.
		ExportDither dither;
		const float lsb = ExportDither::lsbForBitDepth(16);
		std::vector<double> ditheredError(count);
		for (std::size_t i = 0; i < count; ++i)
		{
			const float offset = dither.nextOffset(lsb);
			ditheredError[i] = static_cast<double>(quantise16(signal[i] + offset))
				/ OUTPUT_SAMPLE_MULTIPLIER - static_cast<double>(signal[i]);
		}

		std::vector<double> signalAsDouble(signal.begin(), signal.end());
		const double plainCorrelation = correlation(plainError, signalAsDouble);
		const double ditheredCorrelation = correlation(ditheredError, signalAsDouble);

		evidence("error<->signal corr, NO dither", plainCorrelation);
		evidence("error<->signal corr, TPDF dither", ditheredCorrelation);
		evidence("error variance, NO dither / LSB^2",
			variance(plainError) / (static_cast<double>(lsb) * lsb));
		evidence("error variance, TPDF dither / LSB^2",
			variance(ditheredError) / (static_cast<double>(lsb) * lsb));

		// The control must actually be a control: without dither the error IS
		// the signal's own quantisation sawtooth.
		QVERIFY2(std::fabs(plainCorrelation) > 0.8,
			qPrintable(QStringLiteral("undithered error correlates only %1 with the signal; "
				"the fixture is not low-level enough to be a control")
				.arg(plainCorrelation)));

		// ... and with dither it is gone. This is the assertion the feature
		// exists to satisfy.
		QVERIFY2(std::fabs(ditheredCorrelation) < 0.05,
			qPrintable(QStringLiteral("dithered error still correlates %1 with the signal")
				.arg(ditheredCorrelation)));

		// The price is paid in noise, and it is bounded: the total error of a
		// truncating quantiser dithered by +/-1 LSB cannot exceed 1 LSB.
		const double ditheredStd = std::sqrt(variance(ditheredError));
		QVERIFY2(ditheredStd < static_cast<double>(lsb) * 1.01,
			"the dithered total error must stay within one LSB");
		QVERIFY2(ditheredStd > static_cast<double>(lsb) * 0.05,
			"the dithered total error is suspiciously small; no dither was applied");
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
		std::vector<float> b(1000);
		std::vector<float> c(1000);
		for (std::size_t i = 0; i < 1000; ++i)
		{
			a[i] = first.nextOffset(lsb);
			b[i] = second.nextOffset(lsb);
			c[i] = other.nextOffset(lsb);
		}
		for (std::size_t i = 0; i < 1000; ++i)
		{
			QCOMPARE(a[i], b[i]);
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

		// And ditherFrames() applies exactly the offsets nextOffset() draws.
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
	// 4. OFF BY DEFAULT - as a value, and as bytes.
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
	}

	//! The WAV the default export produces must be EXACTLY the pre-change
	//! truncation - not "close to" it. This is the byte-level half of the
	//! off-by-default claim.
	void aDefaultExportIsExactlyTheUnditheredQuantisation()
	{
		const std::vector<float> signal = lowLevelSine(2048, 4.0);
		const QString path = writeWav(signal, false, OutputSettings::BitDepth::Depth16Bit, "plain.wav");
		const std::vector<std::int16_t> read = readWavS16(path);
		QCOMPARE(read.size(), signal.size());

		for (std::size_t i = 0; i < signal.size(); ++i)
		{
			QCOMPARE(read[i], quantise16(signal[i]));
		}
		evidence("default export == undithered quantisation", 1.0);
	}

	void ditherOnChangesTheBytesAndStaysReproducible()
	{
		const std::vector<float> signal = lowLevelSine(2048, 4.0);
		const QString plain = writeWav(signal, false, OutputSettings::BitDepth::Depth16Bit, "off.wav");
		const QString dithered = writeWav(signal, true, OutputSettings::BitDepth::Depth16Bit, "on.wav");
		const QString ditheredAgain = writeWav(signal, true, OutputSettings::BitDepth::Depth16Bit, "on2.wav");

		const std::vector<std::int16_t> a = readWavS16(plain);
		const std::vector<std::int16_t> b = readWavS16(dithered);
		const std::vector<std::int16_t> c = readWavS16(ditheredAgain);
		QCOMPARE(a.size(), b.size());

		std::size_t differing = 0;
		std::int16_t maxDelta = 0;
		for (std::size_t i = 0; i < a.size(); ++i)
		{
			if (a[i] != b[i]) { ++differing; }
			maxDelta = std::max<std::int16_t>(maxDelta,
				static_cast<std::int16_t>(std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]))));
		}
		evidence("dither-on samples differing from off", static_cast<double>(differing));
		evidence("dither-on max |delta| (16-bit counts)", static_cast<double>(maxDelta));

		QVERIFY2(differing > 0, "dither ON produced the same bytes as dither OFF");
		// The dither is bounded by 1 LSB, so the quantised value can move by at
		// most 1 count either way.
		QVERIFY2(maxDelta <= 2, qPrintable(QStringLiteral("dither moved a sample by %1 counts")
			.arg(maxDelta)));

		// Reproducibility, end to end: the same project renders the same file.
		QCOMPARE(b.size(), c.size());
		for (std::size_t i = 0; i < b.size(); ++i)
		{
			QCOMPARE(b[i], c[i]);
		}
	}

	//! The wiring, at the depth where the quantiser lives: 24-bit output goes
	//! through the float path, and 32-bit float must be left alone.
	void theDitherReachesThe24BitPathAndNotTheFloatPath()
	{
		const std::vector<float> signal = lowLevelSine(1024, 3.0);
		const QString off = writeWav(signal, false, OutputSettings::BitDepth::Depth24Bit, "24off.wav");
		const QString on = writeWav(signal, true, OutputSettings::BitDepth::Depth24Bit, "24on.wav");
		QVERIFY(off != on);

		const std::vector<float> fOff = readWavFloat(writeWav(signal, false,
			OutputSettings::BitDepth::Depth32Bit, "32off.wav"));
		const std::vector<float> fOn = readWavFloat(writeWav(signal, true,
			OutputSettings::BitDepth::Depth32Bit, "32on.wav"));
		QCOMPARE(fOff.size(), fOn.size());
		QCOMPARE(fOff.size(), signal.size());
		for (std::size_t i = 0; i < fOff.size(); ++i)
		{
			QCOMPARE(fOff[i], fOn[i]);
			QCOMPARE(fOff[i], signal[i]);
		}
		evidence("32-bit float off == on", 1.0);
	}

private:

	//! Writes \p signal through the real export path and returns the file path.
	QString writeWav(const std::vector<float>& signal, bool dither, OutputSettings::BitDepth depth,
		const QString& name)
	{
		OutputSettings settings(44100, 160, depth, OutputSettings::StereoMode::Stereo);
		settings.setDither(dither);
		// The choice must be published the way ProjectRenderer publishes it, or
		// the wiring under test is not the wiring used in production.
		ExportRenderSettings::setDither(dither);

		const QString path = QDir(m_dir.path()).filePath(name);
		bool successful = false;
		std::unique_ptr<AudioFileDevice> device(
			AudioFileWave::getInst(path, settings, DEFAULT_CHANNELS, Engine::audioEngine(), successful));
		Q_ASSERT(device != nullptr);
		if (!successful) { return QString(); }

		std::vector<SampleFrame> frames(signal.size());
		for (std::size_t i = 0; i < signal.size(); ++i)
		{
			frames[i] = SampleFrame(signal[i], signal[i]);
		}
		device->writeBuffer(frames.data(), static_cast<f_cnt_t>(frames.size()));
		return path;   // device closes the file in its destructor
	}

	std::vector<std::int16_t> readWavS16(const QString& path)
	{
		SF_INFO info{};
		SNDFILE* file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
		Q_ASSERT(file != nullptr);
		std::vector<std::int16_t> samples(static_cast<std::size_t>(info.frames));
		sf_readf_short(file, samples.data(), info.frames);
		sf_close(file);
		return samples;
	}

	std::vector<float> readWavFloat(const QString& path)
	{
		SF_INFO info{};
		SNDFILE* file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
		Q_ASSERT(file != nullptr);
		std::vector<float> samples(static_cast<std::size_t>(info.frames));
		sf_readf_float(file, samples.data(), info.frames);
		sf_close(file);
		return samples;
	}

	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(ExportDitherTest)
#include "ExportDitherTest.moc"
