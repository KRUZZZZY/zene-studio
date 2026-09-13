/*
 * AudioResamplerRatioTest.cpp - the converter ratio convention, and the export
 *                               SRC quality that selects the converter.
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

/*! Why this file exists.
 *
 *  `docs/WARP.md` §3.1, `src/core/SamplePlayHandle.cpp` and a comment in
 *  `tests/src/tracks/SampleClipWarpTest.cpp` all used to report that the
 *  converter's ratio convention is INVERTED - that `AudioResampler` /
 *  libsamplerate `SRC_LINEAR` reads `ratio` as input/output while the engine
 *  documents it as output/input - and WARP.md concluded from that a live defect
 *  on the pre-existing mismatch-rate path (a 48 kHz source in a 44.1 kHz project
 *  playing ~8.8 % fast and sharp, because it "reads 0.919 as input frames per
 *  output frame").
 *
 *  The premise is false, and this file is the measurement that settles it, on
 *  the library this build actually links:
 *
 *    src_ratio = 2.00  ->  4096 input frames  ->  8192 output frames
 *    src_ratio = 1.00  ->  4096 input frames  ->  4096 output frames
 *    src_ratio = 0.50  ->  4096 input frames  ->  2048 output frames
 *
 *  libsamplerate's `SRC_DATA::src_ratio` is OUTPUT frames per INPUT frame -
 *  the same convention the engine documents - so the engine's
 *  `outputSampleRate / sampleRate` ratio is correct, the mismatch-rate path
 *  consumes the pitch-preserving 48000/44100 source frames per output frame,
 *  and "fixing" the phantom inversion (by handing libsamplerate `1 / ratio`)
 *  would have introduced the very defect the note claimed to have found. That
 *  is why the convention is pinned by a test rather than by prose: an inverted
 *  `AudioResampler::process()` now fails here, by name, instead of silently
 *  changing the pitch of every mismatched-rate project.
 *
 *  The second half of the file proves the EXPORT SRC QUALITY reaches the
 *  resampler: not that a value was stored, but that a different converter ran,
 *  measured on the samples that come out of `Sample::play`.
 */

#include <QtTest>

#include <cmath>
#include <cstdio>
#include <vector>

#include "AudioBufferView.h"
#include "AudioEngine.h"
#include "AudioResampler.h"
#include "Engine.h"
#include "ExportRenderSettings.h"
#include "Sample.h"
#include "SampleFrame.h"
#include "SrcQuality.h"

using namespace lmms;

namespace
{

constexpr float kTwoPi = 6.28318530717958647692f;

std::vector<SampleFrame> sineFrames(std::size_t count, double rate, double frequency)
{
	std::vector<SampleFrame> frames(count);
	for (std::size_t i = 0; i < count; ++i)
	{
		const auto value = static_cast<float>(0.5
			* std::sin(kTwoPi * frequency * static_cast<double>(i) / rate));
		frames[i] = SampleFrame(value, value);
	}
	return frames;
}

//! One conversion in the shape Sample::play drives it: an output request of
//! \p outputFrames answered from \p inputFrames of source.
AudioResampler::Result convert(AudioResampler& resampler, std::size_t inputFrames,
	std::size_t outputFrames)
{
	std::vector<SampleFrame> input(inputFrames, SampleFrame(0.0f, 0.0f));
	std::vector<SampleFrame> output(outputFrames, SampleFrame(0.0f, 0.0f));
	return resampler.process(
		{&input[0][0], 2, input.size()},
		{&output[0][0], 2, output.size()});
}

void evidence(const char* label, double a, double b = 0.0)
{
	std::printf("SRC_EVIDENCE %-52s %14.9f %14.9f\n", label, a, b);
	std::fflush(stdout);
}

} // namespace

class AudioResamplerRatioTest : public QObject
{
	Q_OBJECT

private slots:

	void initTestCase()
	{
		ExportRenderSettings::reset();
		Engine::init(true);
		QVERIFY(Engine::audioEngine() != nullptr);
	}

	void cleanupTestCase()
	{
		ExportRenderSettings::reset();
		Engine::destroy();
	}

	// -----------------------------------------------------------------------
	// 1. THE CONVENTION, measured at the boundary where it can be inverted.
	// -----------------------------------------------------------------------

