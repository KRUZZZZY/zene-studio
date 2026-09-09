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
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "PluginPortsHarness.h"

#include <cstdio>
#include <vector>

#include <QByteArray>
#include <QLibrary>
#include <memory>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

#include "Engine.h"
#include "Plugin.h"

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
	void migratedPluginsPreserveBehaviour();
	void comparisonIsSensitive();
	void slice5InstrumentsAreLiveAndDistinct();
	void slice6PluginsAreLiveAndExact();

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
	QVERIFY2(lmms::Engine::audioEngine() != nullptr, "engine failed to initialise");
	lmms::Engine::audioEngine()->audioDev()->stopProcessing();

	const lmms::f_cnt_t frames = lmms::Engine::audioEngine()->framesPerPeriod();
	qInfo("engine: sample rate %d, framesPerPeriod %d, buffers per plugin %d",
		static_cast<int>(lmms::Engine::audioEngine()->outputSampleRate()), static_cast<int>(frames), partc::Buffers);
	QVERIFY(frames > 0);
}

void PluginPortsMigrationTest::migratedPluginsPreserveBehaviour()
{
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

		QVERIFY2(migrated.checksum == referenceRender.checksum,
			qPrintable(QString{"%1: migrated render differs from the pre-migration reference "
							   "(max|delta|=%2)"}
						   .arg(migrated.name)
						   .arg(worst, 0, 'g', 3)));
	}
}

/*!
 * Negative control: the comparison above must be able to fail. Comparing two
 * *different* plugins' renders has to produce different checksums; otherwise
 * the harness would be comparing silence with silence and prove nothing.
 */
void PluginPortsMigrationTest::comparisonIsSensitive()
{
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

QTEST_MAIN(PluginPortsMigrationTest)
#include "PluginPortsMigrationTest.moc"
