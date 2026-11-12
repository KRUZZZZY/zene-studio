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
