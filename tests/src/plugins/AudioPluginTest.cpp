/*
 * AudioPluginTest.cpp - coverage of AudioPlugin's Effect specialization
 *
 * Copyright (c) 2026 LMMS developers
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

/*
 * Drives include/AudioPlugin.h through a synthetic plugin module
 * (SyntheticAudioPlugin.cpp) so that the paths no production plugin reaches
 * are exercised through the public lmms::Effect API:
 *
 *  - audioPortsModel()'s active() branch
 *  - the sleep / wake-on-input paths of processAudioBuffer()
 *  - processLock() failure
 *  - the bypass path and AudioPlugin's default processBypassedImpl()
 *  - all ProcessStatus switch arms, including the defensive default
 *  - AudioPortSerializer::saveSettings/loadSettings (the legacy bridge's
 *    serialization hook), including the audioPorts() accessor it calls
 *
 * The plugin module is only ever used through lmms::Effect; the C knobs the
 * module exports are resolved via QLibrary.
 */

#include <array>
#include <memory>
#include <vector>

#include <QDomDocument>
#include <QDomElement>
#include <QLibrary>
#include <QTextStream>
#include <QtTest>

#include "AudioBuffer.h"
#include "AudioBus.h"
#include "AudioEngine.h"
#include "AudioPortsModel.h"
#include "Effect.h"
#include "Engine.h"
#include "Plugin.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

using MainFn = Plugin* (*)(Model*, void*);
using SetIntFn = void (*)(int);
using VoidFn = void (*)();
using GetCountsFn = void (*)(int*, int*, int*);

constexpr f_cnt_t TestFrames = 64;

//! 0 = Continue, 1 = ContinueIfNotQuiet, 2 = Sleep
enum StatusCode
{
	StatusContinue = 0,
	StatusContinueIfNotQuiet = 1,
	StatusSleep = 2,
};

} // namespace

class AudioPluginTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase();

	void audioPortsModelExposesActivePorts();
	void saveStateRestoreStateRoundTripsAudioPorts();
	void sleepingEffectSilencesSilentInputAndStaysAsleep();
	void sleepingEffectWakesOnNoisyInputAndProcesses();
	void sleepStatusPutsEffectToSleep();
	void lockFailureReturnsTrueWithoutProcessing();
	void bypassedEffectRunsDefaultBypassImpl();
	void unknownProcessStatusIsIgnored();
	void legacyAudioBufferPathRoutesInPlacePorts();

private:
	auto createEffect() const -> std::unique_ptr<Effect>;
	auto createPlainEffect() const -> std::unique_ptr<Effect>;
	void setStatus(int code) const { m_setStatus(code); }
	void setLockFailures(int count) const { m_setLockFailures(count); }
	void resetCounts() const { m_resetCounts(); }
	auto counts() const -> std::array<int, 3>;

	QLibrary m_lib;
	MainFn m_main = nullptr;
	MainFn m_createPlain = nullptr;
	SetIntFn m_setStatus = nullptr;
	SetIntFn m_setLockFailures = nullptr;
	VoidFn m_resetCounts = nullptr;
	GetCountsFn m_getCounts = nullptr;
};

void AudioPluginTest::initTestCase()
{
	Engine::init(true);
	QVERIFY2(Engine::audioEngine() != nullptr, "engine failed to initialise");
	Engine::audioEngine()->audioDev()->stopProcessing();
	QVERIFY(Engine::audioEngine()->framesPerPeriod() > 0);

	m_lib.setFileName(QStringLiteral(SYNTHETIC_PLUGIN_PATH));
	m_lib.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(m_lib.load(), qPrintable(m_lib.errorString()));

	m_main = reinterpret_cast<MainFn>(m_lib.resolve("lmms_plugin_main"));
	m_createPlain = reinterpret_cast<MainFn>(m_lib.resolve("synthetic_create_plain"));
	m_setStatus = reinterpret_cast<SetIntFn>(m_lib.resolve("synthetic_set_status"));
	m_setLockFailures = reinterpret_cast<SetIntFn>(m_lib.resolve("synthetic_set_lock_failures"));
	m_resetCounts = reinterpret_cast<VoidFn>(m_lib.resolve("synthetic_reset_counts"));
	m_getCounts = reinterpret_cast<GetCountsFn>(m_lib.resolve("synthetic_get_counts"));

	QVERIFY(m_main != nullptr);
	QVERIFY(m_createPlain != nullptr);
	QVERIFY(m_setStatus != nullptr);
	QVERIFY(m_setLockFailures != nullptr);
	QVERIFY(m_resetCounts != nullptr);
	QVERIFY(m_getCounts != nullptr);
}

auto AudioPluginTest::createEffect() const -> std::unique_ptr<Effect>
{
	return std::unique_ptr<Effect>{static_cast<Effect*>(m_main(nullptr, nullptr))};
}

auto AudioPluginTest::createPlainEffect() const -> std::unique_ptr<Effect>
{
	return std::unique_ptr<Effect>{static_cast<Effect*>(m_createPlain(nullptr, nullptr))};
}

