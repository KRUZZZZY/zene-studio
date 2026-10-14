/*
 * AudioPluginTest.cpp - coverage of AudioPlugin's Effect specialization
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
 *
 * Windows: a test host cannot load this module at all - an MSVC module's
 * import descriptor names zene.exe, and the test host is not zene.exe - so
 * initTestCase() skips the whole suite there (see tests/QA-GATES.md, Notes).
 * Linux/macOS run every assertion below unchanged.
 */

#include <array>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <cmath>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <memory>
#include <new>
#include <vector>

#include <fftw3.h>
#include <QMutex>
#include <QRgb>
#include <QCoreApplication>
#include <QDir>
#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfo>
#include <QLibrary>
#include <QTemporaryDir>
#include <QProcess>
#include <QTextStream>
#include <QtTest>

#define private public
#include "../../../plugins/SpectrumAnalyzer/SaProcessor.h"
#undef private

#include "../../../plugins/SpectrumAnalyzer/SaProcessor.cpp"

#include "AudioBuffer.h"
#include "AudioBus.h"
#include "AudioEngine.h"
#include "AudioPortsModel.h"
#include "Effect.h"
#include "EffectControls.h"
#include "Engine.h"
#include "Plugin.h"
#include "SampleFrame.h"
#include "../../../plugins/NeuralAmp/NeuralAmpEffect.h"
#include "../../../plugins/Patman/Patman.h"
#include "ToolPlugin.h"
#include "TapTempoView.h"
#define private public
#include "TapTempo.h"
#undef private

using namespace lmms;

namespace
{

using MainFn = Plugin* (*)(Model*, void*);
using SetIntFn = void (*)(int);
using VoidFn = void (*)();
using GetCountsFn = void (*)(int*, int*, int*);

constexpr f_cnt_t TestFrames = 64;
constexpr qsizetype PatchHeaderSize = 239;
constexpr qsizetype PatchSampleHeaderSize = 96;

void writeLittleEndian16(QByteArray& data, qsizetype offset, std::uint16_t value)
{
	data[offset] = static_cast<char>(value & 0xff);
	data[offset + 1] = static_cast<char>(value >> 8);
}

void writeLittleEndian32(QByteArray& data, qsizetype offset, std::uint32_t value)
{
	for (int byte = 0; byte < 4; ++byte)
	{
		data[offset + byte] = static_cast<char>((value >> (byte * 8)) & 0xff);
	}
}

auto makeSingleSamplePatch() -> QByteArray
{
	constexpr qsizetype SampleFrames = 4;
	auto patch = QByteArray(PatchHeaderSize + PatchSampleHeaderSize + SampleFrames, '\0');
	std::memcpy(patch.data(), "GF1PATCH110\0ID#000002", 22);
	patch[198] = 1;

	const qsizetype sample = PatchHeaderSize;
	writeLittleEndian32(patch, sample + 8, SampleFrames);
	writeLittleEndian32(patch, sample + 16, SampleFrames);
	writeLittleEndian16(patch, sample + 20, 22050);
	writeLittleEndian32(patch, sample + 30, 440000);
	patch[PatchHeaderSize + PatchSampleHeaderSize] = 32;
	patch[PatchHeaderSize + PatchSampleHeaderSize + 1] = -32;
	patch[PatchHeaderSize + PatchSampleHeaderSize + 2] = 64;
	patch[PatchHeaderSize + PatchSampleHeaderSize + 3] = -64;
	return patch;
}

//! 0 = Continue, 1 = ContinueIfNotQuiet, 2 = Sleep
enum StatusCode
{
	StatusContinue = 0,
	StatusContinueIfNotQuiet = 1,
	StatusSleep = 2,
};

/*!
 * Windows: a test host cannot load a plugin MODULE library at runtime.
 *
 * Every plugin module links the zene executable, so an MSVC module's import
 * descriptor names zene.exe; the Windows loader then fails with
 * ERROR_MOD_NOT_FOUND (126) because the test host is not zene.exe. CI
 * (msvc-x64, QT_FORCE_STDERR_LOGGING=1) shows the loader error verbatim:
 *   QWARN : ... Cannot load library ...\plugins	ripleoscillator.dll:
 *           The specified module could not be found.
 * On Linux/macOS the module's undefined lmms symbols are bound from the
 * loading process's exported symbol table (the test target sets
 * ENABLE_EXPORTS), so the same load succeeds there. The product loads these
 * modules inside zene.exe, where the import resolves by construction: this
 * is a test-host limitation, not a product defect.
 */
constexpr auto testHostCanLoadPluginModules() -> bool
{
#ifdef Q_OS_WIN
	return false;
#else
	return true;
#endif
}

} // namespace

class AudioPluginTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase();

	void waterfallFlipRequestStartsClear();
	void audioPortsModelExposesActivePorts();
	void saveStateRestoreStateRoundTripsAudioPorts();
	void sleepingEffectSilencesSilentInputAndStaysAsleep();
	void sleepingEffectWakesOnNoisyInputAndProcesses();
	void eqActiveFilterKeepsOneFrameOutputFinite();
	void sleepStatusPutsEffectToSleep();
	void lockFailureReturnsTrueWithoutProcessing();
	void bypassedEffectRunsDefaultBypassImpl();
	void unknownProcessStatusIsIgnored();
	void legacyAudioBufferPathRoutesInPlacePorts();
	void neuralAmpRejectsModelsTrainedAtAnotherSampleRate();
	void clearingPatmanPatchReleasesItsSampleBank();
	void subMillisecondTapIntervalDoesNotProduceInfiniteTempo();
	void namHarnessRejectsNegativeWarmup();

private:
	auto createEffect() const -> std::unique_ptr<Effect>;
	auto createPlainEffect() const -> std::unique_ptr<Effect>;
	void setStatus(int code) const { m_setStatus(code); }
	void setLockFailures(int count) const { m_setLockFailures(count); }
	void resetCounts() const { m_resetCounts(); }
	auto counts() const -> std::array<int, 3>;

	QLibrary m_lib;
	QLibrary m_eqLib;
	MainFn m_main = nullptr;
	MainFn m_eqMain = nullptr;
	MainFn m_createPlain = nullptr;
	SetIntFn m_setStatus = nullptr;
	SetIntFn m_setLockFailures = nullptr;
	VoidFn m_resetCounts = nullptr;
	GetCountsFn m_getCounts = nullptr;
};

void AudioPluginTest::initTestCase()
{
	// A Windows test host cannot load the synthetic module (proven by the
	// MSVC import descriptor naming zene.exe - see the note on
	// testHostCanLoadPluginModules()), and this whole suite is about driving
	// that loaded module, so skip instead of failing at the load.
	if (!testHostCanLoadPluginModules())
	{
		QSKIP("cannot load the synthetic plugin module in a Windows test host: plugin "
			"modules link the zene executable, so on Windows their import descriptor names "
			"zene.exe and a test host cannot satisfy it; the product loads them inside "
			"zene.exe where that resolves by construction (CI msvc-x64: QLibrary::load -> "
			"ERROR_MOD_NOT_FOUND, 126)");
	}

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

	m_eqLib.setFileName(QDir{QCoreApplication::applicationDirPath()}.filePath(
		QStringLiteral("../plugins/eq")));
	m_eqLib.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(m_eqLib.load(), qPrintable(m_eqLib.errorString()));
	m_eqMain = reinterpret_cast<MainFn>(m_eqLib.resolve("lmms_plugin_main"));
	QVERIFY(m_eqMain != nullptr);
}

