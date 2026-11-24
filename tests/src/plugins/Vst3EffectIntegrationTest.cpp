/*
 * Vst3EffectIntegrationTest.cpp - headless end-to-end test of the VST3 effect
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

#include <QtTest>

#include <QDataStream>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "AudioBus.h"
#include "BufferManager.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Plugin.h"
#include "Vst3Effect.h"
#include "Vst3EffectControls.h"
#include "plugin_export.h" // PLUGIN_EXPORT of the vst3effect plug-in

#ifndef VST3_TEST_PLUGIN_PATH
#define VST3_TEST_PLUGIN_PATH ""
#endif

namespace lmms
{

class Vst3EffectIntegrationTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void testProcessesAudioThroughAudioBus();
	void testASidechainChannelIsNotMixedIntoTheSecondOutput();
	void testWrapperProcessingIsQuiescedDuringReprepare();
	void testStateRoundTripThroughMmp();
	void testParameterFallbackWithoutStateChunks();
	void testAValueTheChunksDoNotCarryIsRestoredFromTheHost();
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

//! Where a rendered WAV for inspection goes, on THIS platform.
//!
//! The two write sites used to spell "/tmp/vst3_before.wav" literally, which is
//! a POSIX assumption rather than a path: on Windows "/tmp" resolves to
//! "C:/tmp", that directory does not exist, QFile::open() fails and the
//! QVERIFY at the write site fails on a box whose hosting is otherwise fine.
//! That is exactly what reddened msvc-x64 (job 103762607470,
//! Vst3EffectIntegrationTest.cpp(293)) while the same test passed on Linux and
//! macOS, where "/tmp" does exist. QDir::tempPath() is the platform's own
//! temporary directory ($TMPDIR, %TMP%/%TEMP%, /tmp), and the resolved path is
//! logged so the artefact can be found afterwards.
auto renderedWavPath(const QString& name) -> QString
{
	const auto path = QDir{QDir::tempPath()}.filePath(name);
	qInfo("rendered wav: %s", qPrintable(path));
	return path;
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

extern "C" Plugin::Descriptor PLUGIN_EXPORT vst3effect_plugin_descriptor;

auto Vst3EffectIntegrationTest::makeKey() const -> Plugin::Descriptor::SubPluginFeatures::Key
{
	Plugin::Descriptor::SubPluginFeatures::Key key;
	key.desc = &vst3effect_plugin_descriptor;
	key.name = QStringLiteral("AGain Sample Accurate");
	key.attributes["file"] = QStringLiteral(VST3_TEST_PLUGIN_PATH);
	key.attributes["class"] = QStringLiteral("AGain Sample Accurate");
	return key;
}

void Vst3EffectIntegrationTest::initTestCase()
{
	Engine::init(true);
	Engine::audioEngine()->audioDev()->stopProcessing();
	if (!QFileInfo::exists(QStringLiteral(VST3_TEST_PLUGIN_PATH)))
	{
		QSKIP("no VST3 test plug-in available (build the SDK's 'again' sample)");
	}
}

void Vst3EffectIntegrationTest::cleanupTestCase()
{
	Engine::destroy();
}

void Vst3EffectIntegrationTest::testProcessesAudioThroughAudioBus()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new Vst3Effect{&chain, &key};
	chain.appendEffect(effect);
	QVERIFY(!effect->isCorrupted());

	auto* controls = dynamic_cast<Vst3EffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	QVERIFY(controls->paramModels().size() >= 2);

	// id 1 is "Gain"; id 0 is the stepped bypass switch
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

/*! Cluster new-p8-263: the layout the finding reports, driven through the hosted VST3 path - a mono main
 *  input bus, a side-chain bus starting at flattened channel 1, and two outputs. The two input channels
 *  carry different samples and the mix is half dry, so the second output's value says whether the key
 *  channel reached the dry side of the mix. Channel 1 is past the main input bus, so the mix must leave it
 *  fully wet: the plug-in's own output and nothing else. A dry contribution from channel 1 would show up as
 *  0.5 * key + 0.5 * wet, which the assertion below separates by 0.0625.
 *
 *  The SDK's test effect ("AGain Sample Accurate") declares one stereo port pair and no auxiliary input,
 *  so the layout is reported through Vst3Effect::setBusLayoutForTest(); the plug-in call, the key remap in
 *  processImpl() and the wet/dry mix are the effect's own code. */
