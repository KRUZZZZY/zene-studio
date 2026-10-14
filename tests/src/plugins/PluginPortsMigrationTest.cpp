/*
 * PluginPortsMigrationTest.cpp - Part C (task #585) behaviour preservation
 *
 * Loads the migrated plugin modules built by the normal build and compares
 * their output against PluginPortsMigrationReference, which renders the same
 * input through the verbatim pre-migration (base commit 4ac5c3e38) sources in
 * tests/reference/. Both sides go through the production Effect API
 * (loadSettings + processAudioBuffer(AudioBus&)), so a sample difference can
 * only come from the migration.
 *
 * Windows: no test host can load the migrated modules (an MSVC module's
 * import descriptor names zene.exe - see the note on
 * testHostCanLoadPluginModules()), so every slot in this file skips there
 * rather than failing at the load. Linux/macOS run every assertion below
 * unchanged.
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "PluginPortsHarness.h"

#include <cstddef>
#include <cmath>
#include <cmath>
#include <algorithm>
#include <cmath>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <new>
#include <algorithm>
#include <limits>
#include <cmath>
#include <limits>
#include <vector>
#include <memory>

#include <QByteArray>
#include <QFile>
#include <QDomDocument>
#include <QLibrary>
#include <memory>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

#include "BasicFilters.h"
#include "../../../plugins/Xpressive/Xpressive.h"
#include "Engine.h"
#include "Ladspa2LMMS.h"
#include "DspEffectLibrary.h"
#include "EffectControls.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Plugin.h"
#include "plugins/Xpressive/ExprSynth.h"

// Delay builds Lfo.cpp into a MODULE target, so include its implementation
// here to exercise the constructor directly without adding a new test target.
#include "../../../plugins/Delay/Lfo.cpp"
#include "TempoSyncKnobModel.h"
#include "../../../plugins/Xpressive/ExprSynth.h"

namespace
{

using MainFn = lmms::Plugin* (*)(lmms::Model*, void*);

struct MigratedModule
{
	const char* plugin;
	const char* path;
};

/*! Paths injected by tests/CMakeLists.txt via $<TARGET_FILE:...>. */
auto migratedModules() -> const std::vector<MigratedModule>&
{
	static const std::vector<MigratedModule> modules{
		{"amplifier", PART_C_MIGRATED_amplifier},
		{"bassbooster", PART_C_MIGRATED_bassbooster},
		{"bitcrush", PART_C_MIGRATED_bitcrush},
		{"dualfilter", PART_C_MIGRATED_dualfilter},
		{"waveshaper", PART_C_MIGRATED_waveshaper},
		{"flanger", PART_C_MIGRATED_flanger},
		{"delay", PART_C_MIGRATED_delay},
		{"compressor", PART_C_MIGRATED_compressor},
		{"crossovereq", PART_C_MIGRATED_crossovereq},
		{"dynamicsprocessor", PART_C_MIGRATED_dynamicsprocessor},
		{"lomm", PART_C_MIGRATED_lomm},
		{"multitapecho", PART_C_MIGRATED_multitapecho},
		{"reverbsc", PART_C_MIGRATED_reverbsc},
		{"stereoenhancer", PART_C_MIGRATED_stereoenhancer},
		{"stereomatrix", PART_C_MIGRATED_stereomatrix},
		// Slice 3 (task #589)
		{"dispersion", PART_C_MIGRATED_dispersion},
		{"vectorscope", PART_C_MIGRATED_vectorscope},
		{"analyzer", PART_C_MIGRATED_analyzer},
		{"granularpitchshifter", PART_C_MIGRATED_granularpitchshifter},
		{"eq", PART_C_MIGRATED_eq},
		// Slice 7 (task #589)
		{"frequencyshifter", PART_C_MIGRATED_frequencyshifter},
		{"oscilloscope", PART_C_MIGRATED_oscilloscope},
		{"slewdistortion", PART_C_MIGRATED_slewdistortion},
	};
	return modules;
}

struct MigratedInstrument
{
	const char* plugin;
	const char* path;
};

/*! Instrument paths injected by tests/CMakeLists.txt via $<TARGET_FILE:...>. */
auto migratedInstruments() -> const std::vector<MigratedInstrument>&
{
	static const std::vector<MigratedInstrument> instruments{
		{"freeboy", PART_C_MIGRATED_freeboy},
		{"nes", PART_C_MIGRATED_nes},
		{"sid", PART_C_MIGRATED_sid},
		{"opulenz", PART_C_MIGRATED_opulenz},
		{"sfxr", PART_C_MIGRATED_sfxr},
		{"bitinvader", PART_C_MIGRATED_bitinvader},
		{"watsyn", PART_C_MIGRATED_watsyn},
		{"xpressive", PART_C_MIGRATED_xpressive},
		{"vibedstrings", PART_C_MIGRATED_vibedstrings},
		{"kicker", PART_C_MIGRATED_kicker},
		// Slice 5 (task #589)
		{"tripleoscillator", PART_C_MIGRATED_tripleoscillator},
		{"monstro", PART_C_MIGRATED_monstro},
		{"organic", PART_C_MIGRATED_organic},
		{"audiofileprocessor", PART_C_MIGRATED_audiofileprocessor},
		// Slice 6 (task #589)
		{"lb302", PART_C_MIGRATED_lb302},
		// Slice 8 (task #589): the asset-driven instruments. An entry exists
		// only when its CMake module was added, i.e. when the build has the
		// dependency AND the asset the harness needs to render it.
#ifdef PART_C_MIGRATED_sf2player
		{"sf2player", PART_C_MIGRATED_sf2player},
#endif
#ifdef PART_C_MIGRATED_malletsstk
		{"malletsstk", PART_C_MIGRATED_malletsstk},
#endif
#ifdef PART_C_MIGRATED_gigplayer
		{"gigplayer", PART_C_MIGRATED_gigplayer},
#endif
	};
	return instruments;
}

//! Finds a render by plugin name (the migrated and reference lists are
//! index-aligned, but the negative control looks modules up by name).
auto findRender(const std::vector<partc::PluginRender>& renders, const char* name)
	-> const partc::PluginRender*
{
	for (const auto& r : renders)
	{
		if (r.name == name)
		{
			return &r;
		}
	}
	return nullptr;
}

