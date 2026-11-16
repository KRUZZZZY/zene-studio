/*
 * AudioTestToneTest.cpp - audio.device_list's running-backend fields, and audio.test_tone / audio.test_tone_stop:
 *                         the tone reaches the master output and stops, and the song, transport and undo journal stay put
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
/*! Q050-183.1: the running backend is reported by audio.device_list; audio.test_tone plays a sine through the
 *  output and audio.test_tone_stop ends it. The tone is session audio: the song, its transport, the undo journal
 *  and the config are left alone, and bad arguments are refused before anything plays. */

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <span>

#include <QDir>
#include <QJsonObject>
#include <QList>
#include <QTemporaryDir>

#include "AudioEngine.h"
#include "AudioTestTone.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "SampleFrame.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The largest absolute sample, either channel, of one rendered master period.
float periodPeak(std::span<const SampleFrame> period)
{
	float peak = 0.f;
	for (const SampleFrame& frame : period)
	{
		peak = std::max({peak, std::fabs(frame.left()), std::fabs(frame.right())});
	}
	return peak;
}

//! Renders @a periods master periods of the engine with the song stopped and returns the loudest sample among them.
float renderPeak(int periods)
{
	float peak = 0.f;
	for (int i = 0; i < periods; ++i)
	{
		peak = std::max(peak, periodPeak(Engine::audioEngine()->renderNextPeriod()));
	}
	return peak;
}

} // namespace

class AudioTestToneTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		// A private working directory, as the library tests use: nothing this test does is written to the developer's own.
		ConfigManager::inst()->setWorkingDir(QDir(m_dir.path()).filePath(QStringLiteral("work")));
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void devicesReportRunningAndConfigured()
	{
		const ControlResult listed = run(QStringLiteral("audio.device_list"));
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		AudioEngine* engine = Engine::audioEngine();
		QVERIFY(engine != nullptr);
		const QJsonObject& out = listed.result;
		QCOMPARE(out.value(QStringLiteral("current")).toString(), engine->audioDevName());
		QVERIFY(out.value(QStringLiteral("requested")).isString());
		QVERIFY(out.value(QStringLiteral("fallback")).isBool());
		QVERIFY(out.value(QStringLiteral("failure_reason")).isString());
		QCOMPARE(out.value(QStringLiteral("failure_reason")).toString(), engine->audioDevStartReason());
	}

	void toneDoesNotTouchProject()
	{
		Song* song = Engine::getSong();
		const bool playingBefore = song->isPlaying();
		const TimePos posBefore = song->getPlayPos();
		const bool modifiedBefore = song->isModified();
		const int undoBefore = Engine::projectJournal()->undoDepth();

		const ControlResult started = run(QStringLiteral("audio.test_tone"), {{QStringLiteral("seconds"), 0.5}});
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		QVERIFY(started.result.value(QStringLiteral("playing")).toBool());
		QCOMPARE(started.result.value(QStringLiteral("output")).toString(), Engine::audioEngine()->audioDevName());
		QVERIFY(audioTestToneActive());
		QCOMPARE(song->isPlaying(), playingBefore);
		QCOMPARE(song->getPlayPos().getTicks(), posBefore.getTicks());
		QCOMPARE(song->isModified(), modifiedBefore);
		QCOMPARE(Engine::projectJournal()->undoDepth(), undoBefore);

		// audioTestToneActive() reads the stop flag directly, so the stop is visible at once; nothing waits on the audio thread.
		const ControlResult stopped = run(QStringLiteral("audio.test_tone_stop"));
		QVERIFY2(stopped.ok, qPrintable(stopped.errorMessage));
		QVERIFY(stopped.result.value(QStringLiteral("was_playing")).toBool());
		QVERIFY(!audioTestToneActive());

		const ControlResult second = run(QStringLiteral("audio.test_tone_stop"));
		QVERIFY2(second.ok, qPrintable(second.errorMessage));
		QVERIFY(!second.result.value(QStringLiteral("was_playing")).toBool());
	}

	void toneReachesOutput()
	{
		// The dummy device thread would render periods of its own while this test renders them.
		Engine::audioEngine()->audioDev()->stopProcessing();
		QVERIFY(!Engine::getSong()->isPlaying());

		const ControlResult started = run(QStringLiteral("audio.test_tone"), {{QStringLiteral("seconds"), 0.5}});
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		QVERIFY(renderPeak(8) > 0.01f);
		QVERIFY(audioTestToneActive());

		QVERIFY(run(QStringLiteral("audio.test_tone_stop")).ok);
		// The period the stop lands in may still carry the tail of the tone; the one after it must be silent.
		renderPeak(1);
		QVERIFY(renderPeak(1) < 1e-4f);
	}

	void invalidArgsAreRefusedAndBoundariesPlay()
	{
		const QList<QJsonObject> refused{
			QJsonObject{{QStringLiteral("frequency"), 10}},
			QJsonObject{{QStringLiteral("frequency"), 30000}},
			QJsonObject{{QStringLiteral("seconds"), 0}},
			QJsonObject{{QStringLiteral("seconds"), 11}},
			QJsonObject{{QStringLiteral("level_db"), 0}},
			QJsonObject{{QStringLiteral("frequency"), QStringLiteral("x")}}};
		for (const QJsonObject& args : refused)
		{
			QCOMPARE(run(QStringLiteral("audio.test_tone"), args).errorKind, ControlErrorKind::InvalidArgs);
			QVERIFY(!audioTestToneActive());
		}

		const QList<QJsonObject> accepted{
			QJsonObject{{QStringLiteral("frequency"), 20}},
			QJsonObject{{QStringLiteral("seconds"), 0.1}},
			QJsonObject{{QStringLiteral("level_db"), -60}}};
		for (const QJsonObject& args : accepted)
		{
			const ControlResult started = run(QStringLiteral("audio.test_tone"), args);
			QVERIFY2(started.ok, qPrintable(started.errorMessage));
			QVERIFY(audioTestToneActive());
			QVERIFY(run(QStringLiteral("audio.test_tone_stop")).ok);
			QVERIFY(!audioTestToneActive());
		}
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(AudioTestToneTest)
#include "AudioTestToneTest.moc"
