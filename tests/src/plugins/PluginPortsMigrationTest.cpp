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
	};
	return modules;
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

private:
	std::vector<QLibrary*> m_libraries;
	std::vector<partc::PluginRender> m_migrated;
	std::vector<partc::PluginRender> m_reference;
};

void PluginPortsMigrationTest::initTestCase()
{
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

QTEST_MAIN(PluginPortsMigrationTest)
#include "PluginPortsMigrationTest.moc"
