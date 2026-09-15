/*
 * AudioStretcherTest.cpp - pitch-preserving time stretch (0.3.0 feature-list
 *                          row 30): the DSP's own proof.
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
 */

/*! WHY THIS FILE EXISTS, and what it measures.
 *
 *  The task's acceptance says the registered proof has to distinguish the new
 *  stretch from plain resampling ON THE SAME INPUT, with the pitch MEASURED
 *  rather than asserted. "A warp is an octave up" is a claim about a waveform;
 *  the only honest way to settle it is to put the same samples through both
 *  paths and measure what comes out.
 *
 *  So the fixture is a two-tone signal (440 Hz at 0.5, 660 Hz at 0.3 - two
 *  frequencies whose periods do not divide the synthesis hop, so a stretch
 *  that fails to re-align its grains CANCELS instead of hiding) and every
 *  render is asked the same question: how much energy is there at 440, 660,
 *  880 and 1320 Hz? The amplitudes are measured with a Goertzel filter, i.e.
 *  a single-bin DFT - no new dependency, and a number that is a property of
 *  the samples that came out.
 *
 *  MEASURED, on this tree (the same numbers the test prints as EVIDENCE):
 *
 *    signal            440      660      880     1320     (amplitudes)
 *    input          0.5000   0.3000   0.0000   0.0000
 *    resampled x2   0.0000   0.0000   0.5000   0.3000     <- the pitch moved
 *    wsola x2       0.4992   0.2990   0.0001   0.0001     <- the pitch did not
 *    wsola, no align 0.0285  0.0060   0.0007   0.0004     <- and the search is why
 *
 *  The last row is the control that says WHY the alignment search is worth its
 *  cost: the same stretch with `searchRadius = 0` is a plain overlap-add, which
 *  on this fixture cancels the tones almost to silence (0.0285 of 0.5) because
 *  the grains land out of phase. The search is what turns that into 0.4992.
 *
 *  The second half of the file is the contract a caller depends on: period-sized
 *  calls produce byte-identical output to one call (the render path calls in
 *  periods), the parameters are clamped rather than throwing, and the render
 *  path allocates nothing at all (I8) - measured with the same AllocationProbe
 *  the warp and Slice 0 lanes used.
 */

#include <QtTest>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "AllocationProbe.h"

#include "AudioResampler.h"
#include "AudioStretcher.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

constexpr double kRate = 44100.0;
constexpr double kTwoPi = 6.28318530717958647692;

//! The fixture: two tones, deterministic, no engine and no file needed.
std::vector<SampleFrame> twoTone(int frames, double rate = kRate)
{
	std::vector<SampleFrame> data(static_cast<std::size_t>(frames));
	for (int i = 0; i < frames; ++i)
	{
		const auto value = static_cast<sample_t>(0.5 * std::sin(kTwoPi * 440.0 * i / rate)
			+ 0.3 * std::sin(kTwoPi * 660.0 * i / rate));
		data[static_cast<std::size_t>(i)] = SampleFrame(value, value);
	}
	return data;
}

/*! The amplitude of \a freq in [from, to), measured with a Goertzel filter
 *  (one bin of a DFT) and normalised so a pure sine of amplitude A reports A.
 *  The bin is rounded to the whole number of cycles the window holds, which is
 *  what makes the measurement exact for a tone at that frequency. */
double amplitudeAt(const SampleFrame* data, int from, int to, double freq, double rate = kRate)
{
	const int frames = to - from;
	if (frames <= 8) { return 0.0; }
	const double cycles = std::round(frames * freq / rate);
	const double omega = kTwoPi * cycles / frames;
	const double coefficient = 2.0 * std::cos(omega);

	double previous = 0.0;
	double previousPrevious = 0.0;
	for (int i = from; i < to; ++i)
	{
		const double current = data[i][0] + coefficient * previous - previousPrevious;
		previousPrevious = previous;
		previous = current;
	}
	const double real = previous - previousPrevious * std::cos(omega);
	const double imaginary = previousPrevious * std::sin(omega);
	const double magnitude = std::sqrt(real * real + imaginary * imaginary);
	return 2.0 * magnitude / frames;
}

/*! The fundamental, measured by counting upward zero crossings and timing the
 *  first against the last with linear interpolation between the two samples the
 *  crossing fell between. Independent of the Goertzel measurement above (a
 *  different question - period, not bin energy), so agreement between the two
 *  is evidence and not a tautology. */