auto AudioPluginTest::counts() const -> std::array<int, 3>
{
	std::array<int, 3> result{-1, -1, -1};
	m_getCounts(&result[0], &result[1], &result[2]);
	return result;
}

//! AudioPlugin::audioPortsModel() (the Effect specialization): the active
//! branch must hand out the plugin's port model.
void AudioPluginTest::audioPortsModelExposesActivePorts()
{
	const auto fx = createEffect();
	QVERIFY(fx != nullptr);

	const AudioPortsModel* model = fx->audioPortsModel();
	QVERIFY2(model != nullptr, "an initialized effect must expose its ports model");
	QCOMPARE(model->in().channelCount(), 2);
	QCOMPARE(model->out().channelCount(), 2);
	QCOMPARE(model->isInstrument(), false);
}

//! AudioPortSerializer::saveSettings/loadSettings: the hook must append the
//! audio ports to the effect's XML and restore them again.
void AudioPluginTest::saveStateRestoreStateRoundTripsAudioPorts()
{
	const auto fx = createEffect();
	QVERIFY(fx != nullptr);

	QDomDocument doc;
	QDomElement parent = doc.createElement("syntheticeffect");
	const QDomElement saved = fx->saveState(doc, parent);
	QVERIFY(!saved.isNull());

	const QDomElement pins = saved.firstChildElement("pins");
	QVERIFY2(!pins.isNull(), "AudioPortSerializer must append the audio ports element");
	QCOMPARE(pins.attribute("inputs"), QStringLiteral("2"));
	QCOMPARE(pins.attribute("outputs"), QStringLiteral("2"));
	QVERIFY(!pins.firstChildElement("in_matrix").isNull());
	QVERIFY(!pins.firstChildElement("out_matrix").isNull());

	// Serialize and re-parse so the restore really starts from fresh XML.
	QString xml;
	QTextStream stream{&xml};
	saved.save(stream, 1);
	QDomDocument doc2;
	QVERIFY(doc2.setContent(xml));
	const QDomElement reparsed = doc2.documentElement();
	QVERIFY(!reparsed.isNull());

	fx->restoreState(reparsed);

	const AudioPortsModel* model = fx->audioPortsModel();
	QVERIFY(model != nullptr);
	QCOMPARE(model->in().channelCount(), 2);
	QCOMPARE(model->out().channelCount(), 2);
}

//! A sleeping effect with silent input must stay asleep, silence its output
//! channels and report that it did not continue processing.
void AudioPluginTest::sleepingEffectSilencesSilentInputAndStaysAsleep()
{
	const auto fx = createEffect();
	QVERIFY(fx != nullptr);
	QVERIFY2(!fx->isAwake(), "effects start asleep");

	std::vector<SampleFrame> data(TestFrames, SampleFrame{0.0f, 0.0f});
	SampleFrame* channel = data.data();
	AudioBus bus{&channel, 1, TestFrames};
	QVERIFY(bus.updateAll());

	resetCounts();
	const bool continued = fx->processAudioBuffer(bus);

	QCOMPARE(continued, false);
	QVERIFY(!fx->isAwake());
	const auto [lockCalls, processCalls, bypassCalls] = counts();
	QCOMPARE(lockCalls, 0);
	QCOMPARE(processCalls, 0);
	QCOMPARE(bypassCalls, 0);
	QCOMPARE(data[0].left(), 0.0f);
}

//! A sleeping effect with noisy input must wake up and run its DSP through
//! the audio ports router (ProcessStatus::Continue arm).
void AudioPluginTest::sleepingEffectWakesOnNoisyInputAndProcesses()
{
	const auto fx = createEffect();
	QVERIFY(fx != nullptr);
	QVERIFY2(!fx->isAwake(), "effects start asleep");

	std::vector<SampleFrame> data(TestFrames, SampleFrame{0.8f, -0.4f});
	SampleFrame* channel = data.data();
	AudioBus bus{&channel, 1, TestFrames};
	QVERIFY(!bus.updateAll());

	setStatus(StatusContinue);
	resetCounts();
	const bool continued = fx->processAudioBuffer(bus);

	QCOMPARE(continued, true);
	QVERIFY2(fx->isAwake(), "noisy input must wake the effect");
	const auto [lockCalls, processCalls, bypassCalls] = counts();
	QCOMPARE(lockCalls, 1);
	QCOMPARE(processCalls, 1);
	QCOMPARE(bypassCalls, 0);
	// The synthetic processImpl() halves every frame; 0.8f / 2 and -0.4f / 2
	// are exact in binary floating point.
	QCOMPARE(data[0].left(), 0.4f);
	QCOMPARE(data[0].right(), -0.2f);
}

//! ProcessStatus::Sleep must put the effect back to sleep.
void AudioPluginTest::sleepStatusPutsEffectToSleep()
{
	const auto fx = createEffect();
	QVERIFY(fx != nullptr);

	std::vector<SampleFrame> data(TestFrames, SampleFrame{0.8f, -0.4f});
	SampleFrame* channel = data.data();
	AudioBus bus{&channel, 1, TestFrames};
	QVERIFY(!bus.updateAll());

	setStatus(StatusSleep);
	resetCounts();
	const bool continued = fx->processAudioBuffer(bus);

	QCOMPARE(continued, false);
	QVERIFY(!fx->isAwake());
	const auto [lockCalls, processCalls, bypassCalls] = counts();
	QCOMPARE(lockCalls, 1);
	QCOMPARE(processCalls, 1);
	QCOMPARE(bypassCalls, 0);
}

