/*
 * RnnoiseDenoiserEffectTest.cpp - rate handling for the RNNoise denoiser
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
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <QtTest>

#include "AudioBuffer.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "RnnoiseDenoiserEffect.h"
#include "rnnoise.h"

using namespace lmms;

namespace
{

constexpr f_cnt_t TestFrames = 480;
constexpr float TestSample = 0.25f;
constexpr int RnnoiseLatencyFrames = 479;

// One processAudioBuffer() call long enough for RNNoise to emit a full denoised frame: the first
// 480 samples fill the accumulator, the first processed frame is the internal delay, and the second
// processed frame is real audio. Its output appears at indices 959..1438 (see processImpl()).
constexpr f_cnt_t ProcessingFrames = 1440;
constexpr f_cnt_t EmittedFirst = 959;
constexpr f_cnt_t EmittedLast = 1438;
constexpr float NoisePeak = 0.25f;
constexpr std::uint32_t NoiseSeed = 0x5eed1234u;

// Deterministic LCG: no rand(), no clock, so every run feeds RNNoise the same signal.
float nextNoiseSample(std::uint32_t& state)
{
	state = state * 1664525u + 1013904223u;
	const float unit = static_cast<float>((state >> 8) & 0xFFFFu) / 32768.0f - 1.0f;
	return unit * NoisePeak;
}

enum class StereoInput
{
	HardPannedRight, // left noise, right silent
	OutOfPhase, // left noise, right negated
	InPhase, // left and right identical
};

AudioBuffer processNoiseBlock(StereoInput input)
{
	AudioBuffer buffer{ProcessingFrames, DEFAULT_CHANNELS};
	buffer.allocateInterleavedBuffer();
	std::uint32_t state = NoiseSeed;
	for (f_cnt_t frame = 0; frame < ProcessingFrames; ++frame)
	{
		const float noise = nextNoiseSample(state);
		buffer.interleavedBuffer()[frame][0] = noise;
		buffer.interleavedBuffer()[frame][1] = input == StereoInput::HardPannedRight ? 0.0f
			: (input == StereoInput::OutOfPhase ? -noise : noise);
	}
	buffer.assumeNonSilent(0);
	if (input != StereoInput::HardPannedRight) { buffer.assumeNonSilent(1); }

	RnnoiseDenoiserEffect effect{nullptr, nullptr};
	effect.processAudioBuffer(buffer);
	return buffer;
}

struct ChannelPeaks
{
	float left = 0.0f;
	float right = 0.0f;
};

ChannelPeaks emittedPeaks(const AudioBuffer& buffer)
{
	ChannelPeaks peaks;
	for (f_cnt_t frame = EmittedFirst; frame <= EmittedLast; ++frame)
	{
		peaks.left = std::max(peaks.left, std::abs(buffer.interleavedBuffer()[frame][0]));
		peaks.right = std::max(peaks.right, std::abs(buffer.interleavedBuffer()[frame][1]));
	}
	return peaks;
}


AudioBuffer signalBlock()
{
	AudioBuffer buffer{TestFrames, DEFAULT_CHANNELS};
	buffer.allocateInterleavedBuffer();
	for (f_cnt_t frame = 0; frame < TestFrames; ++frame)
	{
		buffer.interleavedBuffer()[frame][0] = TestSample;
		buffer.interleavedBuffer()[frame][1] = TestSample;
	}
	buffer.assumeNonSilent(0);
	buffer.assumeNonSilent(1);
	return buffer;
}

// The latency regression needs the engine at 48 kHz, the only rate where RNNoise is active. The rest of
// this file runs at 44.1 kHz, so the scope restores that engine when it ends.
class Engine48kScope
{
public:
	Engine48kScope() { restart(QStringLiteral("48000")); }
	~Engine48kScope() { restart(QStringLiteral("44100")); }

private:
	static void restart(const QString& rate)
	{
		Engine::destroy();
		ConfigManager::inst()->setValue(QStringLiteral("audioengine"), QStringLiteral("samplerate"), rate);
		Engine::init(true);
	}
};

} // namespace

class RnnoiseDenoiserEffectTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		ConfigManager::inst()->setValue(QStringLiteral("audioengine"), QStringLiteral("samplerate"),
			QStringLiteral("44100"));
		Engine::init(true);
		QCOMPARE(Engine::audioEngine()->baseSampleRate(), 44100);
	}

	void cleanupTestCase() { Engine::destroy(); }

	void unsupportedRatePassesAudioThrough()
	{
		RnnoiseDenoiserEffect effect{nullptr, nullptr};
		AudioBuffer buffer = signalBlock();
		effect.processAudioBuffer(buffer);
		for (f_cnt_t frame = 0; frame < TestFrames; ++frame)
		{
			QCOMPARE(buffer.interleavedBuffer()[frame][0], TestSample);
			QCOMPARE(buffer.interleavedBuffer()[frame][1], TestSample);
		}
	}

	void unsupportedRateAddsNoLatency()
	{
		RnnoiseDenoiserEffect effect{nullptr, nullptr};
		QCOMPARE(effect.latencyFrames(), 0);
	}

	void stateAt48kReportsRnnoiseLatency()
	{
		Engine48kScope scope;
		QCOMPARE(Engine::audioEngine()->baseSampleRate(), 48000);
		RnnoiseDenoiserEffect effect{nullptr, nullptr};
		QVERIFY(effect.m_rnnoiseState != nullptr);
		QCOMPARE(effect.latencyFrames(), RnnoiseLatencyFrames);
	}

	// Pre-fix: latencyFrames() still reported 479 with no state, while processing bypassed without delay.
	void missingStateAt48kAddsNoLatency()
	{
		Engine48kScope scope;
		QCOMPARE(Engine::audioEngine()->baseSampleRate(), 48000);
		RnnoiseDenoiserEffect effect{nullptr, nullptr};
		discardState(effect);
		QCOMPARE(effect.latencyFrames(), 0);
	}

	void missingStateAt48kPassesAudioThroughUndelayed()
	{
		Engine48kScope scope;
		RnnoiseDenoiserEffect effect{nullptr, nullptr};
		discardState(effect);
		AudioBuffer buffer = signalBlock();
		effect.processAudioBuffer(buffer);
		for (f_cnt_t frame = 0; frame < TestFrames; ++frame)
		{
			QCOMPARE(buffer.interleavedBuffer()[frame][0], TestSample);
			QCOMPARE(buffer.interleavedBuffer()[frame][1], TestSample);
		}
	}

	// Pre-fix: the denoised mono downmix is written to both channels, so a hard-panned left signal
	// bleeds into the silent right channel.
	void hardPannedRightChannelStaysSilent()
	{
		Engine48kScope scope;
		AudioBuffer buffer = processNoiseBlock(StereoInput::HardPannedRight);
		const ChannelPeaks peaks = emittedPeaks(buffer);
		qInfo() << "hardPannedRight max|L| =" << peaks.left << "max|R| =" << peaks.right;
		QVERIFY2(peaks.left > 1e-4f, "RNNoise emitted no left-channel signal; assertion would be vacuous");
		for (f_cnt_t frame = EmittedFirst; frame <= EmittedLast; ++frame)
		{
			const float right = buffer.interleavedBuffer()[frame][1];
			QVERIFY2(right == 0.0f,
				qPrintable(QStringLiteral("frame %1: R=%2").arg(frame).arg(right)));
		}
	}

	// Pre-fix: mirrored stereo cancels in the mono downmix, so RNNoise receives silence and both
	// channels collapse to the same (silent) signal instead of staying mirrored.
	void outOfPhaseStereoIsNotCollapsed()
	{
		Engine48kScope scope;
		AudioBuffer buffer = processNoiseBlock(StereoInput::OutOfPhase);
		const ChannelPeaks peaks = emittedPeaks(buffer);
		qInfo() << "outOfPhase max|L| =" << peaks.left << "max|R| =" << peaks.right;
		QVERIFY2(peaks.left > 1e-4f, "RNNoise emitted no left-channel signal; assertion would be vacuous");
		for (f_cnt_t frame = EmittedFirst; frame <= EmittedLast; ++frame)
		{
			const float left = buffer.interleavedBuffer()[frame][0];
			const float right = buffer.interleavedBuffer()[frame][1];
			// RNNoise's gains follow the magnitude spectrum, so mirrored input yields mirrored output.
			QVERIFY2(std::abs(left + right) <= 1e-6f * peaks.left,
				qPrintable(QStringLiteral("frame %1: L=%2 R=%3").arg(frame).arg(left).arg(right)));
		}
	}

	// Guards against a fix that swaps or decorrelates channels: identical inputs must stay identical.
	void inPhaseStereoStaysIdentical()
	{
		Engine48kScope scope;
		AudioBuffer buffer = processNoiseBlock(StereoInput::InPhase);
		const ChannelPeaks peaks = emittedPeaks(buffer);
		qInfo() << "inPhase max|L| =" << peaks.left << "max|R| =" << peaks.right;
		QVERIFY2(peaks.left > 1e-4f, "RNNoise emitted no left-channel signal; assertion would be vacuous");
		for (f_cnt_t frame = EmittedFirst; frame <= EmittedLast; ++frame)
		{
			const float left = buffer.interleavedBuffer()[frame][0];
			const float right = buffer.interleavedBuffer()[frame][1];
			QVERIFY2(left == right,
				qPrintable(QStringLiteral("frame %1: L=%2 R=%3").arg(frame).arg(left).arg(right)));
		}
	}

private:
	// Leaves the effect as the constructor does when rnnoise_create() fails: no state, nothing to free.
	static void discardState(RnnoiseDenoiserEffect& effect)
	{
		rnnoise_destroy(effect.m_rnnoiseState);
		effect.m_rnnoiseState = nullptr;
	}
};

QTEST_MAIN(RnnoiseDenoiserEffectTest)

#include "RnnoiseDenoiserEffectTest.moc"