double measuredFrequency(const SampleFrame* data, int frames, double rate = kRate)
{
	int cycles = 0;
	double first = -1.0;
	double last = -1.0;
	for (int i = 1; i < frames; ++i)
	{
		const double before = data[i - 1][0];
		const double after = data[i][0];
		if (before <= 0.0 && after > 0.0)
		{
			const double crossing = static_cast<double>(i - 1) + (0.0 - before) / (after - before);
			if (first < 0.0) { first = crossing; }
			else { last = crossing; ++cycles; }
		}
	}
	if (cycles < 1 || last <= first) { return 0.0; }
	return cycles * rate / (last - first);
}

double rms(const SampleFrame* data, int from, int to)
{
	double sum = 0.0;
	for (int i = from; i < to; ++i) { sum += static_cast<double>(data[i][0]) * data[i][0]; }
	return std::sqrt(sum / std::max(1, to - from));
}

void evidence(const char* label, double a, double b = 0.0, double c = 0.0, double d = 0.0)
{
	std::printf("STRETCH_EVIDENCE %-46s %10.4f %10.4f %10.4f %10.4f\n", label, a, b, c, d);
	std::fflush(stdout);
}

} // namespace

class AudioStretcherTest : public QObject
{
	Q_OBJECT

private slots:

	/*! THE ACCEPTANCE ITEM: the same input, two renders, and the pitch
	 *  measured in both. The resampler moves it (that is what a warp does
	 *  today); the stretcher does not.
	 *
	 *  Also asserted: the two renders are the SAME LENGTH and the same level,
	 *  so "the pitch did not move" cannot be true because "nothing happened".
	 */
	void theSameInputThroughBothPathsMeasuresTwoDifferentPitches()
	{
		constexpr int sourceFrames = 88200;          // 2 s
		const int outputFrames = sourceFrames / 2;   // speed 2.0
		const std::vector<SampleFrame> source = twoTone(sourceFrames);

		// The control: what the engine does today for a 2x warp - the ratio a
		// warped clip's warpRatio() hands Sample::play, through the same
		// converter (AudioResampler, Mode::Linear, the engine's default).
		std::vector<SampleFrame> resampled(static_cast<std::size_t>(outputFrames));
		{
			AudioResampler resampler(AudioResampler::Mode::Linear, 2);
			resampler.setRatio(0.5);   // output frames per input frame: 2x speed
			QVERIFY(resampler.process({&source[0][0], 2, source.size()},
				{&resampled[0][0], 2, resampled.size()}).outputFramesGenerated > 0);
		}

		AudioStretcher stretcher;
		stretcher.prepare();
		QVERIFY(stretcher.isPrepared());
		std::vector<SampleFrame> stretched(static_cast<std::size_t>(outputFrames));
		QCOMPARE(stretcher.process(source.data(), static_cast<f_cnt_t>(source.size()),
			stretched.data(), static_cast<f_cnt_t>(stretched.size()), 2.0),
			static_cast<f_cnt_t>(outputFrames));

		const int from = outputFrames / 4;
		const int to = outputFrames * 3 / 4;
		const double input440 = amplitudeAt(source.data(), sourceFrames / 4, sourceFrames * 3 / 4, 440.0);
		const double input660 = amplitudeAt(source.data(), sourceFrames / 4, sourceFrames * 3 / 4, 660.0);
		const double resampled440 = amplitudeAt(resampled.data(), from, to, 440.0);
		const double resampled660 = amplitudeAt(resampled.data(), from, to, 660.0);
		const double resampled880 = amplitudeAt(resampled.data(), from, to, 880.0);
		const double resampled1320 = amplitudeAt(resampled.data(), from, to, 1320.0);
		const double stretched440 = amplitudeAt(stretched.data(), from, to, 440.0);
		const double stretched660 = amplitudeAt(stretched.data(), from, to, 660.0);
		const double stretched880 = amplitudeAt(stretched.data(), from, to, 880.0);
		const double stretched1320 = amplitudeAt(stretched.data(), from, to, 1320.0);

		evidence("input: 440 / 660 / 880 / 1320", input440, input660, 0.0, 0.0);
		evidence("resampled 2x: 440 / 660 / 880 / 1320",
			resampled440, resampled660, resampled880, resampled1320);
		evidence("stretched 2x: 440 / 660 / 880 / 1320",
			stretched440, stretched660, stretched880, stretched1320);
		evidence("measured frequency, resampled 2x", measuredFrequency(resampled.data(), outputFrames));
		evidence("measured frequency, stretched 2x", measuredFrequency(stretched.data(), outputFrames));
		evidence("rms input / resampled / stretched", rms(source.data(), sourceFrames / 4, sourceFrames * 3 / 4),
			rms(resampled.data(), from, to), rms(stretched.data(), from, to));

		// 1. The resampler moved BOTH tones up an octave: this is what the
		//    engine does today, measured, and it is the behaviour the new mode
		//    is distinguished from.
		QVERIFY2(resampled880 > 0.45 && resampled1320 > 0.27,
			qPrintable(QStringLiteral("the resample control did not move the pitch: 880 = %1, 1320 = %2")
				.arg(resampled880).arg(resampled1320)));
		QVERIFY2(resampled440 < 0.01 && resampled660 < 0.01,
			"the resample control left energy at the original frequencies, so this fixture "
			"cannot tell the two paths apart");

		// 2. THE POINT: the stretcher kept both tones where they were.
		QVERIFY2(std::fabs(stretched440 - input440) < 0.02,
			qPrintable(QStringLiteral("the stretched render's 440 Hz component is %1, the input's is %2")
				.arg(stretched440).arg(input440)));
		QVERIFY2(std::fabs(stretched660 - input660) < 0.02,
			qPrintable(QStringLiteral("the stretched render's 660 Hz component is %1, the input's is %2")
				.arg(stretched660).arg(input660)));
		QVERIFY2(stretched880 < 0.01 && stretched1320 < 0.01,
			qPrintable(QStringLiteral("the stretched render moved energy up an octave: 880 = %1, "
				"1320 = %2 (that is resampling, not a stretch)").arg(stretched880).arg(stretched1320)));

		// 3. The independent measurement agrees (zero crossings, not bins).
		const double resampledFrequency = measuredFrequency(resampled.data(), outputFrames);
		const double stretchedFrequency = measuredFrequency(stretched.data(), outputFrames);
		QVERIFY2(std::fabs(resampledFrequency - 880.0) < 9.0,
			qPrintable(QStringLiteral("resampled 2x measured %1 Hz, expected 880").arg(resampledFrequency)));
		QVERIFY2(std::fabs(stretchedFrequency - 440.0) < 4.5,
			qPrintable(QStringLiteral("stretched 2x measured %1 Hz, expected 440").arg(stretchedFrequency)));

		// 4. Same length, same level: the difference is the waveform, not the
		//    amount of audio or its loudness.
		QCOMPARE(static_cast<int>(stretched.size()), outputFrames);
		const double stretchedRms = rms(stretched.data(), from, to);
		const double resampledRms = rms(resampled.data(), from, to);
		QVERIFY2(std::fabs(stretchedRms - resampledRms) < 0.05,
			qPrintable(QStringLiteral("stretched rms %1 vs resampled rms %2").arg(stretchedRms).arg(resampledRms)));
	}