void Vst3EffectIntegrationTest::testASidechainChannelIsNotMixedIntoTheSecondOutput()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new Vst3Effect{&chain, &key};
	chain.appendEffect(effect);
	QVERIFY(!effect->isCorrupted());

	auto* controls = dynamic_cast<Vst3EffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	auto* gain = controls->modelForParam(1); // id 1 is "Gain": the plug-in's output is gain * input
	QVERIFY(gain != nullptr);
	gain->setValue(0.5f);

	// A half-dry mix, set through the effect's own saved attribute so both halves of the mix are nonzero.
	QDomDocument settingsDoc;
	auto settings = settingsDoc.createElement(QStringLiteral("effect"));
	effect->saveGenericSettings(settingsDoc, settings);
	settings.setAttribute(QStringLiteral("wet"), QStringLiteral("0.5"));
	effect->loadSettings(settings);
	QVERIFY2(std::abs(effect->wetLevel() - 0.5f) < 1e-6f,
		qPrintable(QStringLiteral("wet level %1").arg(effect->wetLevel())));
	QVERIFY2(std::abs(effect->dryLevel() - 0.5f) < 1e-6f,
		qPrintable(QStringLiteral("dry level %1").arg(effect->dryLevel())));

	// The reported layout: inputs = 1 main channel + 1 key channel, the key bus at flattened channel 1.
	vst3::BusLayout layout;
	layout.inputBusChannels = {1, 1};
	layout.outputBusChannels = {2};
	layout.inputs = 2;
	layout.outputs = 2;
	layout.hasSideChain = true;
	layout.sideChainChannel = 1;
	effect->setBusLayoutForTest(layout);

	constexpr float mainSample = 0.5f;  // channel 0, the main input bus
	constexpr float keySample = -0.25f; // channel 1, the side-chain: never an output's dry source
	SampleFrame storage[fpp];
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		storage[f][0] = mainSample;
		storage[f][1] = keySample;
	}
	SampleFrame* busData[1] = {storage};
	AudioBus bus{busData, 1, fpp};
	QVERIFY(chain.processAudioBuffer(bus));

	// Known results: the plug-in's output is 0.5 * input on each channel. Output 0 is inside the main bus,
	// so it gets dry * main + wet * 0.5 * main. Output 1 is past the main bus and stays fully wet.
	constexpr float plugInGain = 0.5f;
	const auto wet0 = plugInGain * mainSample;
	const auto wet1 = plugInGain * keySample;
	const auto expected0 = 0.5f * mainSample + 0.5f * wet0; // 0.375
	const auto withKeyAsDry1 = 0.5f * keySample + 0.5f * wet1; // -0.1875, what the bug would give
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		QVERIFY2(std::abs(storage[f][0] - expected0) < 1e-6f,
			qPrintable(QStringLiteral("frame %1: main output %2, expected %3")
				.arg(f).arg(storage[f][0]).arg(expected0)));
		QVERIFY2(std::abs(storage[f][1] - wet1) < 1e-6f,
			qPrintable(QStringLiteral("frame %1: output 1 is %2, expected the wet value %3 - the "
				"side-chain channel was mixed into it as dry audio (that would give %4)")
				.arg(f).arg(storage[f][1]).arg(wet1).arg(withKeyAsDry1)));
	}
	qInfo("sidechain layout: main %.6f key %.6f, gain %.3f, wet 0.5 dry 0.5 -> out[0]=%.9f out[1]=%.9f "
		"(a key-as-dry mix would give out[1]=%.9f)",
		mainSample, keySample, plugInGain, storage[0][0], storage[0][1], withKeyAsDry1);
}