/*!
 * Windows: a test host cannot load a plugin MODULE library at runtime.
 *
 * Every plugin module links the zene executable, so an MSVC module's import
 * descriptor names zene.exe; the Windows loader then fails with
 * ERROR_MOD_NOT_FOUND (126) because the test host is not zene.exe. CI
 * (msvc-x64, QT_FORCE_STDERR_LOGGING=1) shows the loader error verbatim:
 *   QWARN : ... Cannot load library ...\plugins\tripleoscillator.dll:
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

//! What every Windows skip in this suite says: the proven mechanism, named once.
constexpr const char* PluginModuleHostSkipMessage =
	"a Windows test host cannot load the migrated plugin modules: plugin modules link the "
	"zene executable, so on Windows their import descriptor names zene.exe and a test host "
	"cannot satisfy it; the product loads them inside zene.exe where that resolves by "
	"construction (CI msvc-x64: QLibrary::load -> ERROR_MOD_NOT_FOUND, 126)";

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
lmms::WaveSample* constructWaveSampleInPlace(unsigned char* storage)
{
	return ::new (storage) lmms::WaveSample{1};
}

/* Construct from the graphModel path without exposing the caller's seeded bytes. */
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
lmms::WaveSample* constructWaveSampleInPlace(unsigned char* storage, const lmms::graphModel* graph)
{
	return ::new (storage) lmms::WaveSample{graph};
}

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
bool sameObjectRepresentation(const unsigned char* actual, const unsigned char* expected,
	std::size_t size)
{
	for (std::size_t i = 0; i < size; ++i)
	{
		if (actual[i] != expected[i])
		{
			return false;
		}
	}
	return true;
}

struct MonstroRenderResult
{
	std::vector<float> samples;
	QString error;
};

auto renderMonstroLfoCase(bool modulationEnabled) -> MonstroRenderResult
{
	QLibrary lib{QString::fromUtf8(PART_C_MIGRATED_monstro)};
	lib.setLoadHints(QLibrary::PreventUnloadHint);
	if (!lib.load()) { return {{}, lib.errorString()}; }

	auto entry = reinterpret_cast<MainFn>(lib.resolve("lmms_plugin_main"));
	if (entry == nullptr) { return {{}, QStringLiteral("Monstro entry point not found")}; }

	partc::prepareInstrumentEnvironment("monstro");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* inst = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	if (inst == nullptr) { return {{}, QStringLiteral("Monstro instrument creation failed")}; }

	QDomDocument doc;
	QDomElement root = doc.createElement("instrument");
	QDomElement saved = inst->saveState(doc, root);
	saved.setAttribute(QStringLiteral("l1wav"), QStringLiteral("4"));
	saved.setAttribute(QStringLiteral("l1att"), QStringLiteral("20"));
	saved.setAttribute(QStringLiteral("l1rat"), QStringLiteral("10000"));
	saved.setAttribute(QStringLiteral("l1phs"), QStringLiteral("0"));
	saved.setAttribute(QStringLiteral("v1l1"), modulationEnabled ? QStringLiteral("1") : QStringLiteral("0"));
	inst->restoreState(saved);

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	auto samples = partc::renderInstrumentBuffers(*inst, frames, lmms::DefaultKey);
	delete inst;
	return {std::move(samples), {}};
}

auto maxAbsDiffInRange(const std::vector<float>& a, const std::vector<float>& b,
						std::size_t begin, std::size_t end) -> float
{
	float worst = 0.0f;
	for (std::size_t i = begin; i < end; ++i)
	{
		worst = std::max(worst, std::fabs(a[i] - b[i]));
	}
	return worst;
}

struct MonstroParityEvidence
{
	float attackDifference;
	float outsideAttackDifference;
	std::size_t firstOutsideDifferenceIndex;
};

auto compareMonstroAttackWindows(const std::vector<float>& migrated,
									const std::vector<float>& reference,
									std::size_t framesPerPeriod,
									std::size_t attackFrames) -> MonstroParityEvidence
{
	const std::size_t samplesPerPeriod = framesPerPeriod * 2;
	const std::size_t attackValues = std::min(attackFrames * 2, samplesPerPeriod);
	MonstroParityEvidence evidence{0.0f, 0.0f, migrated.size()};
	for (std::size_t periodStart = 0; periodStart < migrated.size(); periodStart += samplesPerPeriod)
	{
		const std::size_t periodEnd = periodStart + samplesPerPeriod;
		const std::size_t attackEnd = periodStart + attackValues;
		evidence.attackDifference = std::max(evidence.attackDifference,
			maxAbsDiffInRange(migrated, reference, periodStart, attackEnd));
		evidence.outsideAttackDifference = std::max(evidence.outsideAttackDifference,
			maxAbsDiffInRange(migrated, reference, attackEnd, periodEnd));
		for (std::size_t i = attackEnd; i < periodEnd; ++i)
		{
			if (migrated[i] != reference[i])
			{
				evidence.firstOutsideDifferenceIndex = std::min(evidence.firstOutsideDifferenceIndex, i);
			}
		}
	}
	return evidence;
}

} // namespace

class PluginPortsMigrationTest : public QObject
{
	Q_OBJECT

public:
	~PluginPortsMigrationTest() override
	{
		for (auto* lib : m_libraries)
		{
			delete lib;
		}
	}

private slots:
	void initTestCase();
	void waveSampleDefaultsInterpolationToDisabled();
	void gigPlayerAcceptsMidiNoteZero();
	void lfoStartsFromInitializedState();
	void delayLfoControlDescribesItsPeriod();
	void doubleFilterClearHistoryClearsSubfilter();
	void xpressiveLoadedWaveSamplesAreFinite();
	void positiveFractionHandlesVeryLargePositiveValues();
	void positiveFractionHandlesVeryLargeNegativeValues();
	void positiveFractionPreservesOrdinaryValues();
	void migratedPluginsPreserveBehaviour();
	void monstroLfoAttackRampsWithinPeriod();
	void comparisonIsSensitive();
	void nonRealtimeLadspaEffectsAreInvalid();
	void slice5InstrumentsAreLiveAndDistinct();
	void organicDistortionLeavesPreOffsetFramesUnchanged();
	void slice6PluginsAreLiveAndExact();
	void slice7PluginsAreLiveAndExact();
	void slice8PluginsAreLiveAndExact();
	void fastBassBoostFrequencySetterHonoursFloor();
	void xpressiveRandomVectorRejectsOutOfRangeFloatInputs();

private:
	std::vector<QLibrary*> m_libraries;
	std::vector<partc::PluginRender> m_migrated;
	std::vector<partc::PluginRender> m_reference;
	std::vector<partc::LadspaRender> m_ladspa;
};