void AudioPluginTest::waterfallFlipRequestStartsClear()
{
	alignas(SaProcessor) std::array<std::byte, sizeof(SaProcessor)> storage;
	storage.fill(std::byte{0x01});
	auto* processor = new (storage.data()) SaProcessor(nullptr);

	std::array<unsigned char, sizeof(bool)> actualRepresentation{};
	std::array<unsigned char, sizeof(bool)> falseRepresentation{};
	const bool falseValue = false;
	std::memcpy(actualRepresentation.data(), &processor->m_flipRequest, sizeof(bool));
	std::memcpy(falseRepresentation.data(), &falseValue, sizeof(bool));
	std::destroy_at(processor);

	QVERIFY(actualRepresentation == falseRepresentation);
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

//! A one-frame EQ block has a defined progress value for active filters.
void AudioPluginTest::eqActiveFilterKeepsOneFrameOutputFinite()
{
	const auto fx = std::unique_ptr<Effect>{static_cast<Effect*>(m_eqMain(nullptr, nullptr))};
	QVERIFY(fx != nullptr);

	QDomDocument settingsDocument;
	QDomElement settings = settingsDocument.createElement(QStringLiteral("Eq"));
	settings.setAttribute(QStringLiteral("HPactive"), QStringLiteral("1"));
	fx->controls()->restoreState(settings);

	std::array<SampleFrame, 1> data{SampleFrame{0.8f, -0.4f}};
	SampleFrame* channel = data.data();
	AudioBus bus{&channel, 1, 1};
	QVERIFY(!bus.updateAll());
	QVERIFY(fx->processAudioBuffer(bus));
	QVERIFY(std::isfinite(data[0].left()));
	QVERIFY(std::isfinite(data[0].right()));
	QVERIFY(data[0].left() != 0.8f || data[0].right() != -0.4f);
	// The audio bus clears any channel containing NaN, so invalid progress
	// would turn both samples into silence before they reach this assertion.
	QVERIFY(data[0].left() != 0.0f || data[0].right() != 0.0f);
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

void AudioPluginTest::neuralAmpRejectsModelsTrainedAtAnotherSampleRate()
{
	using nam::sampleRateMatchesEngine;
	QVERIFY(sampleRateMatchesEngine(-1.0, 48000.0)); // model has no declared rate
	QVERIFY(sampleRateMatchesEngine(48000.0, 48000.0));
	QVERIFY(sampleRateMatchesEngine(48000.5, 48000.0));
	QVERIFY(!sampleRateMatchesEngine(44100.0, 48000.0));
	QVERIFY(!sampleRateMatchesEngine(48000.0, 0.0));
}

//! Clearing a Patman patch must release the bank's last strong sample reference.
void AudioPluginTest::clearingPatmanPatchReleasesItsSampleBank()
{
	QTemporaryDir directory;
	QVERIFY(directory.isValid());

	QFile patchFile(directory.filePath(QStringLiteral("single-sample.pat")));
	QVERIFY(patchFile.open(QIODevice::WriteOnly));
	const QByteArray patch = makeSingleSamplePatch();
	QCOMPARE(patchFile.write(patch), static_cast<qint64>(patch.size()));
	patchFile.close();

	const QString pluginPath = QDir(QFileInfo(QStringLiteral(SYNTHETIC_PLUGIN_PATH)).absolutePath())
		.absoluteFilePath(QStringLiteral("../plugins/patman"));
	QLibrary patmanLibrary(pluginPath);
	patmanLibrary.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(patmanLibrary.load(), qPrintable(patmanLibrary.errorString()));
	const auto patmanMain = reinterpret_cast<MainFn>(patmanLibrary.resolve("lmms_plugin_main"));
	QVERIFY2(patmanMain != nullptr, qPrintable(patmanLibrary.errorString()));
	auto plugin = std::unique_ptr<Plugin>{patmanMain(nullptr, nullptr)};
	QVERIFY(plugin != nullptr);
	auto* instrument = static_cast<PatmanInstrument*>(plugin.get());

	QVERIFY(QMetaObject::invokeMethod(instrument, "setFile", Qt::DirectConnection,
		Q_ARG(QString, patchFile.fileName()), Q_ARG(bool, false)));
	QCOMPARE(instrument->m_patchSamples.size(), 1);
	const std::weak_ptr<Sample> loadedSample = instrument->m_patchSamples.constFirst();
	QVERIFY(!loadedSample.expired());

	QVERIFY(QMetaObject::invokeMethod(instrument, "setFile", Qt::DirectConnection,
		Q_ARG(QString, QString{}), Q_ARG(bool, false)));

	QVERIFY(instrument->m_patchFile.isEmpty());
	QVERIFY(instrument->m_patchSamples.isEmpty());
	QVERIFY(loadedSample.expired());
}

//! TapTempo must reject intervals that truncate to zero milliseconds.
void AudioPluginTest::subMillisecondTapIntervalDoesNotProduceInfiniteTempo()
{
	using namespace std::chrono_literals;

	TapTempo tapTempo;
	const auto firstTap = TapTempo::clock::time_point{} + 10s;
	const auto secondTap = firstTap + 999us;
	tapTempo.m_intervals = {500ms, 600ms, 700ms};
	tapTempo.m_bpm = 90.0;

	tapTempo.updateTempo(firstTap);
	const auto bpmBeforeShortInterval = tapTempo.m_bpm;
	const auto tapsBeforeShortInterval = tapTempo.m_taps;
	const auto intervalsBeforeShortInterval = tapTempo.m_intervals;
	const auto lastTapBeforeShortInterval = tapTempo.m_lastTap;
	tapTempo.updateTempo(secondTap);

	QCOMPARE(tapTempo.m_bpm, bpmBeforeShortInterval);
	QCOMPARE(tapTempo.m_taps, tapsBeforeShortInterval);
	QCOMPARE(tapTempo.m_intervals[0], intervalsBeforeShortInterval[0]);
	QCOMPARE(tapTempo.m_intervals[1], intervalsBeforeShortInterval[1]);
	QCOMPARE(tapTempo.m_intervals[2], intervalsBeforeShortInterval[2]);
	QCOMPARE(tapTempo.m_lastTap.time_since_epoch().count(), lastTapBeforeShortInterval.time_since_epoch().count());
}

//! Negative warmup counts must be rejected before the harness loads a model.
void AudioPluginTest::namHarnessRejectsNegativeWarmup()
{
	QString harness = QDir::cleanPath(QCoreApplication::applicationDirPath() +
		QStringLiteral("/../plugins/nam_harness"));
#ifdef Q_OS_WIN
	harness += QStringLiteral(".exe");
#endif
	QVERIFY2(QFileInfo::exists(harness), qPrintable(harness));

	QProcess process;
	process.start(harness, {
		QStringLiteral("--warmup"), QStringLiteral("-1"),
		QStringLiteral("--blocks"), QStringLiteral("1"),
		QStringLiteral("missing-negative-warmup-test.nam")
	});
	QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
	QVERIFY2(process.waitForFinished(5000), qPrintable(process.errorString()));
	QCOMPARE(process.exitStatus(), QProcess::NormalExit);
	QCOMPARE(process.exitCode(), 2);

	const QByteArray standardError = process.readAllStandardError();
	QVERIFY2(standardError.contains("error: --warmup must be >= 0"),
		qPrintable(QString::fromLocal8Bit(standardError)));
	QVERIFY(!standardError.contains("LOAD FAILED"));
}

QTEST_MAIN(AudioPluginTest)
#include "AudioPluginTest.moc"