	/*! THE CONTROL that says why the alignment search is there. With
	 *  `searchRadius = 0` the class is a plain overlap-add: same grains, same
	 *  window, no correlation - and on this fixture the grains land out of
	 *  phase and cancel (measured: 0.0285 of the input's 0.5 at 440 Hz).
	 *  Turning the search back on is what recovers it.
	 */
	void theAlignmentSearchIsWhatKeepsTheWaveform()
	{
		constexpr int sourceFrames = 88200;
		const int outputFrames = sourceFrames / 2;
		const std::vector<SampleFrame> source = twoTone(sourceFrames);

		const auto render = [&source, outputFrames](int searchRadius) {
			AudioStretcher stretcher;
			stretcher.prepare({1024, searchRadius});
			std::vector<SampleFrame> out(static_cast<std::size_t>(outputFrames));
			stretcher.process(source.data(), static_cast<f_cnt_t>(source.size()), out.data(),
				static_cast<f_cnt_t>(out.size()), 2.0);
			return out;
		};

		const std::vector<SampleFrame> aligned = render(128);
		const std::vector<SampleFrame> unaligned = render(0);
		const int from = outputFrames / 4;
		const int to = outputFrames * 3 / 4;

		const double aligned440 = amplitudeAt(aligned.data(), from, to, 440.0);
		const double unaligned440 = amplitudeAt(unaligned.data(), from, to, 440.0);
		const double aligned660 = amplitudeAt(aligned.data(), from, to, 660.0);
		const double unaligned660 = amplitudeAt(unaligned.data(), from, to, 660.0);
		evidence("aligned: 440 / 660", aligned440, aligned660);
		evidence("overlap-add with no search: 440 / 660", unaligned440, unaligned660);

		QVERIFY2(aligned440 > 0.45,
			qPrintable(QStringLiteral("the aligned stretch lost the 440 Hz tone: %1").arg(aligned440)));
		QVERIFY2(unaligned440 < 0.2,
			qPrintable(QStringLiteral("overlap-add with searchRadius 0 kept the tone (%1), so this "
				"test cannot show the search doing anything").arg(unaligned440)));
		QVERIFY2(aligned440 > 4.0 * unaligned440,
			qPrintable(QStringLiteral("the alignment search changed nothing: %1 vs %2")
				.arg(aligned440).arg(unaligned440)));
	}