	void theConverterRatioIsOutputFramesPerInputFrame()
	{
		AudioResampler resampler(AudioResampler::Mode::Linear, 2);

		resampler.setRatio(2.0);
		const auto doubled = convert(resampler, 4096, 8192);
		resampler.reset();
		resampler.setRatio(1.0);
		const auto unity = convert(resampler, 4096, 4096);
		resampler.reset();
		resampler.setRatio(0.5);
		const auto halved = convert(resampler, 4096, 4096);

		const double doubledRatio = static_cast<double>(doubled.outputFramesGenerated)
			/ static_cast<double>(doubled.inputFramesUsed);
		const double unityRatio = static_cast<double>(unity.outputFramesGenerated)
			/ static_cast<double>(unity.inputFramesUsed);
		const double halvedRatio = static_cast<double>(halved.outputFramesGenerated)
			/ static_cast<double>(halved.inputFramesUsed);
		evidence("ratio 2.0 -> output/input", doubledRatio);
		evidence("ratio 1.0 -> output/input", unityRatio);
		evidence("ratio 0.5 -> output/input", halvedRatio);

		// THE ASSERTION THAT FAILS IF process() IS INVERTED. Under an
		// input/output reading these three would be 0.5, 1.0 and 2.0.
		QVERIFY2(std::fabs(doubledRatio - 2.0) < 0.02,
			qPrintable(QStringLiteral("src_ratio 2.0 gave %1 output frames per input frame; "
				"the converter is inverted relative to AudioResampler::setRatio's contract")
				.arg(doubledRatio)));
		QVERIFY2(std::fabs(unityRatio - 1.0) < 0.02, "src_ratio 1.0 must be 1:1");
		QVERIFY2(std::fabs(halvedRatio - 0.5) < 0.02,
			qPrintable(QStringLiteral("src_ratio 0.5 gave %1 output frames per input frame")
				.arg(halvedRatio)));
	}

	//! The pre-existing path: a 48 kHz source in a 44.1 kHz project. The
	//! "inversion" note called this an ~8.8 % fast-and-sharp defect; it is the
	//! pitch-preserving rate.
	void theMismatchRatePathConsumesThePitchPreservingFrameCount()
	{
		AudioResampler resampler(AudioResampler::Mode::Linear, 2);
		resampler.setRatio(48000, 44100);
		QCOMPARE(resampler.ratio(), 44100.0 / 48000.0);

		constexpr std::size_t outputRequest = 4096;
		const auto result = convert(resampler, 65536, outputRequest);
		QVERIFY(result.outputFramesGenerated > 0);
		QVERIFY(result.inputFramesUsed > 0);

		const double sourceFramesPerOutput = static_cast<double>(result.inputFramesUsed)
			/ static_cast<double>(result.outputFramesGenerated);
		const double expected = 48000.0 / 44100.0;   // 1.088435...
		evidence("48k source in 44.1k project: source frames/output frame", sourceFramesPerOutput);
		evidence("expected (48000/44100)", expected);
		evidence("the inverted reading would give", 44100.0 / 48000.0);

		QVERIFY2(std::fabs(sourceFramesPerOutput - expected) < 0.02,
			qPrintable(QStringLiteral("a 48 kHz source consumed %1 source frames per output "
				"frame; pitch-preserving is %2").arg(sourceFramesPerOutput).arg(expected)));

		// And explicitly NOT the number the old note said it read.
		QVERIFY2(std::fabs(sourceFramesPerOutput - (44100.0 / 48000.0)) > 0.1,
			"the mismatch-rate path is consuming the INVERTED ratio");
	}

	// -----------------------------------------------------------------------
	// 2. The SRC quality selects the converter.
	// -----------------------------------------------------------------------

	void theQualitySelectsTheConverter()
	{
		QCOMPARE(AudioResampler::modeForSrcQuality(SrcQuality::Linear),
			AudioResampler::Mode::Linear);
		QCOMPARE(AudioResampler::modeForSrcQuality(SrcQuality::SincFastest),
			AudioResampler::Mode::SincFastest);
		QCOMPARE(AudioResampler::modeForSrcQuality(SrcQuality::SincMedium),
			AudioResampler::Mode::SincMedium);
		QCOMPARE(AudioResampler::modeForSrcQuality(SrcQuality::SincBest),
			AudioResampler::Mode::SincBest);

		// The default is the converter the engine has always used, which is
		// what keeps a default render byte-for-byte what it was.
		QVERIFY(ExportRenderSettings::srcQuality() == SrcQuality::Linear);
		QCOMPARE(AudioResampler::modeForSrcQuality(ExportRenderSettings::srcQuality()),
			AudioResampler::Mode::Linear);

		// setMode really swaps the converter (and is a no-op when it matches).
		AudioResampler resampler(AudioResampler::Mode::Linear, 2);
		QCOMPARE(resampler.mode(), AudioResampler::Mode::Linear);
		resampler.setMode(AudioResampler::Mode::SincBest);
		QCOMPARE(resampler.mode(), AudioResampler::Mode::SincBest);
		resampler.setMode(AudioResampler::Mode::SincBest);
		QCOMPARE(resampler.mode(), AudioResampler::Mode::SincBest);
	}

