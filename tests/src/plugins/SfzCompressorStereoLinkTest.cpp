// Copyright (c) 2026 Zene Studio contributors
// SPDX-License-Identifier: GPL-2.0-or-later

//! Cluster new-p21-172: sfizz's stereo-linked compressor must build its detector input from the right
//! channel's oversampled sample at each index, not from the right channel's sample 1 at every index.

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "Effects.h"
#include "Opcode.h"
#include "effects/Compressor.h"

class SfzCompressorStereoLinkTest : public QObject
{
	Q_OBJECT

private:
	static constexpr double sampleRate = 44100.0;
	static constexpr unsigned numFrames = 512;
	static constexpr float sentinel = -1234.0f;
	static constexpr double pi = 3.14159265358979323846;
	static constexpr double toneHz = 1000.0;
	static constexpr double toneAmplitude = 0.5;
	// The compressed output peak must fall below this share of the input peak.
	static constexpr float compressedShare = 0.75f;
	// Levels are measured over frames tailStart..numFrames-1 of the block, after the compressor
	// envelope has settled from the onset. The whole-block peak includes the first crest, which
	// the 1 ms attack has not yet reduced.
	static constexpr unsigned tailStart = 256;

	// Builds a compressor with a -30 dB threshold, a 50:1 ratio and short envelope times, then sets its
	// sample rate and block size before any process() call.
	static std::unique_ptr<sfz::Effect> makeCompressor(bool stlink)
	{
		const std::vector<sfz::Opcode> opcodes {
			sfz::Opcode("comp_stlink", stlink ? "1" : "0"),
			sfz::Opcode("comp_threshold", "-30"),
			sfz::Opcode("comp_ratio", "50"),
			sfz::Opcode("comp_attack", "0.001"),
			sfz::Opcode("comp_release", "0.01"),
		};
		std::unique_ptr<sfz::Effect> comp = sfz::fx::Compressor::makeInstance(opcodes);
		comp->setSampleRate(sampleRate);
		comp->setSamplesPerBlock(static_cast<int>(numFrames));
		return comp;
	}

	// Fills a block with silence before onsetFrame, then a sine tone whose phase starts at zero at
	// onsetFrame. The input is computed, not random, so the test is repeatable.
	static void fillToneFrom(std::vector<float>& buf, unsigned onsetFrame)
	{
		for (unsigned i = 0; i < buf.size(); ++i) {
			if (i < onsetFrame) {
				buf[i] = 0.0f;
				continue;
			}
			const double t = static_cast<double>(i - onsetFrame) / sampleRate;
			buf[i] = static_cast<float>(toneAmplitude * std::sin(2.0 * pi * toneHz * t));
		}
	}

	// Peak magnitude of buf from index from to the end of the block.
	static float peakAbs(const std::vector<float>& buf, unsigned from = 0)
	{
		float peak = 0.0f;
		for (unsigned i = from; i < buf.size(); ++i)
			peak = std::max(peak, std::abs(buf[i]));
		return peak;
	}

	// Runs one block through the compressor. Outputs start at a sentinel, so a sample that is never
	// written cannot pass as silence.
	static void runBlock(sfz::Effect& comp, const std::vector<float>& left, const std::vector<float>& right,
		std::vector<float>& outLeft, std::vector<float>& outRight)
	{
		outLeft.assign(numFrames, sentinel);
		outRight.assign(numFrames, sentinel);
		const float* inputs[2] { left.data(), right.data() };
		float* outputs[2] { outLeft.data(), outRight.data() };
		comp.process(inputs, outputs, numFrames);
	}

	static QString levelReport(float outPeak, float inPeak)
	{
		return QStringLiteral("right output peak %1 is not below %2 times the input peak %3")
			.arg(static_cast<double>(outPeak))
			.arg(static_cast<double>(compressedShare))
			.arg(static_cast<double>(inPeak));
	}

private Q_SLOTS:
	// (a) Stereo link with a silent left and a loud right. The detector must follow the right
	// channel, so the compressor reduces the right output below the input peak.
	void stlinkRightOnlyCompressesRight()
	{
		auto comp = makeCompressor(true);
		const std::vector<float> left(numFrames, 0.0f);
		std::vector<float> right(numFrames);
		fillToneFrom(right, 0);

		std::vector<float> outLeft, outRight;
		runBlock(*comp, left, right, outLeft, outRight);

		const float inPeak = peakAbs(right, tailStart);
		const float outPeak = peakAbs(outRight, tailStart);
		QVERIFY2(outPeak < compressedShare * inPeak, qPrintable(levelReport(outPeak, inPeak)));
		QVERIFY2(peakAbs(outLeft) == 0.0f, "silent left output must stay silent");
	}

	// (b) Across blocks: the first block is silent on both channels, so the compressor starts the
	// second block with no gain reduction. In the second block the right tone starts after eight
	// leading zero frames, and the detector must follow the tone inside that block. A detector that
	// reads right sample 1 of the block keeps reading silence there, so the gain stays at unity and
	// this slot fails.
	void reconfiguredInstanceCarriesCompressorStateAcrossBlocks()
	{
		constexpr unsigned leadingSilence = 8;
		auto comp = makeCompressor(true);
		const std::vector<float> left(numFrames, 0.0f);
		std::vector<float> right(numFrames, 0.0f);
		std::vector<float> outLeft, outRight;

		runBlock(*comp, left, right, outLeft, outRight);

		fillToneFrom(right, leadingSilence);
		runBlock(*comp, left, right, outLeft, outRight);

		const float inPeak = peakAbs(right, tailStart);
		const float outPeak = peakAbs(outRight, tailStart);
		QVERIFY2(outPeak < compressedShare * inPeak, qPrintable(levelReport(outPeak, inPeak)));
	}

	// (c) Guard: with comp_stlink=0 each channel has its own detector, so a loud right channel is
	// compressed by its own level. This path does not read the stereo-link term.
	// Deliberate guard: this must keep passing under the bug, because it never reads the link term.
	void unlinkedRightOnlyCompressesRight()
	{
		auto comp = makeCompressor(false);
		const std::vector<float> left(numFrames, 0.0f);
		std::vector<float> right(numFrames);
		fillToneFrom(right, 0);

		std::vector<float> outLeft, outRight;
		runBlock(*comp, left, right, outLeft, outRight);

		const float inPeak = peakAbs(right, tailStart);
		const float outPeak = peakAbs(outRight, tailStart);
		QVERIFY2(outPeak < compressedShare * inPeak, qPrintable(levelReport(outPeak, inPeak)));
		QVERIFY2(peakAbs(outLeft) == 0.0f, "silent left output must stay silent");
	}
};

QTEST_APPLESS_MAIN(SfzCompressorStereoLinkTest)

#include "SfzCompressorStereoLinkTest.moc"