void PluginPortsMigrationTest::initTestCase()
{
	// Slice 6 (task #589): LadspaManager reads LADSPA_PATH when it is first
	// constructed, so it has to be set before Engine::init(). The reference
	// renderer is spawned as a child process and inherits this environment.
	qputenv("LADSPA_PATH", QByteArray{PART_C_LADSPA_DIR});

	lmms::Engine::init(true);
	lmms::NotePlayHandleManager::init();
	QVERIFY2(lmms::Engine::audioEngine() != nullptr, "engine failed to initialise");
	lmms::Engine::audioEngine()->audioDev()->stopProcessing();

	const lmms::f_cnt_t frames = lmms::Engine::audioEngine()->framesPerPeriod();
	lmms::NotePlayHandleManager::init();
	qInfo("engine: sample rate %d, framesPerPeriod %d, buffers per plugin %d",
		static_cast<int>(lmms::Engine::audioEngine()->outputSampleRate()), static_cast<int>(frames), partc::Buffers);
	QVERIFY(frames > 0);
}

void PluginPortsMigrationTest::waveSampleDefaultsInterpolationToDisabled()
{
	alignas(lmms::WaveSample) unsigned char storage[sizeof(lmms::WaveSample)]{};
	const bool initialInterpolation = true;
	const bool disabledInterpolation = false;
	lmms::graphModel graph{-1.0f, 1.0f, 1, nullptr, true};
	const auto checkDefault = [&](lmms::WaveSample* wave) {
		const bool isDisabled = sameObjectRepresentation(
			storage + offsetof(lmms::WaveSample, m_interpolate),
			reinterpret_cast<const unsigned char*>(&disabledInterpolation), sizeof(bool));
		wave->~WaveSample();
		return isDisabled;
	};

	std::memcpy(storage + offsetof(lmms::WaveSample, m_interpolate),
		&initialInterpolation, sizeof(initialInterpolation));
	auto* wave = constructWaveSampleInPlace(storage);
	const bool integerConstructorDefault = checkDefault(wave);

	std::memcpy(storage + offsetof(lmms::WaveSample, m_interpolate),
		&initialInterpolation, sizeof(initialInterpolation));
	wave = constructWaveSampleInPlace(storage, &graph);
	const bool graphConstructorDefault = checkDefault(wave);

	QVERIFY2(integerConstructorDefault && graphConstructorDefault,
		"Both WaveSample constructors must default interpolation to disabled");
}

void PluginPortsMigrationTest::gigPlayerAcceptsMidiNoteZero()
{
#ifndef PART_C_MIGRATED_gigplayer
	QSKIP("GigPlayer requires libgig and the vendored .gig test instrument");
#else
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_gigplayer)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);

	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	partc::prepareInstrumentEnvironment("gigplayer");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);
	partc::applyInstrumentTestSettings(*instrument, "gigplayer");

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames)}, lmms::TimePos{}, 0};
	auto* noteHandle = lmms::NotePlayHandleManager::acquire(track.get(), 0, frames, note);
	std::vector<lmms::SampleFrame> output(static_cast<std::size_t>(frames));
	instrument->playNote(noteHandle, std::span<lmms::SampleFrame>{output});
	const bool accepted = noteHandle->m_pluginData != nullptr;
	if (accepted)
	{
		instrument->deleteNotePluginData(noteHandle);
		noteHandle->m_pluginData = nullptr;
	}
	lmms::NotePlayHandleManager::release(noteHandle);
	delete instrument;

	QVERIFY2(accepted, "GigPlayer did not register MIDI note 0");
#endif
}

void PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid()
{
	auto* manager = lmms::Engine::getLADSPAManager();
	const lmms::ladspa_key_t key{"decimator_1202.so", "decimator"};
	const auto* description = manager->getDescription(key);
	QVERIFY2(description != nullptr, "SWH Decimator test fixture was not loaded");
	QVERIFY(description->type == lmms::LadspaPluginType::Transfer);
	QVERIFY(description->inputChannels == 1 && description->outputChannels == 1);
	QVERIFY(!manager->isRealTimeCapable(key));

	const auto entry = qMakePair(manager->getName(key), key);
	QVERIFY2(!manager->getValidEffects().contains(entry),
		"non-real-time LADSPA plugin was classified as a valid effect");
	QVERIFY2(manager->getInvalidEffects().contains(entry),
		"non-real-time LADSPA plugin was not classified as invalid");
}

void PluginPortsMigrationTest::lfoStartsFromInitializedState()
{
	const auto alignment = std::align_val_t{alignof(lmms::Lfo)};
	void* storage = ::operator new(sizeof(lmms::Lfo), alignment);
	auto* poisonedStorage = static_cast<volatile unsigned char*>(storage);
	for (std::size_t byte = 0; byte < sizeof(lmms::Lfo); ++byte)
	{
		poisonedStorage[byte] = 0xFF;
	}
	auto* lfo = new (storage) lmms::Lfo{44100};
	lfo->setFrequency(2.0);
	const float firstOutput = lfo->tick();
	lfo->~Lfo();
	::operator delete(storage, alignment);

	QVERIFY(std::isfinite(firstOutput));
	QCOMPARE(firstOutput, 0.0f);
}

void PluginPortsMigrationTest::fastBassBoostFrequencySetterHonoursFloor()
{
	lmms::DspEffectLibrary::FastBassBoost updated{80.0f, 1.0f, 2.8f};
	lmms::DspEffectLibrary::FastBassBoost floor{10.0f, 1.0f, 2.8f};
	updated.setFrequency(-1.0f);

	for (const float input : {0.25f, -0.5f, 0.125f})
	{
		const float actual = updated.nextSample(input);
		const float expected = floor.nextSample(input);
		QVERIFY(std::isfinite(actual));
		QCOMPARE(actual, expected);
	}
}

void PluginPortsMigrationTest::delayLfoControlDescribesItsPeriod()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_delay)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);

	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);
	std::unique_ptr<lmms::Effect> effect{static_cast<lmms::Effect*>(entry(nullptr, nullptr))};
	QVERIFY(effect != nullptr);

	const auto models = effect->controls()->findChildren<lmms::TempoSyncKnobModel*>();
	const auto lfoModel = std::find_if(models.cbegin(), models.cend(), [](const auto* model)
	{
		return model->value() == 2.f;
	});
	QVERIFY(lfoModel != models.cend());
	QCOMPARE((*lfoModel)->displayName(), QStringLiteral("LFO period"));

	QFile dialogSource{QCoreApplication::applicationDirPath()
		+ QStringLiteral("/../../plugins/Delay/DelayControlsDialog.cpp")};
	QVERIFY2(dialogSource.open(QIODevice::ReadOnly), qPrintable(dialogSource.errorString()));
	const auto dialogCode = dialogSource.readAll();
	QVERIFY(dialogCode.contains("tr(\"TIME\")"));
	QVERIFY(dialogCode.contains("setHintText(tr(\"LFO period:\"), \" s\")"));
}

