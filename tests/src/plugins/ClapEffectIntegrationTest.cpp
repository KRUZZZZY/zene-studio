/*
 * ClapEffectIntegrationTest.cpp - headless end-to-end test of the CLAP effect
 *
 * Copyright (c) 2026 LMMS contributors
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

#include <QtTest>

#include <QDataStream>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <vector>

#include "AudioBus.h"
#include "BufferManager.h"
#include "ClapEffect.h"
#include "ClapEffectControls.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Plugin.h"
#include "plugin_export.h" // PLUGIN_EXPORT of the clapeffect plug-in

#ifndef CLAP_TEST_PLUGIN_PATH
#define CLAP_TEST_PLUGIN_PATH ""
#endif

namespace lmms
{

class ClapEffectIntegrationTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void testProcessesAudioThroughAudioBus();
	void testMultiChannelBusPassesThroughExtraChannels();
	void testStateRoundTripThroughMmp();
	void testRendersBeforeAfterWav();

private:
	auto makeKey() const -> Plugin::Descriptor::SubPluginFeatures::Key;
};

namespace
{

constexpr double kPi = 3.14159265358979323846;

auto rms(const std::vector<float>& samples) -> double
{
	double sum = 0.0;
	for (const auto sample : samples)
	{
		sum += static_cast<double>(sample) * static_cast<double>(sample);
	}
	return std::sqrt(sum / static_cast<double>(samples.size()));
}

//! Minimal 16-bit stereo PCM WAV writer, so rendered audio can be inspected.
auto writeWav(const QString& path, const std::vector<float>& samples, int sampleRate) -> bool
{
	QFile file{path};
	if (!file.open(QIODevice::WriteOnly)) { return false; }

	QDataStream out{&file};
	out.setByteOrder(QDataStream::LittleEndian);

	const auto dataBytes = static_cast<quint32>(samples.size() * 2 * sizeof(qint16));
	out.writeRawData("RIFF", 4);
	out << quint32(36 + dataBytes);
	out.writeRawData("WAVE", 4);
	out.writeRawData("fmt ", 4);
	out << quint32(16) << quint16(1) << quint16(2) << quint32(sampleRate)
	    << quint32(sampleRate * 2 * sizeof(qint16)) << quint16(2 * sizeof(qint16))
	    << quint16(16);
	out.writeRawData("data", 4);
	out << dataBytes;

	for (const auto sample : samples)
	{
		const auto pcm = static_cast<qint16>(
			std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
		out << pcm << pcm;
	}

	return file.error() == QFileDevice::NoError;
}

} // namespace

extern "C" Plugin::Descriptor PLUGIN_EXPORT clapeffect_plugin_descriptor;

auto ClapEffectIntegrationTest::makeKey() const -> Plugin::Descriptor::SubPluginFeatures::Key
{
	Plugin::Descriptor::SubPluginFeatures::Key key;
	key.desc = &clapeffect_plugin_descriptor;
	key.name = QStringLiteral("LMMS CLAP Test Gain");
	key.attributes["file"] = QStringLiteral(CLAP_TEST_PLUGIN_PATH);
	key.attributes["id"] = QStringLiteral("org.lmms.test.clap-gain");
	return key;
}

void ClapEffectIntegrationTest::initTestCase()
{
	Engine::init(true);
	Engine::audioEngine()->audioDev()->stopProcessing();
	if (!QFileInfo::exists(QStringLiteral(CLAP_TEST_PLUGIN_PATH)))
	{
		QSKIP("no CLAP test plug-in available (build tests/data/clap-test-plugin "
			"from the pinned CLAP headers)");
	}
}

void ClapEffectIntegrationTest::cleanupTestCase()
{
	Engine::destroy();
}

void ClapEffectIntegrationTest::testProcessesAudioThroughAudioBus()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);
	QVERIFY(!effect->isCorrupted());

	auto* controls = dynamic_cast<ClapEffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	QVERIFY(controls->paramModels().size() >= 2);

	// id 1 is "Gain"; id 2 is the stepped bypass switch
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr);
	gain->setValue(0.5f);

	SampleFrame storage[fpp];
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		storage[f][0] = 0.5f;
		storage[f][1] = -0.25f;
	}
	SampleFrame* busData[1] = { storage };
	AudioBus bus{busData, 1, fpp};

	QVERIFY(chain.processAudioBuffer(bus));

	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		QVERIFY2(std::abs(storage[f][0] - 0.25f) < 1e-6f,
			qPrintable(QStringLiteral("left %1").arg(storage[f][0])));
		QVERIFY2(std::abs(storage[f][1] + 0.125f) < 1e-6f,
			qPrintable(QStringLiteral("right %1").arg(storage[f][1])));
	}
	qInfo("effect chain: input 0.5/-0.25, gain 0.5 -> measured out[0]=%.9f out[1]=%.9f",
		storage[0][0], storage[0][1]);
}

//! The Part B transport is multi-channel: the stereo plug-in must process its
//! own channel pair and leave the remaining track channels untouched.
void ClapEffectIntegrationTest::testMultiChannelBusPassesThroughExtraChannels()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);
	QVERIFY(!effect->isCorrupted());

	auto* controls = dynamic_cast<ClapEffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr);
	gain->setValue(0.5f);

	SampleFrame pairA[fpp];
	SampleFrame pairB[fpp];
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		pairA[f][0] = 0.5f;
		pairA[f][1] = 0.5f;
		pairB[f][0] = 0.25f;
		pairB[f][1] = -0.5f;
	}
	SampleFrame* busData[2] = { pairA, pairB };
	AudioBus bus{busData, 2, fpp}; // four track channels

	QVERIFY(chain.processAudioBuffer(bus));

	QVERIFY2(std::abs(pairA[0][0] - 0.25f) < 1e-6f,
		qPrintable(QStringLiteral("processed %1").arg(pairA[0][0])));
	QVERIFY2(std::abs(pairB[0][0] - 0.25f) < 1e-6f,
		qPrintable(QStringLiteral("passthrough %1").arg(pairB[0][0])));
	QVERIFY2(std::abs(pairB[0][1] + 0.5f) < 1e-6f,
		qPrintable(QStringLiteral("passthrough %1").arg(pairB[0][1])));
	qInfo("multichannel bus: pair 0 in 0.5 -> measured %.9f, pair 1 untouched %.9f/%.9f",
		pairA[0][0], pairB[0][0], pairB[0][1]);
}

void ClapEffectIntegrationTest::testStateRoundTripThroughMmp()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);

	auto* controls = dynamic_cast<ClapEffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr);

	gain->setValue(0.3f);

	// Deliver the change to the plug-in before saving. Parameter changes reach
	// the processor through processAudioBuffer(), exactly as in a running host.
	SampleFrame storage[fpp];
	auto fillInput = [&storage, fpp] {
		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			storage[f][0] = 0.5f;
			storage[f][1] = 0.5f;
		}
	};
	fillInput();
	SampleFrame* busData[1] = { storage };
	AudioBus bus{busData, 1, fpp};
	QVERIFY(chain.processAudioBuffer(bus));
	QVERIFY2(std::abs(storage[0][0] - 0.15f) < 1e-6f,
		qPrintable(QStringLiteral("delivered %1").arg(storage[0][0])));

	QDomDocument doc;
	QDomElement element = doc.createElement("effect");
	effect->saveSettings(doc, element);

	gain->setValue(0.9f);
	QVERIFY(std::abs(gain->value() - 0.9f) < 1e-6f);

	effect->loadSettings(element);
	QVERIFY2(std::abs(gain->value() - 0.3f) < 1e-6f,
		qPrintable(QStringLiteral("restored %1").arg(gain->value())));

	fillInput();
	QVERIFY(chain.processAudioBuffer(bus));

	QVERIFY2(std::abs(storage[0][0] - 0.15f) < 1e-6f,
		qPrintable(QStringLiteral("processed %1").arg(storage[0][0])));
	qInfo("state round trip: input 0.5, restored gain 0.3 -> measured out[0]=%.9f",
		storage[0][0]);
}

void ClapEffectIntegrationTest::testRendersBeforeAfterWav()
{
	constexpr f_cnt_t fpp = 48;
	constexpr int sampleRate = 44100;
	constexpr int totalFrames = sampleRate; // one second
	constexpr double frequency = 440.0;
	constexpr float amplitude = 0.5f;

	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new ClapEffect{&chain, &key};
	chain.appendEffect(effect);
	QVERIFY(!effect->isCorrupted());

	auto* controls = dynamic_cast<ClapEffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr);

	std::vector<float> dry(totalFrames, 0.0f);
	std::vector<float> wetUnity(totalFrames, 0.0f);
	std::vector<float> wetHalf(totalFrames, 0.0f);

	auto render = [&](std::vector<float>& wet, float gainValue) {
		gain->setValue(gainValue);
		for (int pos = 0; pos < totalFrames; pos += fpp)
		{
			SampleFrame storage[fpp];
			for (f_cnt_t f = 0; f < fpp; ++f)
			{
				const auto sample = amplitude * static_cast<float>(std::sin(
					2.0 * kPi * frequency * static_cast<double>(pos + f) / sampleRate));
				storage[f][0] = sample;
				storage[f][1] = sample;
				if (gainValue == 1.0f) { dry[static_cast<std::size_t>(pos) + f] = sample; }
			}

			SampleFrame* busData[1] = {storage};
			AudioBus bus{busData, 1, fpp};
			QVERIFY(chain.processAudioBuffer(bus));

			for (f_cnt_t f = 0; f < fpp; ++f)
			{
				wet[static_cast<std::size_t>(pos) + f] = storage[f][0];
			}
		}
	};

	render(wetUnity, 1.0f);
	render(wetHalf, 0.5f);

	QVERIFY(writeWav(QStringLiteral("/tmp/clap_before.wav"), dry, sampleRate));
	QVERIFY(writeWav(QStringLiteral("/tmp/clap_after.wav"), wetHalf, sampleRate));

	const auto dryRms = rms(dry);
	const auto unityRms = rms(wetUnity);
	const auto halfRms = rms(wetHalf);
	qInfo("wav render: 1 s @ %d Hz, 440 Hz sine amp 0.5 -> dry RMS=%.9f "
		  "unity gain RMS=%.9f ratio=%.6f, gain 0.5 RMS=%.9f ratio=%.6f",
		sampleRate, dryRms, unityRms, unityRms / dryRms, halfRms, halfRms / dryRms);
	QVERIFY2(std::abs(unityRms - dryRms) < 1e-6,
		qPrintable(QStringLiteral("unity %1 dry %2").arg(unityRms).arg(dryRms)));
	QVERIFY2(std::abs(halfRms - 0.5 * dryRms) < 1e-6,
		qPrintable(QStringLiteral("half %1 dry %2").arg(halfRms).arg(dryRms)));
}

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::ClapEffectIntegrationTest)

#include "ClapEffectIntegrationTest.moc"
