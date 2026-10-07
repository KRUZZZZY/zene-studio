/*
 * ControlRecordingRoutesTest.cpp - the record.arm_track undo contract when a
 *                                   take writes outside the project journal.
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

#include <QtTest>

#include <cstdint>
#include <vector>

#include <QFileInfo>
#include <QTemporaryDir>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "MultiTrackRecorder.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

//! Make sure a failed assertion cannot leave the test engine writing to a
//! temporary directory after that directory is removed.
struct StopRecorderOnExit
{
	MultiTrackRecorder& recorder;
	~StopRecorderOnExit() { recorder.disarmAll(); }
};

} // namespace

class ControlRecordingRoutesTest : public QObject
{
	Q_OBJECT

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

	void undoRefusesToPretendTheRetainedTakeWasRemoved()
	{
		AudioEngine* audio = Engine::audioEngine();
		QVERIFY(audio != nullptr);
		MultiTrackRecorder& recorder = audio->recorder();
		QVERIFY(recorder.inputChannelCapacity() > 0);

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		StopRecorderOnExit stopRecorder{recorder};
		const QString takePath = dir.filePath(QStringLiteral("route.wav"));
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult armed = registry->invoke(QStringLiteral("record.arm_track"),
			QJsonObject{{QStringLiteral("route"), 0}, {QStringLiteral("file"), takePath}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		QVERIFY(recorder.track(0).isArmed());
		QVERIFY(QFileInfo::exists(takePath));

		constexpr f_cnt_t Frames = 4;
		const int channels = recorder.inputChannelCapacity();
		std::vector<sample_t> input(static_cast<std::size_t>(channels) * Frames, 0.25f);
		recorder.processInputInterleaved(input.data(), channels, Frames);
		QCOMPARE(recorder.track(0).framesPushed(), static_cast<std::uint64_t>(Frames));

		const ControlResult undo = registry->invoke(QStringLiteral("control.undo"));
		QVERIFY2(!undo.ok,
			"control.undo claimed to reverse record.arm_track while its take file is retained");
		QCOMPARE(undo.errorKind, ControlErrorKind::Irreversible);
		QVERIFY(undo.errorMessage.contains(QStringLiteral("record.arm_track")));
		QVERIFY(undo.errorMessage.contains(QStringLiteral("record.disarm_track")));
		QVERIFY(recorder.track(0).isArmed());
		QVERIFY(QFileInfo::exists(takePath));

		const ControlRegistry::Transaction* transaction = registry->lastTransaction();
		QVERIFY(transaction != nullptr);
		QCOMPARE(transaction->command, QStringLiteral("record.arm_track"));
		QCOMPARE(transaction->cls, QStringLiteral("irreversible"));
		QVERIFY(!transaction->reversible);
		QVERIFY(transaction->inverse.value(QStringLiteral("op")).toString().isEmpty());
		const control::ReversibilityEntry* row = control::ReversibilityTable::instance()
			.lookup(QStringLiteral("record.arm_track"));
		QVERIFY(row != nullptr);
		QCOMPARE(row->cls, control::ReversibilityClass::Irreversible);
		QVERIFY(!row->reversible);
		QVERIFY(row->fallback.contains(QStringLiteral("record.disarm_track")));
		QVERIFY(row->fallback.contains(QStringLiteral("delete it manually")));
		QVERIFY(registry->command(QStringLiteral("record.arm_track"))->description
			.contains(QStringLiteral("Not reversible")));
		QVERIFY(!registry->command(QStringLiteral("record.disarm_track"))->description
			.contains(QStringLiteral("recorded inverse")));

		const ControlResult stopped = registry->invoke(QStringLiteral("record.disarm_track"),
			QJsonObject{{QStringLiteral("route"), 0}});
		QVERIFY2(stopped.ok, qPrintable(stopped.errorMessage));
		QVERIFY(!recorder.track(0).isArmed());
		QVERIFY(QFileInfo::exists(takePath));
		QVERIFY(QFileInfo(takePath).size() > 44);
	}
};

QTEST_GUILESS_MAIN(ControlRecordingRoutesTest)

#include "ControlRecordingRoutesTest.moc"
