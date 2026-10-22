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
 * It also loads the real Gate module and checks that a nonfinite input sample
 * does not leave the gate closed after later finite input returns.
 *
 * The plugin module is only ever used through lmms::Effect; the C knobs the
 * module exports are resolved via QLibrary.
 *
 * Windows: a test host cannot load this module at all - an MSVC module's
 * import descriptor names zene.exe, and the test host is not zene.exe - so
 * initTestCase() skips the whole suite there (see tests/QA-GATES.md, Notes).
 * Linux/macOS run every assertion below unchanged.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <cmath>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <limits>
#include <chrono>
#include <memory>
#include <new>
#include <type_traits>
#include <vector>

#include <fftw3.h>
#include <QMutex>
#include <QRgb>
#include <QCoreApplication>
#include <QDir>
#include <QCoreApplication>
#include <QDir>
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

// ExprFront lives in the Xpressive plugin module rather than lmmsobjs. Compile
// its implementation into this test host so the regression exercises the
// production API without adding a separate test target.
#define exprtk_disable_sc_andor
#define exprtk_disable_return_statement
#define exprtk_disable_break_continue
#define exprtk_disable_comments
#define exprtk_disable_string_capabilities
#define exprtk_disable_rtl_io_file
#define exprtk_disable_rtl_vecops
#include "../../../plugins/Xpressive/ExprSynth.cpp"

// Inspect Analyzer's SA_DEBUG-only timer declaration without linking the
// Spectrum Analyzer plugin into this generic AudioPlugin test executable.
#include "../../../plugins/SpectrumAnalyzer/DataprocLauncher.h"
#include "LocklessRingBuffer.h"
#include "../../../plugins/SpectrumAnalyzer/SaControls.h"
#include "../../../plugins/SpectrumAnalyzer/SaProcessor.h"
#define SA_DEBUG
#define private public
#include "../../../plugins/SpectrumAnalyzer/Analyzer.h"
#undef private
#undef SA_DEBUG

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
	const int at = static_cast<int>(offset);  // Qt 5 QByteArray::operator[] takes int/uint; qsizetype is ambiguous
	data[at] = static_cast<char>(value & 0xff);
	data[at + 1] = static_cast<char>(value >> 8);
}