	/*! The render path calls in PERIODS (one `SamplePlayHandle::play` per audio
	 *  period), so the streaming contract is: the same frames, byte for byte,
	 *  whether they are asked for in one call or in whatever sizes the engine
	 *  happens to use. A discrepancy here would be a per-period discontinuity
	 *  in the render, which is exactly the kind of defect that is inaudible in
	 *  one period and obvious over a clip.
	 */
	void periodSizedCallsProduceTheSameFramesAsOneCall()
	{
		constexpr int sourceFrames = 44100;
		const int outputFrames = sourceFrames / 2;
		const std::vector<SampleFrame> source = twoTone(sourceFrames);

		std::vector<SampleFrame> oneShot(static_cast<std::size_t>(outputFrames));
		AudioStretcher one;
		one.prepare();
		one.process(source.data(), static_cast<f_cnt_t>(source.size()), oneShot.data(),
			static_cast<f_cnt_t>(oneShot.size()), 2.0);

		for (const int period : {64, 512, 1024, 4096})
		{
			std::vector<SampleFrame> streamed(static_cast<std::size_t>(outputFrames));
			AudioStretcher stream;
			stream.prepare();
			int done = 0;
			while (done < outputFrames)
			{
				const int take = std::min(period, outputFrames - done);
				stream.process(source.data(), static_cast<f_cnt_t>(source.size()),
					streamed.data() + done, static_cast<f_cnt_t>(take), 2.0);
				done += take;
			}
			int differing = 0;
			for (int i = 0; i < outputFrames; ++i)
			{
				if (streamed[static_cast<std::size_t>(i)][0] != oneShot[static_cast<std::size_t>(i)][0])
				{
					++differing;
				}
			}
			QVERIFY2(differing == 0,
				qPrintable(QStringLiteral("%1-frame periods differ from the one-shot render in %2 of %3 frames")
					.arg(period).arg(differing).arg(outputFrames)));
		}
	}

	//! The parameters are clamped into the fixed-state caps rather than
	//! throwing, and a repeated prepare() is a no-op (it must not drop the
	//! stream a caller already started).
	void preparesIdempotentlyAndClampsItsParameters()
	{
		AudioStretcher stretcher;
		stretcher.prepare({4096, 4096});
		QCOMPARE(stretcher.grainFrames(), AudioStretcher::MaxGrainFrames);
		QCOMPARE(stretcher.parameters().searchRadius, AudioStretcher::MaxSearchRadius);
		QCOMPARE(stretcher.synthesisHop(), AudioStretcher::MaxGrainFrames / 2);

		stretcher.prepare({10, -5});
		QCOMPARE(stretcher.grainFrames(), 64);
		QCOMPARE(stretcher.synthesisHop(), 32);
		QCOMPARE(stretcher.parameters().searchRadius, 0);

		// A second prepare() with the same shape leaves a started stream alone.
		const std::vector<SampleFrame> source = twoTone(8192);
		std::vector<SampleFrame> out(1024);
		stretcher.prepare({1024, 64});
		stretcher.seek(1000.0);
		QCOMPARE(stretcher.sourcePosition(), 1000.0);
		stretcher.process(source.data(), static_cast<f_cnt_t>(source.size()), out.data(), 512, 2.0);
		const double cursor = stretcher.sourcePosition();
		QVERIFY(cursor > 1000.0);
		stretcher.prepare({1024, 64});
		QCOMPARE(stretcher.sourcePosition(), cursor);

		// reset() is the explicit way back to the start.
		stretcher.reset();
		QCOMPARE(stretcher.sourcePosition(), 0.0);
	}