//! A failing processLock() must leave the effect running but skip its DSP.
void AudioPluginTest::lockFailureReturnsTrueWithoutProcessing()
{
	const auto fx = createEffect();
	QVERIFY(fx != nullptr);

	std::vector<SampleFrame> data(TestFrames, SampleFrame{0.8f, -0.4f});
	SampleFrame* channel = data.data();
	AudioBus bus{&channel, 1, TestFrames};
	QVERIFY(!bus.updateAll());

	setStatus(StatusContinue);
	setLockFailures(1);
	resetCounts();
	const bool continued = fx->processAudioBuffer(bus);

	QCOMPARE(continued, true);
	const auto [lockCalls, processCalls, bypassCalls] = counts();
	QCOMPARE(lockCalls, 1);
	QCOMPARE(processCalls, 0);
	QCOMPARE(bypassCalls, 0);
	// The DSP never ran, so the input is untouched.
	QCOMPARE(data[0].left(), 0.8f);
}

//! A bypassed effect must run AudioPlugin's default (empty)
//! processBypassedImpl() and leave the audio untouched.
void AudioPluginTest::bypassedEffectRunsDefaultBypassImpl()
{
	const auto fx = createPlainEffect();
	QVERIFY(fx != nullptr);

	fx->setDontRun(true);

	std::vector<SampleFrame> data(TestFrames, SampleFrame{0.8f, -0.4f});
	SampleFrame* channel = data.data();
	AudioBus bus{&channel, 1, TestFrames};
	QVERIFY(!bus.updateAll());

	const bool continued = fx->processAudioBuffer(bus);

	// The noisy input woke the effect, but dontRun() keeps it bypassed:
	// isProcessingAudio() stays false and the audio is left untouched.
	QVERIFY(fx->isAwake());
	QCOMPARE(fx->isProcessingAudio(), false);
	QCOMPARE(continued, false);
	QCOMPARE(data[0].left(), 0.8f);
	QCOMPARE(data[0].right(), -0.4f);
}

//! An out-of-range ProcessStatus (the switch's defensive default arm) must be
//! ignored: the effect keeps running and keeps its processed output.
void AudioPluginTest::unknownProcessStatusIsIgnored()
{
	const auto fx = createEffect();
	QVERIFY(fx != nullptr);

	std::vector<SampleFrame> data(TestFrames, SampleFrame{0.8f, -0.4f});
	SampleFrame* channel = data.data();
	AudioBus bus{&channel, 1, TestFrames};
	QVERIFY(!bus.updateAll());

	// Not a ProcessStatus enumerator: exercises `default: break;`.
	setStatus(99);
	resetCounts();
	const bool continued = fx->processAudioBuffer(bus);

	QCOMPARE(continued, true);
	QVERIFY(fx->isAwake());
	const auto [lockCalls, processCalls, bypassCalls] = counts();
	QCOMPARE(lockCalls, 1);
	QCOMPARE(processCalls, 1);
	QCOMPARE(bypassCalls, 0);
	QCOMPARE(data[0].left(), 0.4f);
}

//! The legacy single-buffer path must bridge in-place interleaved effects
//! through the ports router too (#607): the synthetic processImpl halves every
//! sample, so a routed buffer must yield exactly the direct-view result.
void AudioPluginTest::legacyAudioBufferPathRoutesInPlacePorts()
{
	const auto fx = createEffect();
	QVERIFY(fx != nullptr);

	AudioBuffer buffer{TestFrames, DEFAULT_CHANNELS};
	buffer.allocateInterleavedBuffer();
	for (f_cnt_t f = 0; f < TestFrames; ++f)
	{
		buffer.interleavedBuffer()[f][0] = 0.8f;
		buffer.interleavedBuffer()[f][1] = -0.4f;
	}
	buffer.assumeNonSilent(0);
	buffer.assumeNonSilent(1);

	setStatus(StatusContinue);
	resetCounts();
	const bool continued = fx->processAudioBuffer(buffer);

	QCOMPARE(continued, true);
	const auto [lockCalls, processCalls, bypassCalls] = counts();
	// The legacy AudioBuffer entry point is not wrapped by processLock(); only
	// the AudioBus override takes the lock.
	QCOMPARE(lockCalls, 0);
	QCOMPARE(processCalls, 1);
	QCOMPARE(bypassCalls, 0);
	// 0.8f / 2 and -0.4f / 2 are exact in binary floating point.
	QCOMPARE(buffer.interleavedBuffer()[0][0], 0.4f);
	QCOMPARE(buffer.interleavedBuffer()[TestFrames - 1][1], -0.2f);
}

QTEST_MAIN(AudioPluginTest)
#include "AudioPluginTest.moc"