void writeLittleEndian32(QByteArray& data, qsizetype offset, std::uint32_t value)
{
	const int at = static_cast<int>(offset);
	for (int byte = 0; byte < 4; ++byte)
	{
		data[at + byte] = static_cast<char>((value >> (byte * 8)) & 0xff);
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
	const int sampleAt = static_cast<int>(PatchHeaderSize + PatchSampleHeaderSize);  // Qt 5 QByteArray::operator[] takes int/uint
	patch[sampleAt] = 32;
	patch[sampleAt + 1] = -32;
	patch[sampleAt + 2] = 64;
	patch[sampleAt + 3] = -64;
	return patch;
}

template<typename AnalyzerType>
auto analyzerDebugTimerReportsAfter(std::chrono::seconds elapsed) -> bool
{
	using Clock = std::chrono::steady_clock;
	if constexpr (requires(Clock::time_point now) { AnalyzerType::isStatsIntervalElapsed(now, now); })
	{
		const auto last = Clock::time_point{};
		return AnalyzerType::isStatsIntervalElapsed(last + elapsed, last);
	}
	return false;
}

/*!
 * Probes the SA_DEBUG timing collector of the Spectrum Analyzer.
 *
 * The collector is looked up through the Analyzer type, so this helper also
 * compiles on a tree whose Analyzer has no collector at all: the pre-fix code
 * kept the count, the sum and the peak in three separate atomics that the GUI
 * thread reset one after another, so a callback between two of those resets
 * put its count into the next report and its duration into the current one.
 *
 * The measurements are injected, so the probe is deterministic: no clock, no
 * sleep, no thread and no race.
 *
 * Returns an empty string when every snapshot describes exactly one interval,
 * and the reason for the failure otherwise.
 */
template<typename AnalyzerType>
auto analyzerTimingSnapshotProbe() -> QString
{
	if constexpr (!requires { typename AnalyzerType::TimingCollector; })
	{
		return QStringLiteral("Analyzer has no coherent timing snapshot collector");
	}
	else
	{
		using Collector = typename AnalyzerType::TimingCollector;
		using Snapshot = typename Collector::Snapshot;

		Collector collector;
		Snapshot snapshot;

		// The first interval holds two callbacks.
		collector.record(100);
		collector.record(300);
		// The GUI thread asks for the interval that ends now. The callback
		// that lands after the request belongs to the next interval.
		collector.request();
		collector.record(50);

		if (!collector.take(snapshot))
		{
			return QStringLiteral("no snapshot was published for the requested interval");
		}
		if (snapshot.count != 2 || snapshot.sumNs != 400 || snapshot.maxNs != 300)
		{
			return QStringLiteral("the first snapshot mixes intervals: count=%1 sum=%2 peak=%3")
				.arg(static_cast<qulonglong>(snapshot.count))
				.arg(static_cast<qulonglong>(snapshot.sumNs))
				.arg(static_cast<qulonglong>(snapshot.maxNs));
		}

		// The next request closes the interval the late callback started.
		collector.request();
		collector.record(70);
		if (!collector.take(snapshot))
		{
			return QStringLiteral("no snapshot was published for the second interval");
		}
		if (snapshot.count != 1 || snapshot.sumNs != 50 || snapshot.maxNs != 50)
		{
			return QStringLiteral("the second snapshot mixes intervals: count=%1 sum=%2 peak=%3")
				.arg(static_cast<qulonglong>(snapshot.count))
				.arg(static_cast<qulonglong>(snapshot.sumNs))
				.arg(static_cast<qulonglong>(snapshot.maxNs));
		}

		// The third interval holds the callback of 70 ns and two more; the
		// callback that lands after the request starts the fourth interval.
		collector.record(90);
		collector.record(110);
		collector.request();
		collector.record(10);
		if (!collector.take(snapshot))
		{
			return QStringLiteral("no snapshot was published for the third interval");
		}
		if (snapshot.count != 3 || snapshot.sumNs != 270 || snapshot.maxNs != 110)
		{
			return QStringLiteral("the third snapshot mixes intervals: count=%1 sum=%2 peak=%3")
				.arg(static_cast<qulonglong>(snapshot.count))
				.arg(static_cast<qulonglong>(snapshot.sumNs))
				.arg(static_cast<qulonglong>(snapshot.maxNs));
		}

		// The fourth interval holds the callback of 10 ns alone.
		collector.request();
		collector.record(20);
		if (!collector.take(snapshot))
		{
			return QStringLiteral("no snapshot was published for the fourth interval");
		}
		if (snapshot.count != 1 || snapshot.sumNs != 10 || snapshot.maxNs != 10)
		{
			return QStringLiteral("the fourth snapshot mixes intervals: count=%1 sum=%2 peak=%3")
				.arg(static_cast<qulonglong>(snapshot.count))
				.arg(static_cast<qulonglong>(snapshot.sumNs))
				.arg(static_cast<qulonglong>(snapshot.maxNs));
		}

		// Without a request there is nothing to take.
		if (collector.take(snapshot))
		{
			return QStringLiteral("the collector published a snapshot nobody asked for");
		}
		return QString();
	}
}

/*!
 * Probes the SA_DEBUG timing collector of the Spectrum Analyzer for the case
 * that no callback follows the GUI request.
 *
 * The GUI thread asks for the interval that ends now. The baseline reported
 * the accumulated count, sum and peak of that interval even when the audio
 * thread did not run again, so the measurements of an interval must stay
 * reportable until they are taken: a collector that only publishes from the
 * audio callback loses them as soon as the callbacks stop.
 *
 * The measurements are injected, so the probe is deterministic: no clock, no
 * sleep, no thread and no race.
 *
 * Returns an empty string when the measurements stay reportable, and the
 * reason for the failure otherwise.
 */
template<typename AnalyzerType>
auto analyzerTimingNoCallbackAfterRequestProbe() -> QString
{
	if constexpr (!requires { typename AnalyzerType::TimingCollector; })
	{
		return QStringLiteral("Analyzer has no coherent timing snapshot collector");
	}
	else
	{
		using Collector = typename AnalyzerType::TimingCollector;
		using Snapshot = typename Collector::Snapshot;

		Collector collector;
		Snapshot snapshot;

		// Two callbacks are measured, then the GUI thread asks for the
		// interval that ends now.
		collector.record(100);
		collector.record(300);
		collector.request();

		// No callback follows the request: the measurements of the interval
		// must still be reportable.
		if (!collector.take(snapshot))
		{
			return QStringLiteral("the accumulated measurements were lost because no callback followed the request");
		}
		if (snapshot.count != 2 || snapshot.sumNs != 400 || snapshot.maxNs != 300)
		{
			return QStringLiteral("the report of an interval without a following callback is wrong: count=%1 sum=%2 peak=%3")
				.arg(static_cast<qulonglong>(snapshot.count))
				.arg(static_cast<qulonglong>(snapshot.sumNs))
				.arg(static_cast<qulonglong>(snapshot.maxNs));
		}

		// The interval was taken, so the next report has nothing to show.
		if (collector.take(snapshot))
		{
			return QStringLiteral("the collector reported the same interval twice");
		}
		return QString();
	}
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
	void emptyNonInterpolatedCyclicVectorIsRejected();
	void emptyInterpolatedCyclicVectorIsRejected();
	void gateRecoversAfterNanInput();
	void analyzerDebugTimerUsesMonotonicTimePoint();
	void analyzerDebugTimerKeepsStdoutOffAudioThread();
	void analyzerConstructorInitializesTheDebugMembers();
	void analyzerTimingSnapshotStaysCoherent();
	void analyzerTimingSurvivesNoCallbackAfterRequest();
	void analyzerTimingReportUsesOneCoherentSnapshot();

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

void AudioPluginTest::emptyNonInterpolatedCyclicVectorIsRejected()
{
	const float samples[] = {0.0f};
	ExprFront expr("empty(0)", 1);
	QVERIFY(!expr.add_cyclic_vector("empty", samples, 0, false));
}

void AudioPluginTest::emptyInterpolatedCyclicVectorIsRejected()
{
	const float samples[] = {0.0f};
	ExprFront expr("empty(0)", 1);
	QVERIFY(!expr.add_cyclic_vector("empty", samples, 0, true));
}

//! A nonfinite key sample must not poison the Gate's state for later samples.
void AudioPluginTest::gateRecoversAfterNanInput()
{
	const QDir pluginsDir{QDir{QCoreApplication::applicationDirPath()}.absoluteFilePath(
		QStringLiteral("../plugins"))};
	const QFileInfoList gateModules = pluginsDir.entryInfoList(
		{QStringLiteral("gate.*"), QStringLiteral("libgate.*")}, QDir::Files);
	QVERIFY2(!gateModules.isEmpty(), "the built Gate module was not found");

	QLibrary gateLibrary{gateModules.front().absoluteFilePath()};
	gateLibrary.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(gateLibrary.load(), qPrintable(gateLibrary.errorString()));
	const auto gateMain = reinterpret_cast<MainFn>(gateLibrary.resolve("lmms_plugin_main"));
	QVERIFY(gateMain != nullptr);
	const std::unique_ptr<Effect> gate{
		static_cast<Effect*>(gateMain(nullptr, nullptr))};
	QVERIFY(gate != nullptr);

	const f_cnt_t blocks = Engine::audioEngine()->outputSampleRate() / (2 * TestFrames);
	QVERIFY(blocks > 1);
	std::vector<SampleFrame> data(TestFrames, SampleFrame{0.5f, 0.5f});
	const f_cnt_t badFrame = TestFrames / 2;
	data[badFrame][0] = std::numeric_limits<float>::quiet_NaN();
	SampleFrame* channel = data.data();
	AudioBus bus{&channel, 1, TestFrames};
	bus.updateAll();
	QVERIFY(gate->processAudioBuffer(bus));
	for (f_cnt_t block = 1; block < blocks; ++block)
	{
		std::fill(data.begin(), data.end(), SampleFrame{0.5f, 0.5f});
		bus.updateAll();
		QVERIFY(gate->processAudioBuffer(bus));
	}
	QVERIFY2(data.back()[0] > 0.4f && data.back()[1] > 0.4f,
		qPrintable(QStringLiteral("the Gate did not reopen after NaN: L=%1 R=%2")
			.arg(data.back()[0]).arg(data.back()[1])));
}

void AudioPluginTest::analyzerDebugTimerUsesMonotonicTimePoint()
{
	QVERIFY(!analyzerDebugTimerReportsAfter<Analyzer>(std::chrono::seconds{4}));
	QVERIFY(analyzerDebugTimerReportsAfter<Analyzer>(std::chrono::seconds{6}));
	QVERIFY((std::is_same_v<decltype(Analyzer::m_last_dump_time), std::chrono::steady_clock::time_point>));
}

void AudioPluginTest::analyzerDebugTimerKeepsStdoutOffAudioThread()
{
	// processImpl() runs on the audio thread, where std::cout can block, so a debug
	// build must not print from it. The source is read through __FILE__ because this
	// target does not link the Spectrum Analyzer controls that Analyzer needs to construct.
	const QString analyzerPath = QFileInfo(QString::fromUtf8(__FILE__)).dir()
		.filePath(QStringLiteral("../../../plugins/SpectrumAnalyzer/Analyzer.cpp"));
	QFile source(analyzerPath);
	QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(source.errorString()));
	const QString text = QString::fromUtf8(source.readAll());

	const qsizetype begin = text.indexOf(QStringLiteral("ProcessStatus Analyzer::processImpl("));
	QVERIFY(begin >= 0);
	const qsizetype end = text.indexOf(QStringLiteral("\n}"), begin);
	QVERIFY(end > begin);
	const QString callback = text.mid(begin, end - begin);
	QVERIFY2(!callback.contains(QStringLiteral("std::cout")),
		"Analyzer::processImpl() writes to std::cout on the audio thread");
	QVERIFY2(text.contains(QStringLiteral("Analyzer audio thread:")),
		"the SA_DEBUG timing report must still be printed, from the GUI-side reporter");
}

void AudioPluginTest::analyzerConstructorInitializesTheDebugMembers()
{
	// The reported defect: the SA_DEBUG debug members have to be initialized by
	// the constructor, otherwise the first processImpl() audio callback reads an
	// indeterminate dump counter and the GUI-side reporter reads an indeterminate
	// debug timer. The plugin's implementation is not linked into this generic
	// AudioPlugin test host (see the comment above the Analyzer include), so the
	// constructor's initializer list is read from the product source.
	const QString analyzerDir = QFileInfo(QString::fromUtf8(__FILE__)).dir()
		.filePath(QStringLiteral("../../../plugins/SpectrumAnalyzer"));
	QFile source(analyzerDir + QStringLiteral("/Analyzer.cpp"));
	QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(source.errorString()));
	const QByteArray text = source.readAll();
	QFile header(analyzerDir + QStringLiteral("/Analyzer.h"));
	QVERIFY2(header.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(header.errorString()));
	const QByteArray declarations = header.readAll();

	const int ctor = text.indexOf("Analyzer::Analyzer(");
	QVERIFY2(ctor > 0, "the Analyzer constructor was not found in the product source");
	const int body = text.indexOf("m_processorThread.start()", ctor);
	QVERIFY2(body > ctor, "the end of the Analyzer constructor was not found in the product source");
	const QByteArray initializers = text.mid(ctor, body - ctor);
	// Every scalar SA_DEBUG member that still exists must be initialized by the
	// constructor. A member that no longer exists needs no initializer (the
	// timing counters are gone once the coherent collector owns them, and the
	// collector initializes its own storage).
	const bool lastDumpTimeDeclared = declarations.contains("m_last_dump_time");
	QVERIFY2(lastDumpTimeDeclared, "the Analyzer no longer stores the SA_DEBUG report time");
	for (const char* member : {"m_last_dump_time", "m_dump_count", "m_sum_execution_ns", "m_max_execution_ns"})
	{
		if (!declarations.contains(QByteArray{member}))
		{
			continue;
		}
		QVERIFY2(initializers.contains(QByteArray{member} + '('),
			qPrintable(QStringLiteral("the Analyzer constructor does not initialize %1")
				.arg(QString::fromUtf8(member))));
	}
}