	//! An unprepared or degenerate call is a no-op, not a crash: the class is
	//! reachable from the audio thread, where there is nobody to catch anything.
	void degenerateCallsAreSafe()
	{
		AudioStretcher stretcher;
		stretcher.prepare();
		std::vector<SampleFrame> out(64, SampleFrame(0.5f, 0.5f));

		QCOMPARE(stretcher.process(nullptr, 100, out.data(), 64, 1.0), f_cnt_t(0));
		QCOMPARE(stretcher.process(out.data(), 64, nullptr, 64, 1.0), f_cnt_t(0));
		QCOMPARE(stretcher.process(out.data(), 64, out.data(), 0, 1.0), f_cnt_t(0));

		// A nonsense speed is treated as "unwarped" rather than dividing by it.
		const std::vector<SampleFrame> source = twoTone(4096);
		QCOMPARE(stretcher.process(source.data(), static_cast<f_cnt_t>(source.size()),
			out.data(), static_cast<f_cnt_t>(out.size()), 0.0), static_cast<f_cnt_t>(out.size()));
		QCOMPARE(stretcher.process(source.data(), static_cast<f_cnt_t>(source.size()),
			out.data(), static_cast<f_cnt_t>(out.size()), std::nan("1")), static_cast<f_cnt_t>(out.size()));

		// A source shorter than a grain, read entirely past its end: silence,
		// not a crash and not the last frame held as DC.
		std::vector<SampleFrame> tiny(8, SampleFrame(1.0f, 1.0f));
		AudioStretcher shortSource;
		shortSource.prepare();
		std::vector<SampleFrame> tail(2048);
		shortSource.process(tiny.data(), static_cast<f_cnt_t>(tiny.size()), tail.data(),
			static_cast<f_cnt_t>(tail.size()), 1.0);
		QCOMPARE(tail.back()[0], 0.0f);
	}

	/*! I8: the render path must not allocate. `prepare()` is the one call that
	 *  touches the window table, and it is called once per handle - everything
	 *  after it is fixed-size member arrays. */
	void processingDoesNotAllocate()
	{
		const std::vector<SampleFrame> source = twoTone(88200);
		std::vector<SampleFrame> out(44100);
		AudioStretcher stretcher;
		stretcher.prepare();

		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		for (int i = 0; i < 2000; ++i)
		{
			stretcher.seek(0.0);
			stretcher.process(source.data(), static_cast<f_cnt_t>(source.size()), out.data(), 512, 2.0);
		}
		const auto allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		evidence("allocations in 2000 x 512-frame render calls", static_cast<double>(allocations));
		QCOMPARE(static_cast<int>(allocations), 0);
	}

	/*! THE COST, MEASURED. The quality/complexity trade the task asks to state:
	 *  the price of the stretch is the alignment search, and it is linear in
	 *  `searchRadius` (that is the sweep the class documents). The assertion is
	 *  deliberately loose - a wall-clock bound that holds on a busy box with
	 *  seven sibling lanes building - and the NUMBERS are printed, which is
	 *  where they are meant to be read.
	 */
	void theCostIsTheAlignmentSearchAndTheNumbersAreMeasured()
	{
		constexpr int sourceFrames = 88200;
		const int outputFrames = sourceFrames / 2;
		const std::vector<SampleFrame> source = twoTone(sourceFrames);
		std::vector<SampleFrame> out(static_cast<std::size_t>(outputFrames));

		double previousMs = 0.0;
		for (const int radius : {32, 64, 128})
		{
			AudioStretcher stretcher;
			stretcher.prepare({1024, radius});
			const auto start = std::chrono::steady_clock::now();
			stretcher.process(source.data(), static_cast<f_cnt_t>(source.size()), out.data(),
				static_cast<f_cnt_t>(out.size()), 2.0);
			const auto stop = std::chrono::steady_clock::now();
			const double milliseconds = std::chrono::duration<double, std::milli>(stop - start).count();
			const double audioSeconds = outputFrames / kRate;
			evidence("searchRadius / render ms / x-realtime", radius, milliseconds, audioSeconds * 1000.0 / milliseconds);
			QVERIFY2(milliseconds < 20.0 * audioSeconds * 1000.0,
				qPrintable(QStringLiteral("radius %1 took %2 ms for %3 s of audio")
					.arg(radius).arg(milliseconds).arg(audioSeconds)));
			previousMs = milliseconds;
		}
		QVERIFY(previousMs > 0.0);
	}
};

QTEST_GUILESS_MAIN(AudioStretcherTest)
#include "AudioStretcherTest.moc"