void PluginPortsMigrationTest::doubleFilterClearHistoryClearsSubfilter()
{
	lmms::BasicFilters<1> filter{static_cast<lmms::sample_rate_t>(48000)};
	filter.setFilterType(lmms::BasicFilters<1>::FilterType::DoubleLowPass);
	filter.calcFilterCoeffs(1000.0f, 0.7f);
	QVERIFY(filter.update(1.0f, 0) != 0.0f);

	filter.clearHistory();
	QCOMPARE(filter.update(0.0f, 0), 0.0f);
}

void PluginPortsMigrationTest::xpressiveLoadedWaveSamplesAreFinite()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_xpressive)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);
	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	std::unique_ptr<lmms::Instrument> instrument{
		static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
	QVERIFY(instrument != nullptr);

	constexpr std::size_t SampleCount = 4096;
	auto encodeSamples = [](const std::vector<float>& samples) {
		const QByteArray bytes{reinterpret_cast<const char*>(samples.data()),
			static_cast<int>(samples.size() * sizeof(float))};
		return QString::fromLatin1(bytes.toBase64());
	};
	const std::vector<float> nanSamples(SampleCount, std::numeric_limits<float>::quiet_NaN());
	const std::vector<float> positiveInfinitySamples(SampleCount,
		std::numeric_limits<float>::infinity());
	const std::vector<float> negativeInfinitySamples(SampleCount,
		-std::numeric_limits<float>::infinity());
	QDomDocument doc;
	QDomElement settings = doc.createElement("instrument");
	settings.setAttribute("W1sample", encodeSamples(nanSamples));
	settings.setAttribute("W2sample", encodeSamples(positiveInfinitySamples));
	settings.setAttribute("W3sample", encodeSamples(negativeInfinitySamples));
	instrument->restoreState(settings);

	auto* xpressive = reinterpret_cast<lmms::Xpressive*>(instrument.get());
	const auto waveIsFinite = [](const lmms::WaveSample& wave) {
		return std::all_of(wave.m_samples, wave.m_samples + wave.m_length,
			[](float sample) { return std::isfinite(sample); });
	};
	QVERIFY2(waveIsFinite(xpressive->W1()), "W1 waveform retained a saved NaN sample");
	QVERIFY2(waveIsFinite(xpressive->W2()), "W2 waveform retained a saved infinite sample");
	QVERIFY2(waveIsFinite(xpressive->W3()), "W3 waveform retained a saved infinite sample");
}

