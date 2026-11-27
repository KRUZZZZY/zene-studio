/*
 * RetroTakeDefaultPathTest.cpp - cluster new-p12-63-port: a default recording
 *                                 path names its own take.
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
 */

/*
 * record.retro_capture_to_take with no `file` used to write the fixed
 * `zene-retro-take.wav` beside the recovery file, and writeRetroAudioTake
 * replaces its destination on commit: every default take reused one file, so
 * the second take silently destroyed the audio the first one wrote. The four
 * cases below drive the registered command through the real registry on a real
 * engine: the normal path (two default takes, distinct files), an explicit
 * file, a refused write, and exhaustion of the bounded candidate space.
 *
 * Its own file rather than ControlRecordingRoutesTest.cpp because that suite is
 * at the file-length ratchet's 500-code-line ceiling.
 */

#include <QtTest>

#include <algorithm>
#include <cstddef>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlRecordingSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "RetroAudioCapture.h"
#include "SampleFrame.h"

using namespace lmms;

class RetroTakeDefaultPathTest : public QObject
{
	Q_OBJECT

	//! Byte content of \a path: what "the first take is still exactly itself" compares.
	static QByteArray contents(const QString& path)
	{
		QFile file(path);
		file.open(QIODevice::ReadOnly);
		return file.readAll();
	}

	//! Point the default-take directory at \a dir for the rest of the test, then restore it.
	static auto useRecoveryDir(const QTemporaryDir& dir)
	{
		const QString previous = ConfigManager::inst()->workingDir();
		ConfigManager::inst()->setWorkingDir(dir.path());
		return qScopeGuard([previous] { ConfigManager::inst()->setWorkingDir(previous); });
	}

	//! Arm the engine's retrospective capture, retain \a frames frames of \a value at
	//! 48 kHz and disarm again: the window a default retro take is written from.
	static void captureWindow(std::size_t frames, sample_t value)
	{
		RetroAudioCapture& capture = Engine::audioEngine()->retroCapture();
		capture.arm(true);
		const std::vector<SampleFrame> window(frames, SampleFrame(value));
		capture.push(window.data(), static_cast<f_cnt_t>(window.size()), 48000);
		capture.arm(false);
	}

	//! The default retro take name for \a index: the historical stem for 0, "-N" after.
	static QString candidate(const QDir& dir, int index)
	{
		const QString suffix = index == 0 ? QString() : QStringLiteral("-%1").arg(index);
		return dir.filePath(QStringLiteral("zene-retro-take%1.wav").arg(suffix));
	}

private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The reported defect: the second default take must not be the first one's file.
	void defaultRetroTakeDoesNotReuseTheFirstTakesFile()
	{
		QVERIFY(Engine::audioEngine() != nullptr);
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		auto restore = useRecoveryDir(dir);
		ControlRegistry* registry = ControlRegistry::instance();

		captureWindow(8, 0.5f);
		const ControlResult first = registry->invoke(QStringLiteral("record.retro_capture_to_take"));
		QVERIFY2(first.ok, qPrintable(first.errorMessage));
		const QString firstPath = first.result.value(QStringLiteral("file")).toString();
		QCOMPARE(firstPath, candidate(QDir(dir.path()), 0));
		const QByteArray firstBytes = contents(firstPath);
		QVERIFY(firstBytes.size() > 44);

		captureWindow(8, -0.25f);
		const ControlResult second = registry->invoke(QStringLiteral("record.retro_capture_to_take"));
		QVERIFY2(second.ok, qPrintable(second.errorMessage));
		const QString secondPath = second.result.value(QStringLiteral("file")).toString();
		QVERIFY2(secondPath != firstPath,
			qPrintable(QStringLiteral("the second default retro take reused %1").arg(firstPath)));
		QVERIFY(QFileInfo(secondPath).size() > 44);
		QCOMPARE(contents(firstPath), firstBytes);
	}

	//! An explicit file is written where the caller asked and reserves nothing: the
	//! next default take still gets the historical default name.
	void explicitRetroTakeFileLeavesTheDefaultNameFree()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		auto restore = useRecoveryDir(dir);
		ControlRegistry* registry = ControlRegistry::instance();
		const QString askedFor = dir.filePath(QStringLiteral("my-take.wav"));

		captureWindow(8, 0.5f);
		const ControlResult asked = registry->invoke(QStringLiteral("record.retro_capture_to_take"),
			QJsonObject{{QStringLiteral("file"), askedFor}});
		QVERIFY2(asked.ok, qPrintable(asked.errorMessage));
		QCOMPARE(asked.result.value(QStringLiteral("file")).toString(), askedFor);
		QVERIFY(QFileInfo(askedFor).size() > 44);

		captureWindow(8, 0.5f);
		const ControlResult byDefault = registry->invoke(QStringLiteral("record.retro_capture_to_take"));
		QVERIFY2(byDefault.ok, qPrintable(byDefault.errorMessage));
		QCOMPARE(byDefault.result.value(QStringLiteral("file")).toString(),
			candidate(QDir(dir.path()), 0));
		QVERIFY(QFileInfo::exists(askedFor));
	}

	//! A refused write leaves no reservation behind: the take stays retryable and
	//! gets the same default name again.
	void refusedRetroTakeWriteLeavesNoReservation()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		auto restore = useRecoveryDir(dir);
		ControlRegistry* registry = ControlRegistry::instance();

		captureWindow(8, 0.5f);
		retroaudiotesting::failNextTakeMove();
		const ControlResult refused = registry->invoke(QStringLiteral("record.retro_capture_to_take"));
		QVERIFY2(!refused.ok, "a refused move reported a written take");
		QVERIFY(!refused.errorMessage.isEmpty());
		const QStringList left = QDir(dir.path()).entryList(QDir::Files | QDir::NoDotAndDotDot);
		QVERIFY2(left.isEmpty(),
			qPrintable(QStringLiteral("the refused take left [%1]").arg(left.join(QStringLiteral(", ")))));

		captureWindow(8, 0.5f);
		const ControlResult retry = registry->invoke(QStringLiteral("record.retro_capture_to_take"));
		QVERIFY2(retry.ok, qPrintable(retry.errorMessage));
		QCOMPARE(retry.result.value(QStringLiteral("file")).toString(), candidate(QDir(dir.path()), 0));
	}

	//! Every candidate occupied refuses the default take, typed, and writes nothing;
	//! freeing one makes the take possible again.
	void defaultRetroTakeExhaustionRefusesWithoutWriting()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		auto restore = useRecoveryDir(dir);
		ControlRegistry* registry = ControlRegistry::instance();
		for (int index = 0; index < control::TakeNameProbeBound; ++index)
		{
			QFile occupied(candidate(QDir(dir.path()), index));
			QVERIFY(occupied.open(QIODevice::WriteOnly));
			occupied.write("x");
		}

		captureWindow(8, 0.5f);
		const ControlResult refused = registry->invoke(QStringLiteral("record.retro_capture_to_take"));
		QVERIFY2(!refused.ok, "the default retro take succeeded with every candidate occupied");
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QCOMPARE(contents(candidate(QDir(dir.path()), 0)), QByteArray("x"));

		QFile::remove(candidate(QDir(dir.path()), 0));
		const ControlResult retry = registry->invoke(QStringLiteral("record.retro_capture_to_take"));
		QVERIFY2(retry.ok, qPrintable(retry.errorMessage));
		QVERIFY(QFileInfo(candidate(QDir(dir.path()), 0)).size() > 44);
	}
};

QTEST_GUILESS_MAIN(RetroTakeDefaultPathTest)

#include "RetroTakeDefaultPathTest.moc"