void AudioPluginTest::analyzerTimingSnapshotStaysCoherent()
{
	// One report must never mix samples from two intervals: the count, the
	// sum and the peak always describe the same set of callbacks. The
	// measurements are injected, so the probe is deterministic.
	const QString failure = analyzerTimingSnapshotProbe<Analyzer>();
	QVERIFY2(failure.isEmpty(), qPrintable(failure));
}

void AudioPluginTest::analyzerTimingSurvivesNoCallbackAfterRequest()
{
	// The baseline reported the accumulated count, sum and peak of an interval
	// even when the audio thread did not run again, so the measurements of an
	// interval must stay reportable until they are taken. The measurements
	// are injected, so the probe is deterministic.
	const QString failure = analyzerTimingNoCallbackAfterRequestProbe<Analyzer>();
	QVERIFY2(failure.isEmpty(), qPrintable(failure));
}

void AudioPluginTest::analyzerTimingReportUsesOneCoherentSnapshot()
{
	// The reporter must not read (and reset) the count, the sum and the peak
	// one after another: a callback between two of those reads lands in the
	// next report with its duration in the current one. The source is read
	// through __FILE__ because this target does not link the Spectrum
	// Analyzer plugin.
	const QString analyzerPath = QFileInfo(QString::fromUtf8(__FILE__)).dir()
		.filePath(QStringLiteral("../../../plugins/SpectrumAnalyzer/Analyzer.cpp"));
	QFile source(analyzerPath);
	QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(source.errorString()));
	const QString text = QString::fromUtf8(source.readAll());

	QVERIFY2(!text.contains(QStringLiteral(".exchange(0")),
		"Analyzer resets the count, the sum and the peak with separate exchanges, "
		"so one report can mix samples from two intervals");
	QVERIFY2(text.contains(QStringLiteral("m_timing.record(")),
		"the audio callback must record into the coherent timing collector");
	QVERIFY2(text.contains(QStringLiteral("m_timing.request("))
		&& text.contains(QStringLiteral("m_timing.take(")),
		"the GUI reporter must close one interval and take that interval's measurements");
}

QTEST_MAIN(AudioPluginTest)
#include "AudioPluginTest.moc"