void PluginPortsMigrationTest::xpressiveRandomVectorRejectsOutOfRangeFloatInputs()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QLibrary library{QString::fromUtf8(PART_C_MIGRATED_xpressive)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	partc::prepareInstrumentEnvironment("xpressive");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	std::unique_ptr<lmms::Instrument> instrument{
		static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
	QVERIFY(instrument != nullptr);

	QDomDocument document;
	QDomElement root = document.createElement("instrument");
	QDomElement settings = instrument->saveState(document, root);
	const auto renderExpression = [&](const QString& expression)
	{
		settings.setAttribute("O1", expression);
		settings.setAttribute("O2", "0");
		instrument->restoreState(settings);
		return partc::renderInstrumentInFreshThread(*instrument,
			lmms::Engine::audioEngine()->framesPerPeriod(), lmms::DefaultKey);
	};

	const auto outOfRangeIndex = renderExpression("randsv(65536*65536,0)");
	const auto zeroExpression = renderExpression("0");
	const bool indexFallsBackToZero = !outOfRangeIndex.empty()
		&& std::all_of(outOfRangeIndex.begin(), outOfRangeIndex.end(),
			[](const float sample) { return sample == 0.0f; });
	const bool zeroExpressionIsZero = !zeroExpression.empty()
		&& std::all_of(zeroExpression.begin(), zeroExpression.end(),
			[](const float sample) { return sample == 0.0f; });

	const auto zeroSeed = renderExpression("randsv(0,0)");
	const auto outOfRangeSeed = renderExpression("randsv(0,32768*65536)");
	const bool seedFallsBackToZero = outOfRangeSeed.size() == zeroSeed.size()
		&& std::equal(outOfRangeSeed.begin(), outOfRangeSeed.end(), zeroSeed.begin());
	const auto peak = [](const std::vector<float>& samples)
	{
		float result = 0.0f;
		for (const float sample : samples)
		{
			result = std::max(result, std::fabs(sample));
		}
		return result;
	};
	QVERIFY2(indexFallsBackToZero && zeroExpressionIsZero && seedFallsBackToZero,
		qPrintable(QString{"index fallback: %1 (peak %2); zero expression: %3; seed fallback: %4 "
			"(peaks %5, %6; delta %7)"}
			.arg(indexFallsBackToZero).arg(peak(outOfRangeIndex)).arg(zeroExpressionIsZero)
			.arg(seedFallsBackToZero).arg(peak(zeroSeed)).arg(peak(outOfRangeSeed))
			.arg(partc::maxAbsDiff(zeroSeed, outOfRangeSeed))));
}

void PluginPortsMigrationTest::positiveFractionHandlesVeryLargePositiveValues()
{
	const float result = lmms::positiveFraction(std::numeric_limits<float>::max());
	QVERIFY(std::isfinite(result));
	QVERIFY(result >= 0.0f);
	QVERIFY(result < 1.0f);
}

void PluginPortsMigrationTest::positiveFractionHandlesVeryLargeNegativeValues()
{
	const float result = lmms::positiveFraction(-std::numeric_limits<float>::max());
	QVERIFY(std::isfinite(result));
	QVERIFY(result >= 0.0f);
	QVERIFY(result < 1.0f);
}

void PluginPortsMigrationTest::positiveFractionPreservesOrdinaryValues()
{
	QCOMPARE(lmms::positiveFraction(1.25f), 0.25f);
	QCOMPARE(lmms::positiveFraction(-0.25f), 0.75f);
	QCOMPARE(lmms::positiveFraction(-2.0f), 0.0f);
}

void PluginPortsMigrationTest::migratedPluginsPreserveBehaviour()
{
	// The QLibrary loads below are exactly what a Windows test host cannot do
	// (see the note on testHostCanLoadPluginModules()): skip at the top so the
	// failure mode is a named skip, not one loader error per module.
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	const lmms::f_cnt_t frames = lmms::Engine::audioEngine()->framesPerPeriod();

	// 1. Render the migrated plugins through the real plugin entry point.
	for (const auto& m : migratedModules())
	{
		auto* lib = new QLibrary{QString::fromUtf8(m.path)};
		lib->setLoadHints(QLibrary::PreventUnloadHint);
		QVERIFY2(lib->load(), qPrintable(QString{"%1: %2"}.arg(m.plugin, lib->errorString())));
		m_libraries.push_back(lib);

		auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
		QVERIFY2(entry != nullptr, m.plugin);

		auto* fx = static_cast<lmms::Effect*>(entry(nullptr, nullptr));
		QVERIFY2(fx != nullptr, m.plugin);

		partc::applyTestSettings(*fx, m.plugin);

		partc::PluginRender render;
		render.name = QString::fromUtf8(m.plugin);
		render.samples = partc::renderInFreshThread(*fx, frames);
		render.checksum = partc::checksum(render.samples);
		m_migrated.push_back(std::move(render));

		delete fx;
	}

	// 1b. Render the migrated instruments through the real plugin entry point.
	//     Instruments are parented to an InstrumentTrack, exactly like the
	//     engine creates them, and settings go through the instrument's own
	//     saveState()/restoreState() round trip.
	for (const auto& m : migratedInstruments())
	{
		auto* lib = new QLibrary{QString::fromUtf8(m.path)};
		lib->setLoadHints(QLibrary::PreventUnloadHint);
		QVERIFY2(lib->load(), qPrintable(QString{"%1: %2"}.arg(m.plugin, lib->errorString())));
		m_libraries.push_back(lib);

		auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
		QVERIFY2(entry != nullptr, m.plugin);

		partc::prepareInstrumentEnvironment(m.plugin);
		auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
		auto* inst = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
		QVERIFY2(inst != nullptr, m.plugin);

		partc::applyInstrumentTestSettings(*inst, m.plugin);

		partc::PluginRender render;
		render.name = QString::fromUtf8(m.plugin);
		render.samples = partc::renderInstrumentInFreshThread(*inst, frames, lmms::DefaultKey);
		render.checksum = partc::checksum(render.samples);
		m_migrated.push_back(std::move(render));

		delete inst;
	}

	// 1c. Render the migrated LADSPA-hosted effects (slice 6, task #589).
	//     LadspaEffect needs its sub-plugin Key, so these go through
	//     renderLadspa() instead of the plain entry point above.
	for (const auto& spec : partc::ladspaSpecs())
	{
		auto result = partc::renderLadspa(PART_C_MIGRATED_ladspaeffect, spec, frames);
		QVERIFY2(result.loaded, qPrintable(result.error));
		QVERIFY2(result.effectOkay,
			qPrintable(QString{"%1: LadspaEffect did not instantiate the LADSPA plugin "
				"(LADSPA_PATH=%2)"}.arg(result.name, QString::fromUtf8(PART_C_LADSPA_DIR))));

		partc::PluginRender render;
		render.name = result.name;
		render.samples = result.samples;
		render.checksum = result.checksum;
		m_migrated.push_back(std::move(render));
		m_ladspa.push_back(std::move(result));
	}

	// 1d. Render the migrated LV2 host plugins (slice 8, task #589). Both
	//     resolve their DSP from the Key's "uri" attribute, so they go through
	//     the keyed render helpers instead of the plain entry point.
#ifdef PART_C_MIGRATED_lv2effect
	for (const auto& spec : partc::lv2Specs())
	{
		if (!partc::lv2BundleProvides(spec.uri))
		{
			// Optional fixture (mda-lv2) not installed here: skip it on BOTH sides —
			// the reference renderer asks the same question — so the two render lists
			// stay comparable instead of failing on a missing package.
			continue;
		}
		if (QString::fromUtf8(spec.plugin) == QLatin1String("lv2effect"))
		{
			auto result = partc::renderKeyedEffect(PART_C_MIGRATED_lv2effect, spec, frames);
			QVERIFY2(result.loaded, qPrintable(result.error));
			QVERIFY2(result.okay, qPrintable(QString{"%1: Lv2Effect did not instantiate %2"}
				.arg(result.render.name, QString::fromUtf8(spec.uri))));
			m_migrated.push_back(std::move(result.render));
		}
#ifdef PART_C_MIGRATED_lv2instrument
		else
		{
			auto result = partc::renderKeyedInstrument(PART_C_MIGRATED_lv2instrument, spec, frames,
				lmms::DefaultKey);
			QVERIFY2(result.loaded, qPrintable(result.error));
			QVERIFY2(result.okay, qPrintable(QString{"%1: Lv2Instrument did not instantiate %2"}
				.arg(result.render.name, QString::fromUtf8(spec.uri))));
			m_migrated.push_back(std::move(result.render));
		}
#endif
	}
#endif

	// 2. Render the pre-migration reference sources in a separate process.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString referencePath = tmp.filePath("reference-renders.bin");

	QProcess reference;
	reference.start(QString::fromUtf8(PART_C_REFERENCE_EXE), {referencePath});
	QVERIFY(reference.waitForStarted(30000));
	QVERIFY(reference.waitForFinished(300000));
	const QByteArray referenceOut = reference.readAllStandardOutput();
	const QByteArray referenceErr = reference.readAllStandardError();
	if (!referenceOut.isEmpty())
	{
		qInfo("reference renderer stdout:\n%s", referenceOut.constData());
	}
	if (!referenceErr.isEmpty())
	{
		qWarning("reference renderer stderr:\n%s", referenceErr.constData());
	}
	QCOMPARE(reference.exitStatus(), QProcess::NormalExit);
	QCOMPARE(reference.exitCode(), 0);

	m_reference = partc::readRenders(referencePath);
	QCOMPARE(m_reference.size(), m_migrated.size());

	// 3. Compare sample-exactly and report the evidence per plugin.
	for (std::size_t i = 0; i < m_migrated.size(); ++i)
	{
		const auto& migrated = m_migrated[i];
		const auto& referenceRender = m_reference[i];

		QCOMPARE(referenceRender.name, migrated.name);
		QVERIFY2(migrated.samples.size() == referenceRender.samples.size(),
			qPrintable(migrated.name));

		const float worst = partc::maxAbsDiff(migrated.samples, referenceRender.samples);

		qInfo().noquote()
			<< QString{"%1: %2 samples | first samples migrated=[%3, %4] reference=[%5, %6] "
					   "| sha256 migrated=%7 reference=%8 | max|delta|=%9"}
				   .arg(migrated.name)
				   .arg(migrated.samples.size())
				   .arg(migrated.samples[0], 0, 'g', 9)
				   .arg(migrated.samples[1], 0, 'g', 9)
				   .arg(referenceRender.samples[0], 0, 'g', 9)
				   .arg(referenceRender.samples[1], 0, 'g', 9)
				   .arg(migrated.checksum, referenceRender.checksum)
				   .arg(worst, 0, 'g', 3);

		if (migrated.name == QLatin1String("monstro"))
		{
			// The test settings give LFO 1 a 0.1 ms attack and LFO 2 a 0.2 ms
			// attack; both contribute to oscillator volume modulation.
			const auto attackFrames = static_cast<std::size_t>(
				std::ceil(0.2f * lmms::Engine::audioEngine()->outputSampleRate() / 1000.0f));
			const auto evidence = compareMonstroAttackWindows(migrated.samples,
				referenceRender.samples,
				static_cast<std::size_t>(lmms::Engine::audioEngine()->framesPerPeriod()), attackFrames);
			QVERIFY(attackFrames > 0);
			QVERIFY2(evidence.attackDifference > 1e-5f,
				"Monstro's corrected LFO attack did not change samples within its attack window");
			QVERIFY2(evidence.outsideAttackDifference == 0.0f,
				qPrintable(QString{"Monstro differs outside attack windows: attackFrames=%1 firstMismatch=%2 max|delta|=%3"}
					.arg(attackFrames)
					.arg(evidence.firstOutsideDifferenceIndex)
					.arg(evidence.outsideAttackDifference, 0, 'g', 3)));
		}
		else
		{
			QVERIFY2(migrated.checksum == referenceRender.checksum,
				qPrintable(QString{"%1: migrated render differs from the pre-migration reference "
							   "(max|delta|=%2)"}
							   .arg(migrated.name)
							   .arg(worst, 0, 'g', 3)));
		}
	}

	// 4. Migrated but not provable in this build: report each gap explicitly
	//    so it can never be mistaken for a pass.
	for (const auto& gap : partc::unprovenGaps())
	{
		qWarning().noquote() << QString{"UNPROVEN %1: %2"}
			.arg(QString::fromUtf8(gap.plugin), QString::fromUtf8(gap.reason));
	}
}

void PluginPortsMigrationTest::monstroLfoAttackRampsWithinPeriod()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	const auto withoutModulation = renderMonstroLfoCase(false);
	QVERIFY2(withoutModulation.error.isEmpty(), qPrintable(withoutModulation.error));
	const auto withAttack = renderMonstroLfoCase(true);
	QVERIFY2(withAttack.error.isEmpty(), qPrintable(withAttack.error));

	const auto firstPeriodSampleCount = static_cast<std::size_t>(
		lmms::Engine::audioEngine()->framesPerPeriod()) * 2;
	QVERIFY(withoutModulation.samples.size() >= firstPeriodSampleCount);
	QVERIFY(withAttack.samples.size() >= firstPeriodSampleCount);

	const std::vector<float> baselineFirstPeriod{withoutModulation.samples.begin(),
		withoutModulation.samples.begin() + static_cast<std::ptrdiff_t>(firstPeriodSampleCount)};
	const std::vector<float> attackedFirstPeriod{withAttack.samples.begin(),
		withAttack.samples.begin() + static_cast<std::ptrdiff_t>(firstPeriodSampleCount)};
	float baselinePeak = 0.0f;
	for (const float sample : baselineFirstPeriod)
	{
		baselinePeak = std::max(baselinePeak, std::fabs(sample));
	}
	QVERIFY2(baselinePeak > 1e-4f, "Monstro baseline rendered silence");

	const float firstPeriodDifference = partc::maxAbsDiff(attackedFirstPeriod, baselineFirstPeriod);
	QVERIFY2(firstPeriodDifference > 1e-5f,
		qPrintable(QString{"Monstro LFO attack did not ramp within the first period (max|delta|=%1)"}
			.arg(firstPeriodDifference, 0, 'g', 3)));
}