void Vst3EffectIntegrationTest::testWrapperProcessingIsQuiescedDuringReprepare()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new Vst3Effect{&chain, &key};
	chain.appendEffect(effect);
	QVERIFY(!effect->isCorrupted());
	auto* controls = dynamic_cast<Vst3EffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr);
	gain->setValue(0.5f);

	SampleFrame storage[fpp];
	auto fillInput = [&storage, fpp] {
		for (f_cnt_t frame = 0; frame < fpp; ++frame)
		{
			storage[frame][0] = 0.5f;
			storage[frame][1] = 0.5f;
		}
	};
	fillInput();
	SampleFrame* busData[1] = {storage};
	AudioBus bus{busData, 1, fpp};
	QVERIFY(chain.processAudioBuffer(bus));
	QVERIFY(std::abs(storage[0][0] - 0.25f) < 1e-6f);

	struct PausePoint
	{
		std::atomic<bool> entered{false};
		std::atomic<bool> release{false};
		std::atomic<bool> processed{false};
		bool processSucceeded = false;
		float firstOutput = 0.0f;
	};
	PausePoint pause;
	effect->setReprepareTestHook([](void* context) {
		auto& point = *static_cast<PausePoint*>(context);
		point.entered.store(true, std::memory_order_release);
		while (!point.release.load(std::memory_order_acquire)) { std::this_thread::yield(); }
	}, &pause);

	std::thread audioCall([&] {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (!pause.entered.load(std::memory_order_acquire)
			&& std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::yield();
		}
		if (pause.entered.load(std::memory_order_acquire))
		{
			fillInput();
			pause.processSucceeded = chain.processAudioBuffer(bus);
			pause.firstOutput = storage[0][0];
			pause.processed.store(true, std::memory_order_release);
		}
		pause.release.store(true, std::memory_order_release);
	});
	effect->reprepare();
	audioCall.join();

	QVERIFY(pause.entered.load(std::memory_order_acquire));
	QVERIFY(pause.processed.load(std::memory_order_acquire));
	QVERIFY(pause.processSucceeded);
	QVERIFY2(std::abs(pause.firstOutput - 0.5f) < 1e-6f,
		qPrintable(QStringLiteral("wrapper processed plugin audio during re-prepare: %1")
			.arg(pause.firstOutput)));
}

void Vst3EffectIntegrationTest::testStateRoundTripThroughMmp()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new Vst3Effect{&chain, &key};
	chain.appendEffect(effect);

	auto* controls = dynamic_cast<Vst3EffectControls*>(effect->controls());
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

void Vst3EffectIntegrationTest::testParameterFallbackWithoutStateChunks()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new Vst3Effect{&chain, &key};
	chain.appendEffect(effect);
	auto* controls = dynamic_cast<Vst3EffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr);
	gain->setValue(0.3f);

	SampleFrame storage[fpp];
	auto fillInput = [&storage, fpp] {
		for (f_cnt_t frame = 0; frame < fpp; ++frame)
		{
			storage[frame][0] = 0.5f;
			storage[frame][1] = 0.5f;
		}
	};
	fillInput();
	SampleFrame* busData[1] = {storage};
	AudioBus bus{busData, 1, fpp};
	QVERIFY(chain.processAudioBuffer(bus));
	QVERIFY(std::abs(storage[0][0] - 0.15f) < 1e-6f);

	QDomDocument doc;
	QDomElement element = doc.createElement("effect");
	effect->saveSettings(doc, element);
	auto stateElement = element.firstChildElement(QStringLiteral("vst3"));
	QVERIFY(!stateElement.isNull());
	QVERIFY(!stateElement.firstChildElement(QStringLiteral("parameters")).isNull());
	QVERIFY(!stateElement.firstChildElement(QStringLiteral("componentstate")).isNull());
	stateElement.removeChild(stateElement.firstChildElement(QStringLiteral("componentstate")));
	stateElement.removeChild(stateElement.firstChildElement(QStringLiteral("controllerstate")));

	gain->setValue(0.9f);
	effect->plugin()->notifyController(1, 0.9f);
	effect->loadSettings(element);
	QVERIFY(std::abs(gain->value() - 0.3f) < 1e-6f);

	fillInput();
	QVERIFY(chain.processAudioBuffer(bus));
	QVERIFY2(std::abs(storage[0][0] - 0.15f) < 1e-6f,
		qPrintable(QStringLiteral("restored %1 without state chunks").arg(storage[0][0])));
}