	//! Not "the setting was stored" but "a different converter ran": the two
	//! converters must disagree on the samples they produce. Sinc filtering is
	//! not linear interpolation, so a signal with content that linear
	//! interpolation gets wrong is the fixture.
	void theConvertersProduceDifferentSamples()
	{
		constexpr std::size_t frames = 4096;
		const std::vector<SampleFrame> input = sineFrames(frames, 8000.0, 3000.0);

		const auto run = [&input](AudioResampler::Mode mode) {
			AudioResampler resampler(mode, 2);
			resampler.setRatio(2.0);
			std::vector<SampleFrame> output(frames * 2, SampleFrame(0.0f, 0.0f));
			const auto result = resampler.process({&input[0][0], 2, input.size()},
				{&output[0][0], 2, output.size()});
			Q_UNUSED(result);
			return output;
		};

		const std::vector<SampleFrame> linear = run(AudioResampler::Mode::Linear);
		const std::vector<SampleFrame> best = run(AudioResampler::Mode::SincBest);

		double maxDifference = 0.0;
		double energy = 0.0;
		for (std::size_t i = 0; i < linear.size(); ++i)
		{
			maxDifference = std::max(maxDifference,
				std::fabs(static_cast<double>(linear[i][0]) - static_cast<double>(best[i][0])));
			energy += static_cast<double>(best[i][0]) * static_cast<double>(best[i][0]);
		}
		evidence("SincBest vs Linear: max |difference|", maxDifference);
		evidence("SincBest output energy", energy);

		QVERIFY2(energy > 0.0, "the fixture produced no output at all");
		QVERIFY2(maxDifference > 1e-4,
			qPrintable(QStringLiteral("Linear and SincBest produced the same samples "
				"(max difference %1): the quality did not reach the converter")
				.arg(maxDifference)));
	}

	// -----------------------------------------------------------------------
	// 3. The export setting reaches the resampler END TO END, through the
	//    function the render path calls: Sample::play.
	// -----------------------------------------------------------------------

	void theExportQualityReachesTheResamplerThroughSamplePlay()
	{
		AudioEngine* engine = Engine::audioEngine();
		const auto engineRate = static_cast<int>(engine->outputSampleRate());
		QVERIFY(engineRate > 0);
		// Half the engine's rate, so the sample MUST be resampled (ratio 2.0).
		const int sourceRate = engineRate / 2;
		const std::vector<SampleFrame> source = sineFrames(8192, sourceRate, 2000.0);
		Sample sample(source.data(), source.size(), sourceRate);
		QCOMPARE(sample.sampleRate(), sourceRate);

		const auto render = [&sample](SrcQuality quality) {
			ExportRenderSettings::setSrcQuality(quality);
			Sample::PlaybackState state;
			state.setFrameIndex(0);
			std::vector<SampleFrame> out(2048, SampleFrame(0.0f, 0.0f));
			sample.play(out.data(), &state, out.size(), Sample::Loop::Off, 1.0);
			return out;
		};

		const std::vector<SampleFrame> linear = render(SrcQuality::Linear);
		const std::vector<SampleFrame> best = render(SrcQuality::SincBest);
		// Put the selection back: a leaked quality would change every later
		// render in this process (and this test would then be measuring its own
		// side effect).
		ExportRenderSettings::reset();
		QCOMPARE(ExportRenderSettings::srcQuality(), SrcQuality::Linear);

		double maxDifference = 0.0;
		double energy = 0.0;
		for (std::size_t i = 0; i < linear.size(); ++i)
		{
			maxDifference = std::max(maxDifference,
				std::fabs(static_cast<double>(linear[i][0]) - static_cast<double>(best[i][0])));
			energy += static_cast<double>(best[i][0]) * static_cast<double>(best[i][0]);
		}
		evidence("Sample::play SincBest vs Linear: max |difference|", maxDifference);
		evidence("Sample::play SincBest output energy", energy);

		QVERIFY2(energy > 0.0, "Sample::play produced no output: the fixture is broken");
		QVERIFY2(maxDifference > 1e-5,
			qPrintable(QStringLiteral("export.set_src_quality had no effect on Sample::play "
				"(max difference %1)").arg(maxDifference)));
	}

	//! The other half of the contract: the DEFAULT quality is the one the
	//! engine always used, so a render that asks for nothing is unchanged.
	void theDefaultQualityIsTheHistoricalConverter()
	{
		AudioEngine* engine = Engine::audioEngine();
		const int sourceRate = static_cast<int>(engine->outputSampleRate()) / 2;
		const std::vector<SampleFrame> source = sineFrames(4096, sourceRate, 2000.0);
		Sample sample(source.data(), source.size(), sourceRate);

		const auto render = [&sample]() {
			Sample::PlaybackState state;
			state.setFrameIndex(0);
			std::vector<SampleFrame> out(1024, SampleFrame(0.0f, 0.0f));
			sample.play(out.data(), &state, out.size(), Sample::Loop::Off, 1.0);
			return out;
		};

		// With the process-wide defaults in force.
		ExportRenderSettings::reset();
		const std::vector<SampleFrame> byDefault = render();

		// With the quality named explicitly as the default.
		ExportRenderSettings::setSrcQuality(SrcQuality::Linear);
		const std::vector<SampleFrame> byName = render();
		ExportRenderSettings::reset();

		for (std::size_t i = 0; i < byDefault.size(); ++i)
		{
			QCOMPARE(byDefault[i][0], byName[i][0]);
			QCOMPARE(byDefault[i][1], byName[i][1]);
		}
		evidence("default quality is exactly Linear", 1.0);
	}
};

QTEST_GUILESS_MAIN(AudioResamplerRatioTest)
#include "AudioResamplerRatioTest.moc"