/*!
 * Negative control: the comparison above must be able to fail. Comparing two
 * *different* plugins' renders has to produce different checksums; otherwise
 * the harness would be comparing silence with silence and prove nothing.
 */
void PluginPortsMigrationTest::comparisonIsSensitive()
{
	// Consumes the module renders from migratedPluginsPreserveBehaviour(),
	// which a Windows test host cannot produce (see testHostCanLoadPluginModules()).
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QVERIFY(m_migrated.size() >= 2);
	QVERIFY(m_reference.size() == m_migrated.size());

	const auto& a = m_migrated[0];
	const auto& b = m_reference[1];
	const float worst = partc::maxAbsDiff(a.samples, b.samples);

	qInfo().noquote()
		<< QString{"negative control: %1 (migrated) vs %2 (reference) max|delta|=%3"}
			   .arg(a.name, b.name)
			   .arg(worst, 0, 'g', 3);

	QVERIFY2(a.checksum != b.checksum, "distinct plugins rendered identically");
	QVERIFY2(worst > 1e-6f, "comparison is not sensitive to real differences");
}

/*!
 * Slice 5 (task #589) negative control: the four newly migrated instruments
 * must each render audible, pairwise-distinct output, so the sample-exact
 * comparison above is demonstrably comparing real DSP and not silence.
 */
void PluginPortsMigrationTest::slice5InstrumentsAreLiveAndDistinct()
{
	// Consumes the module renders from migratedPluginsPreserveBehaviour(),
	// which a Windows test host cannot produce (see testHostCanLoadPluginModules()).
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	static const std::array<const char*, 4> slice5{
		"tripleoscillator", "monstro", "organic", "audiofileprocessor"};

	for (const char* name : slice5)
	{
		const auto* migrated = findRender(m_migrated, name);
		QVERIFY2(migrated != nullptr, name);

		float peak = 0.0f;
		for (const float s : migrated->samples)
		{
			peak = std::max(peak, std::fabs(s));
		}
		qInfo().noquote() << QString{"slice 5 negative control: %1 peak=%2 (audible)"}
			.arg(name).arg(peak, 0, 'g', 4);
		QVERIFY2(peak > 1e-4f, qPrintable(QString{"%1 rendered silence"}.arg(name)));
	}

	// Pairwise distinctness: every pair of the four new instruments must
	// differ, so the comparator is live on the new modules too.
	for (std::size_t i = 0; i < slice5.size(); ++i)
	{
		for (std::size_t j = i + 1; j < slice5.size(); ++j)
		{
			const auto* a = findRender(m_migrated, slice5[i]);
			const auto* b = findRender(m_reference, slice5[j]);
			QVERIFY(a != nullptr && b != nullptr);
			const float worst = partc::maxAbsDiff(a->samples, b->samples);
			qInfo().noquote()
				<< QString{"slice 5 negative control: %1 (migrated) vs %2 (reference) max|delta|=%3"}
					   .arg(QString::fromUtf8(slice5[i]), QString::fromUtf8(slice5[j]))
					   .arg(worst, 0, 'g', 3);
			QVERIFY2(a->checksum != b->checksum,
				qPrintable(QString{"%1 and %2 rendered identically"}
					.arg(QString::fromUtf8(slice5[i]), QString::fromUtf8(slice5[j]))));
			QVERIFY2(worst > 1e-6f,
				qPrintable(QString{"%1 vs %2: comparison not sensitive"}
					.arg(QString::fromUtf8(slice5[i]), QString::fromUtf8(slice5[j]))));
		}
	}
}

