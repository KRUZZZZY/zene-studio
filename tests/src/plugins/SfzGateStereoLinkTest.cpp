// Copyright (c) 2026 Zene Studio contributors
// SPDX-License-Identifier: GPL-2.0-or-later

//! Cluster new-p21-172: sfizz's stereo-linked gate must build its detector input from the right
//! channel's oversampled sample at each index, not from the right channel's sample 1 at every index.

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "Effects.h"
#include "Opcode.h"
#include "effects/Gate.h"

class SfzGateStereoLinkTest : public QObject
{
	Q_OBJECT

private:
	static constexpr double sampleRate = 44100.0;
	static constexpr unsigned numFrames = 512;
	static constexpr float sentinel = -1234.0f;
	static constexpr double pi = 3.14159265358979323846;
	static constexpr double toneHz = 1000.0;
	static constexpr double toneAmplitude = 0.5;

	// Builds a gate with a -30 dB threshold and short envelope times, then sets its sample rate
	// and block size before any process() call.
	static std::unique_ptr<sfz::Effect> makeGate(bool stlink)
	{
		const std::vector<sfz::Opcode> opcodes {
			sfz::Opcode("gate_stlink", stlink ? "1" : "0"),
			sfz::Opcode("gate_threshold", "-30"),
			sfz::Opcode("gate_attack", "0.001"),
			sfz::Opcode("gate_hold", "0.001"),
			sfz::Opcode("gate_release", "0.01"),
		};
		std::unique_ptr<sfz::Effect> gate = sfz::fx::Gate::makeInstance(opcodes);
		gate->setSampleRate(sampleRate);
		gate->setSamplesPerBlock(static_cast<int>(numFrames));
		return gate;
	}

	// Fills a block with a sine tone that starts at an absolute frame index, so consecutive
	// blocks join without a seam. The input is computed, not random, so the test is repeatable.
	static void fillTone(std::vector<float>& buf, unsigned firstFrame)
	{
		for (unsigned i = 0; i < buf.size(); ++i) {
			const double t = static_cast<double>(firstFrame + i) / sampleRate;
			buf[i] = static_cast<float>(toneAmplitude * std::sin(2.0 * pi * toneHz * t));
		}
	}

	static float peakAbs(const std::vector<float>& buf)
	{
		float peak = 0.0f;
		for (float s : buf)
			peak = std::max(peak, std::abs(s));
		return peak;
	}

	// Runs one block through the gate. Outputs start at a sentinel, so a sample that is never
	// written cannot pass as silence.
	static void runBlock(sfz::Effect& gate, const std::vector<float>& left, const std::vector<float>& right,
		std::vector<float>& outLeft, std::vector<float>& outRight)
	{
		outLeft.assign(numFrames, sentinel);
		outRight.assign(numFrames, sentinel);
		const float* inputs[2] { left.data(), right.data() };
		float* outputs[2] { outLeft.data(), outRight.data() };
		gate.process(inputs, outputs, numFrames);
	}

	static QString levelReport(float outPeak, float inPeak)
	{
		return QStringLiteral("right output peak %1 is not above a quarter of the input peak %2")
			.arg(static_cast<double>(outPeak))
			.arg(static_cast<double>(inPeak));
	}

private Q_SLOTS:
	// (a) Stereo link with a silent left and a loud right. The detector must follow the right
	// channel, so the gate opens and the right output keeps the tone.
	void stlinkRightOnlyOpensGate()
	{
		auto gate = makeGate(true);
		const std::vector<float> left(numFrames, 0.0f);
		std::vector<float> right(numFrames);
		fillTone(right, 0);

		std::vector<float> outLeft, outRight;
		runBlock(*gate, left, right, outLeft, outRight);

		const float inPeak = peakAbs(right);
		const float outPeak = peakAbs(outRight);
		QVERIFY2(outPeak > 0.25f * inPeak, qPrintable(levelReport(outPeak, inPeak)));
		QVERIFY2(peakAbs(outLeft) == 0.0f, "silent left output must stay silent");
	}

	// (b) Guard: silent input on both channels gives exact zeros, never NaN or leftover values.
	// Deliberate guard: this may keep passing under the bug, because silence gives zero either way.
	void silentStereoGivesZeroOutput()
	{
		auto gate = makeGate(true);
		const std::vector<float> silence(numFrames, 0.0f);

		std::vector<float> outLeft, outRight;
		runBlock(*gate, silence, silence, outLeft, outRight);

		for (unsigned i = 0; i < numFrames; ++i) {
			QVERIFY2(outLeft[i] == 0.0f,
				qPrintable(QStringLiteral("left[%1] = %2").arg(i).arg(static_cast<double>(outLeft[i]))));
			QVERIFY2(outRight[i] == 0.0f,
				qPrintable(QStringLiteral("right[%1] = %2").arg(i).arg(static_cast<double>(outRight[i]))));
		}
	}

	// (c) Across blocks: the first block is silent on both channels, so the gate is closed when the
	// second block starts. In the second block the right tone starts after a few leading zero
	// frames, and the detector must follow the tone inside that block. A detector that reads one
	// right sample for the whole block keeps the gate closed, which fails this slot.
	void reconfiguredInstanceCarriesGateStateAcrossBlocks()
	{
		constexpr unsigned leadingSilence = 8;
		auto gate = makeGate(true);
		const std::vector<float> left(numFrames, 0.0f);
		std::vector<float> right(numFrames, 0.0f);
		std::vector<float> outLeft, outRight;

		runBlock(*gate, left, right, outLeft, outRight);

		fillTone(right, numFrames);
		std::fill(right.begin(), right.begin() + leadingSilence, 0.0f);
		runBlock(*gate, left, right, outLeft, outRight);

		const float inPeak = peakAbs(right);
		const float outPeak = peakAbs(outRight);
		QVERIFY2(outPeak > 0.25f * inPeak, qPrintable(levelReport(outPeak, inPeak)));
	}

	// (d) Control: with gate_stlink=0 each channel has its own gate, so a loud right channel opens
	// the right gate by itself. This path does not read the stereo-link term.
	// Deliberate guard: this may keep passing under the bug, because it never reads the link term.
	void unlinkedRightOnlyOpensRightGate()
	{
		auto gate = makeGate(false);
		const std::vector<float> left(numFrames, 0.0f);
		std::vector<float> right(numFrames);
		fillTone(right, 0);

		std::vector<float> outLeft, outRight;
		runBlock(*gate, left, right, outLeft, outRight);

		const float inPeak = peakAbs(right);
		const float outPeak = peakAbs(outRight);
		QVERIFY2(outPeak > 0.25f * inPeak, qPrintable(levelReport(outPeak, inPeak)));
		QVERIFY2(peakAbs(outLeft) == 0.0f, "silent left output must stay silent");
	}
};

QTEST_APPLESS_MAIN(SfzGateStereoLinkTest)

#include "SfzGateStereoLinkTest.moc"