void Vst3EffectIntegrationTest::testRendersBeforeAfterWav()
{
	constexpr f_cnt_t fpp = 48;
	constexpr int sampleRate = 44100;
	// One second of audio, rounded DOWN to a whole number of 48-frame blocks.
	// `totalFrames = sampleRate` is NOT a multiple of fpp (44100 / 48 = 918.75),
	// so the loop below - which writes storage[f] for every f in [0, fpp) and
	// then copies dry[pos + f] out and wet[pos + f] in for the same range - used
	// to run its last iteration 12 frames past the end of both vectors. That is
	// a heap buffer over-run in the TEST, not in the effect: it was found the
	// first time this test was ever built and run (0.2.1 coverage gap 1, the
	// three VST3 effect tests were registered nowhere), where it aborted with
	// "malloc(): corrupted top size". Rounding down keeps the block loop uniform
	// and the assertion (wet RMS = 0.5 * dry RMS) unchanged.
	constexpr int totalFrames = (sampleRate / fpp) * fpp;
	constexpr double frequency = 440.0;
	constexpr float amplitude = 0.5f;

	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new Vst3Effect{&chain, &key};
	chain.appendEffect(effect);
	QVERIFY(!effect->isCorrupted());

	auto* controls = dynamic_cast<Vst3EffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr);
	gain->setValue(0.5f);

	std::vector<float> dry(totalFrames, 0.0f);
	std::vector<float> wet(totalFrames, 0.0f);

	for (int pos = 0; pos < totalFrames; pos += fpp)
	{
		SampleFrame storage[fpp];
		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			const auto sample = amplitude * static_cast<float>(std::sin(
				2.0 * kPi * frequency * static_cast<double>(pos + f) / sampleRate));
			storage[f][0] = sample;
			storage[f][1] = sample;
			dry[static_cast<std::size_t>(pos) + f] = sample;
		}

		SampleFrame* busData[1] = {storage};
		AudioBus bus{busData, 1, fpp};
		QVERIFY(chain.processAudioBuffer(bus));

		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			wet[static_cast<std::size_t>(pos) + f] = storage[f][0];
		}
	}

	QVERIFY(writeWav(renderedWavPath(QStringLiteral("vst3_before.wav")), dry, sampleRate));
	QVERIFY(writeWav(renderedWavPath(QStringLiteral("vst3_after.wav")), wet, sampleRate));

	const auto dryRms = rms(dry);
	const auto wetRms = rms(wet);
	qInfo("wav render: 1 s @ %d Hz, 440 Hz sine amp 0.5, gain 0.5 -> dry RMS=%.9f "
		  "wet RMS=%.9f ratio=%.6f",
		sampleRate, dryRms, wetRms, wetRms / dryRms);
	QVERIFY2(std::abs(wetRms - 0.5 * dryRms) < 1e-4,
		qPrintable(QStringLiteral("dry %1 wet %2").arg(dryRms).arg(wetRms)));
}

/*! stress-4 (b), the effect side of Vst3InstrumentIntegrationTest's case: a SUCCESSFUL chunk load that does not
 *  bring a writable parameter back must not lose it. Gain is set and the effect saved before any block carried it
 *  to the processor - an effect on a silent track is exactly that - so the component state holds the default and
 *  the host's saved <parameters> hold 0.3. Loading it back leaves Gain at 0.3, and the next block applies it. */
void Vst3EffectIntegrationTest::testAValueTheChunksDoNotCarryIsRestoredFromTheHost()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new Vst3Effect{&chain, &key};
	chain.appendEffect(effect);
	auto* controls = dynamic_cast<Vst3EffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr);
	gain->setValue(0.3f);

	QDomDocument doc;
	QDomElement element = doc.createElement("effect");
	effect->saveSettings(doc, element);
	const auto stateElement = element.firstChildElement(QStringLiteral("vst3"));
	QVERIFY2(!stateElement.firstChildElement(QStringLiteral("componentstate")).isNull(),
		"the effect wrote no state chunk: this case is about a SUCCESSFUL chunk load");

	gain->setValue(0.9f);
	effect->loadSettings(element);
	QVERIFY2(std::abs(gain->value() - 0.3f) < 1e-6f, qPrintable(QStringLiteral("restored %1").arg(gain->value())));

	SampleFrame storage[fpp];
	for (f_cnt_t frame = 0; frame < fpp; ++frame)
	{
		storage[frame][0] = 0.5f;
		storage[frame][1] = 0.5f;
	}
	SampleFrame* busData[1] = {storage};
	AudioBus bus{busData, 1, fpp};
	QVERIFY(chain.processAudioBuffer(bus));
	QVERIFY2(std::abs(storage[0][0] - 0.15f) < 1e-6f, qPrintable(QStringLiteral("processed %1").arg(storage[0][0])));
}

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::Vst3EffectIntegrationTest)

#include "Vst3EffectIntegrationTest.moc"