void PluginPortsMigrationTest::organicDistortionLeavesPreOffsetFramesUnchanged()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QLibrary library{PART_C_MIGRATED_organic};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	partc::prepareInstrumentEnvironment("organic");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	std::unique_ptr<lmms::Instrument> instrument{
		static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
	QVERIFY(instrument != nullptr);

	QDomDocument doc;
	QDomElement root = doc.createElement("instrument");
	QDomElement state = instrument->saveState(doc, root);
	state.setAttribute(QStringLiteral("foldback"), QStringLiteral("0.5"));
	instrument->restoreState(state);

	const lmms::f_cnt_t frames = lmms::Engine::audioEngine()->framesPerPeriod();
	const lmms::f_cnt_t offset = frames / 2;
	QVERIFY(offset > 0);
	constexpr float previousSample = 0.75f;
	std::vector<lmms::SampleFrame> output(static_cast<std::size_t>(frames),
		lmms::SampleFrame{previousSample, previousSample});
	const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames * 2)},
		lmms::TimePos{0}, lmms::DefaultKey};

	lmms::NotePlayHandleManager::init();
	auto* handle = lmms::NotePlayHandleManager::acquire(track.get(), offset, frames * 2, note);
	instrument->playNote(handle, std::span<lmms::SampleFrame>{output});
	instrument->deleteNotePluginData(handle);
	handle->m_pluginData = nullptr;
	lmms::NotePlayHandleManager::release(handle);
	lmms::NotePlayHandleManager::free();

	for (lmms::f_cnt_t frame = 0; frame < offset; ++frame)
	{
		QCOMPARE(output[static_cast<std::size_t>(frame)].left(), previousSample);
		QCOMPARE(output[static_cast<std::size_t>(frame)].right(), previousSample);
	}
}

/*!
 * Slice 6 (task #589) negative controls and state round trip.
 *
 * The sample-exact comparison in migratedPluginsPreserveBehaviour() proves
 * "identical to the base commit"; this slot proves the comparison is not
 * vacuous for the slice-6 modules: Lb302 and both LADSPA-hosted effects must
 * render audible output, the LADSPA effects must actually change the signal
 * (a bypassed effect would compare equal trivially), the two distinct LADSPA
 * plugins must not render identically, and every control-port value applied
 * through loadSettings() must survive the saveState() round trip.
 */
void PluginPortsMigrationTest::slice6PluginsAreLiveAndExact()
{
	// Consumes the module renders from migratedPluginsPreserveBehaviour(),
	// which a Windows test host cannot produce (see testHostCanLoadPluginModules()).
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	// Lb302: rendered through the same instrument entry point as every other
	// instrument, so a non-silent peak proves the note path ran.
	const auto* lb302 = findRender(m_migrated, "lb302");
	QVERIFY2(lb302 != nullptr, "lb302 render missing");
	float lb302Peak = 0.0f;
	for (const float s : lb302->samples)
	{
		lb302Peak = std::max(lb302Peak, std::fabs(s));
	}
	qInfo().noquote() << QString{"slice 6 negative control: lb302 peak=%1 (audible)"}
		.arg(lb302Peak, 0, 'g', 4);
	QVERIFY2(lb302Peak > 1e-4f, "lb302 rendered silence");

	// LADSPA-hosted effects: audible, wet and round-tripped.
	const auto dry = partc::dryInput(lmms::Engine::audioEngine()->framesPerPeriod());
	const auto& specs = partc::ladspaSpecs();
	QCOMPARE(m_ladspa.size(), specs.size());
	for (std::size_t i = 0; i < specs.size(); ++i)
	{
		const auto& spec = specs[i];
		const QString name = QString::fromUtf8(spec.name);
		const auto* render = findRender(m_migrated, spec.name);
		QVERIFY2(render != nullptr, spec.name);
		QCOMPARE(render->samples.size(), dry.size());

		float peak = 0.0f;
		for (const float s : render->samples)
		{
			peak = std::max(peak, std::fabs(s));
		}
		const float wet = partc::maxAbsDiff(render->samples, dry);
		qInfo().noquote() << QString{"slice 6 negative control: %1 peak=%2 max|wet-dry|=%3"}
			.arg(name).arg(peak, 0, 'g', 4).arg(wet, 0, 'g', 4);
		QVERIFY2(peak > 1e-4f, qPrintable(name + " rendered silence"));
		QVERIFY2(wet > 0.05f,
			qPrintable(name + " did not change the signal (bypassed?)"));

		// State round trip: each port value applied by loadSettings() must be
		// visible again in the serialised state.
		const auto& ladspa = m_ladspa[i];
		QCOMPARE(static_cast<int>(ladspa.savedPorts.size()),
			static_cast<int>(spec.ports.size()));
		for (int p = 0; p < static_cast<int>(spec.ports.size()); ++p)
		{
			const QStringList parts = ladspa.savedPorts[p].split(QLatin1Char('='));
			QCOMPARE(parts.size(), 2);
			const double saved = parts[1].toDouble();
			const double expected = QString::fromUtf8(spec.ports[p].value).toDouble();
			qInfo().noquote() << QString{"slice 6 state round trip: %1 %2 expected=%3"}
				.arg(name, ladspa.savedPorts[p]).arg(expected, 0, 'g', 9);
			QVERIFY2(std::fabs(saved - expected) < 1e-3,
				qPrintable(QString{"%1: %2 did not round-trip"}.arg(name, ladspa.savedPorts[p])));
		}
	}

	// Two distinct LADSPA plugins through the migrated module must differ, so
	// the comparator is live on the new modules too.
	const auto* amp = findRender(m_migrated, "ladspaeffect:amp");
	const auto* djEq = findRender(m_reference, "ladspaeffect:dj_eq");
	QVERIFY(amp != nullptr && djEq != nullptr);
	const float worst = partc::maxAbsDiff(amp->samples, djEq->samples);
	qInfo().noquote()
		<< QString{"slice 6 negative control: ladspaeffect:amp (migrated) vs ladspaeffect:dj_eq (reference) max|delta|=%1"}
			   .arg(worst, 0, 'g', 3);
	QVERIFY2(amp->checksum != djEq->checksum,
		"distinct LADSPA plugins rendered identically");
	QVERIFY2(worst > 1e-6f, "LADSPA comparison not sensitive");
}

