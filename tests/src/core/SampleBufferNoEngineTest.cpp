/*
 * SampleBufferNoEngineTest.cpp - sample buffers and the metronome before the audio engine exists
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
/*! Cluster stress-1 (plug-in stress run, 2026-10-27): a relocated build with no data directory died
 *  with SIGSEGV in main() before any window or socket. ControlRegistry::instance() registers the
 *  record tools, which read the metronome options, which decode the metronome samples; a sample
 *  that does not decode comes back as SampleBuffer::emptyBuffer(), and a default-constructed
 *  SampleBuffer read Engine::audioEngine()->outputSampleRate() while the engine was still null.
 *
 *  The cases run in order in ONE process that never brought an engine up until the last case,
 *  because the shared empty buffer and the metronome samples are each built once per process. */

#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MetronomeOptions.h"
#include "SampleBuffer.h"

using namespace lmms;

namespace
{

//! The rate the engine itself falls back to when no rate is configured (AudioEngine's constructor).
constexpr auto EngineFloorRate = static_cast<sample_rate_t>(SUPPORTED_SAMPLERATES.front());

//! The configured rate for the final case; any supported rate other than the floor shows whether
//! the shared empty buffer follows the engine or was frozen by a caller from before the engine.
constexpr int ConfiguredRate = 48000;

} // namespace

class SampleBufferNoEngineTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY2(Engine::audioEngine() == nullptr,
			"premise: no case before the last one calls Engine::init(), so there is no engine at all");
		QVERIFY(m_emptyData.isValid());
		// The relocated binary of the report: the data search path resolves, but holds no samples.
		ConfigManager::inst();
		m_savedDataPaths = QDir::searchPaths(QStringLiteral("data"));
		QDir::setSearchPaths(QStringLiteral("data"), {m_emptyData.path()});
	}

	void cleanupTestCase()
	{
		QDir::setSearchPaths(QStringLiteral("data"), m_savedDataPaths);
		if (Engine::audioEngine() != nullptr) { Engine::destroy(); }
	}

	//! The frame of the report's backtrace: a default buffer must not read the engine.
	void aDefaultBufferNeedsNoEngine()
	{
		const SampleBuffer buffer;
		QVERIFY(buffer.empty());
		QCOMPARE(buffer.sampleRate(), EngineFloorRate);
	}

	void theSharedEmptyBufferNeedsNoEngine()
	{
		const auto empty = SampleBuffer::emptyBuffer();
		QVERIFY(empty != nullptr);
		QVERIFY(empty->empty());
		QCOMPARE(empty->sampleRate(), EngineFloorRate);
	}

	//! The backtrace itself: main() -> ControlRegistry::instance() -> registerRecordToolCommands ->
	//! metronomeoptions::options() -> load() -> SampleBuffer::fromFile. Every metronome sample fails to
	//! decode, each failure is logged, the process lives, and the options are the defaults.
	void registeringTheCommandsWithNoSamplesAndNoEngineSurvives()
	{
		const int samples = 2 * static_cast<int>(metronomeoptions::soundNames().size());
		for (int i = 0; i < samples; ++i)
		{
			QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Failed to load sample at path")));
		}
		QVERIFY(ControlRegistry::instance() != nullptr);
		const metronomeoptions::Options options = metronomeoptions::options();
		QCOMPARE(options.level, 1.f);
		QVERIFY(options.accent);
		QVERIFY(!options.recordOnly);
		QCOMPARE(options.sound, 0);
		QVERIFY(metronomeoptions::click(true, 0) == nullptr);
	}

	//! Once the engine is up, a missing click sample is a silent click, not a crash; and the shared
	//! empty buffer carries the engine's rate, as it did before - an earlier caller from before the
	//! engine must not have pinned it to the engine-less default.
	void afterEngineStartTheClickIsSilentAndTheEmptyBufferFollowsTheEngine()
	{
		ConfigManager::inst()->setValue(QStringLiteral("audioengine"), QStringLiteral("samplerate"),
			QString::number(ConfiguredRate));
		QDir::setSearchPaths(QStringLiteral("data"), m_savedDataPaths);
		Engine::init(true);
		QVERIFY(Engine::audioEngine() != nullptr);
		const sample_rate_t engineRate = Engine::audioEngine()->outputSampleRate();
		QVERIFY2(engineRate != EngineFloorRate,
			"premise: the engine runs at a rate other than the engine-less default");
		QVERIFY(metronomeoptions::click(true, 0) == nullptr);
		QVERIFY(metronomeoptions::click(false, 0) == nullptr);
		QCOMPARE(SampleBuffer::emptyBuffer()->sampleRate(), engineRate);
		QCOMPARE(SampleBuffer{}.sampleRate(), engineRate);
	}

private:
	QTemporaryDir m_emptyData;
	QStringList m_savedDataPaths;
};

QTEST_GUILESS_MAIN(SampleBufferNoEngineTest)
#include "SampleBufferNoEngineTest.moc"