/*!
 * Slice 7 (task #589) negative controls for the last in-tree legacy-API
 * plugins: every one of them must render audio, the two signal shapers must
 * actually change the signal (a bypassed effect would compare equal to its
 * reference trivially), and Oscilloscope - a documented pass-through - must
 * hand the input back bit-for-bit. Two distinct new plugins must not render
 * identically, so the comparator is live on the new modules as well.
 */
void PluginPortsMigrationTest::slice7PluginsAreLiveAndExact()
{
	// Consumes the module renders from migratedPluginsPreserveBehaviour(),
	// which a Windows test host cannot produce (see testHostCanLoadPluginModules()).
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	const auto dry = partc::dryInput(lmms::Engine::audioEngine()->framesPerPeriod());
	for (const char* name : {"frequencyshifter", "oscilloscope", "slewdistortion"})
	{
		const auto* render = findRender(m_migrated, name);
		QVERIFY2(render != nullptr, name);
		QCOMPARE(render->samples.size(), dry.size());

		float peak = 0.0f;
		for (const float s : render->samples)
		{
			peak = std::max(peak, std::fabs(s));
		}
		const float wet = partc::maxAbsDiff(render->samples, dry);
		qInfo().noquote() << QString{"slice 7 negative control: %1 peak=%2 max|wet-dry|=%3"}
			.arg(name).arg(peak, 0, 'g', 4).arg(wet, 0, 'g', 4);
		QVERIFY2(peak > 1e-4f, qPrintable(QString{"%1 rendered silence"}.arg(name)));
	}

	// The two shapers must not be bypassed.
	for (const char* name : {"frequencyshifter", "slewdistortion"})
	{
		const auto* render = findRender(m_migrated, name);
		QVERIFY2(render != nullptr, name);
		const float wet = partc::maxAbsDiff(render->samples, dry);
		QVERIFY2(wet > 0.05f,
			qPrintable(QString{"%1 did not change the signal (bypassed?)"}.arg(name)));
	}

	// Oscilloscope only copies the signal into its GUI ring buffer, so its
	// render must equal the dry input exactly.
	const auto* scope = findRender(m_migrated, "oscilloscope");
	QVERIFY2(scope != nullptr, "oscilloscope render missing");
	QCOMPARE(scope->checksum, partc::checksum(dry));

	// Two distinct new plugins must differ from each other.
	const auto* shifter = findRender(m_migrated, "frequencyshifter");
	const auto* slew = findRender(m_reference, "slewdistortion");
	QVERIFY(shifter != nullptr && slew != nullptr);
	const float worst = partc::maxAbsDiff(shifter->samples, slew->samples);
	qInfo().noquote()
		<< QString{"slice 7 negative control: frequencyshifter (migrated) vs slewdistortion (reference) max|delta|=%1"}
			   .arg(worst, 0, 'g', 3);
	QVERIFY2(shifter->checksum != slew->checksum, "distinct plugins rendered identically");
	QVERIFY2(worst > 1e-6f, "slice 7 comparison not sensitive");
}

/*!
 * Slice 8 (task #589) negative control: every newly proven slice-8 render
 * must be audible and pairwise distinct. Without this a host that failed to
 * load its asset (an unloaded soundfont, a missing rawwave directory, an LV2
 * plugin that never instantiated) would render silence on both sides and the
 * sample-exact comparison would pass trivially.
 */
void PluginPortsMigrationTest::slice8PluginsAreLiveAndExact()
{
	// Consumes the module renders from migratedPluginsPreserveBehaviour(),
	// which a Windows test host cannot produce (see testHostCanLoadPluginModules()).
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	std::vector<QString> names;
#ifdef PART_C_MIGRATED_sf2player
	names.push_back(QStringLiteral("sf2player"));
#endif
#ifdef PART_C_MIGRATED_malletsstk
	names.push_back(QStringLiteral("malletsstk"));
#endif
#ifdef PART_C_MIGRATED_gigplayer
	names.push_back(QStringLiteral("gigplayer"));
#endif
#ifdef PART_C_MIGRATED_lv2effect
	for (const auto& spec : partc::lv2Specs())
	{
		if (QString::fromUtf8(spec.plugin) == QLatin1String("lv2effect") && partc::lv2BundleProvides(spec.uri))
		{
			names.push_back(QString::fromUtf8(spec.name));
		}
	}
#endif
#ifdef PART_C_MIGRATED_lv2instrument
	for (const auto& spec : partc::lv2Specs())
	{
		if (QString::fromUtf8(spec.plugin) == QLatin1String("lv2instrument") && partc::lv2BundleProvides(spec.uri))
		{
			names.push_back(QString::fromUtf8(spec.name));
		}
	}
#endif
	// The slice-8 plugins need optional third-party deps (libgig, STK, lilv,
	// fluidsynth). A CI runner without them builds none of these, so an empty
	// list is a skip, not a failure (it failed the whole ctest suite on CI).
	if (names.empty())
	{
		QSKIP("no slice-8 plugin is built into this configuration");
	}

	for (const auto& name : names)
	{
		const auto* render = findRender(m_migrated, name.toUtf8().constData());
		QVERIFY2(render != nullptr, qPrintable(name));

		float peak = 0.0f;
		for (const float s : render->samples)
		{
			peak = std::max(peak, std::fabs(s));
		}
		qInfo().noquote() << QString{"slice 8 negative control: %1 peak=%2"}
			.arg(name).arg(peak, 0, 'g', 4);
		QVERIFY2(peak > 1e-4f, qPrintable(QString{"%1 rendered silence"}.arg(name)));
	}

	for (std::size_t i = 0; i < names.size(); ++i)
	{
		for (std::size_t j = i + 1; j < names.size(); ++j)
		{
			const auto* a = findRender(m_migrated, names[i].toUtf8().constData());
			const auto* b = findRender(m_reference, names[j].toUtf8().constData());
			QVERIFY(a != nullptr && b != nullptr);

			const float worst = partc::maxAbsDiff(a->samples, b->samples);
			qInfo().noquote()
				<< QString{"slice 8 negative control: %1 (migrated) vs %2 (reference) max|delta|=%3"}
					   .arg(names[i], names[j])
					   .arg(worst, 0, 'g', 3);
			QVERIFY2(a->checksum != b->checksum,
				qPrintable(QString{"%1 and %2 rendered identically"}.arg(names[i], names[j])));
			QVERIFY2(worst > 1e-6f, "slice 8 comparison not sensitive");
		}
	}
}

QTEST_MAIN(PluginPortsMigrationTest)
#include "PluginPortsMigrationTest.moc"
