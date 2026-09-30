/*
 * RecordingRecoveryMenuTest.cpp - File > Recover Recordings places or dismisses an interrupted take
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

/*! Two real WAV takes journalled as in progress (record.journal_begin / journal_update, the state an
 *  abnormal exit leaves) in the instance's own recovery directory: the rebuilt submenu counts them
 *  and names each with its recoverable length; Place on a New Sample Track restores the first and
 *  loads it into a clip at bar 1 of a new sample track; Dismiss drops the second's journal and
 *  keeps its audio file; the next rebuild offers neither. */

#include <QtTest>

#include <QAction>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "RecordingRecoveryMenu.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr int kRate = 44100;
constexpr int kFrames = 22050;

//! A 16-bit mono WAV of @a frames frames of a quiet square wave.
bool writeWav(const QString& path, int frames)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	QDataStream out(&file);
	out.setByteOrder(QDataStream::LittleEndian);
	const quint32 data = static_cast<quint32>(frames) * 2;
	out.writeRawData("RIFF", 4); out << quint32(36 + data);
	out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1) << quint32(kRate)
		<< quint32(kRate * 2) << quint16(2) << quint16(16);
	out.writeRawData("data", 4); out << data;
	for (int i = 0; i < frames; ++i) { out << qint16((i / 50) % 2 ? 3000 : -3000); }
	return true;
}

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject{})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QAction* item(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
	}
	return nullptr;
}

} // namespace

class RecordingRecoveryMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_world.isValid());
		Engine::init(true);
		ControlRegistry::setReady(true);
		ConfigManager::inst()->setWorkingDir(m_world.path());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theTextNamesTheLength()
	{
		QCOMPARE(recoveredTakeText({{QStringLiteral("take"), QStringLiteral("/x/take-1.wav")},
			{QStringLiteral("sample_rate"), 48000}, {QStringLiteral("frames_recoverable"), 72000}}),
			QStringLiteral("take-1.wav (1.5 s recoverable)"));
	}

	void anInterruptedTakeIsPlacedOrDismissed()
	{
		const ControlResult scanned = run(QStringLiteral("record.recovery_get_state"));
		QVERIFY2(scanned.ok, qPrintable(scanned.errorMessage));
		const QString dir = scanned.result.value(QStringLiteral("dir")).toString();
		const QString placed = QDir(dir).filePath(QStringLiteral("zene-test-take-placed.wav"));
		const QString dismissed = QDir(dir).filePath(QStringLiteral("zene-test-take-dismissed.wav"));
		for (const QString& take : {placed, dismissed})
		{
			QVERIFY(writeWav(take, kFrames));
			const ControlResult begun = run(QStringLiteral("record.journal_begin"), {{QStringLiteral("take"), take},
				{QStringLiteral("sample_rate"), kRate}, {QStringLiteral("channels"), 1}});
			QVERIFY2(begun.ok, qPrintable(begun.errorMessage));
			QVERIFY(run(QStringLiteral("record.journal_update"), {{QStringLiteral("take"), take},
				{QStringLiteral("frames_on_disk"), kFrames}}).ok);
		}

		QMenu parent;
		QMenu* menu = addRecordingRecoveryMenu(&parent);
		emit menu->aboutToShow();
		QCOMPARE(menu->actions().first()->text(), QStringLiteral("2 interrupted recording(s)"));
		QAction* placedTake = item(menu, QStringLiteral("zene-test-take-placed.wav (0.5 s recoverable)"));
		QAction* dismissedTake = item(menu, QStringLiteral("zene-test-take-dismissed.wav (0.5 s recoverable)"));
		QVERIFY(placedTake != nullptr && placedTake->menu() != nullptr);
		QVERIFY(dismissedTake != nullptr && dismissedTake->menu() != nullptr);

		const auto tracksBefore = Engine::getSong()->tracks().size();
		item(placedTake->menu(), QStringLiteral("Place on a New Sample Track"))->trigger();
		QCOMPARE(Engine::getSong()->tracks().size(), tracksBefore + 1);
		auto* track = dynamic_cast<SampleTrack*>(Engine::getSong()->tracks().back());
		QVERIFY(track != nullptr);
		QCOMPARE(track->name(), QStringLiteral("Recovered: zene-test-take-placed"));
		QCOMPARE(track->numOfClips(), 1);
		auto* clip = dynamic_cast<SampleClip*>(track->getClip(0));
		QVERIFY(clip != nullptr);
		QCOMPARE(clip->sampleFile(), placed);

		item(dismissedTake->menu(), QStringLiteral("Dismiss (keep the file)"))->trigger();
		QVERIFY(QFileInfo::exists(dismissed));
		QVERIFY(!QFileInfo::exists(dismissed + QStringLiteral(".rec-journal")));

		emit menu->aboutToShow();
		QCOMPARE(menu->actions().first()->text(), QStringLiteral("No interrupted recording"));
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}

private:
	QTemporaryDir m_world;
};

QTEST_MAIN(RecordingRecoveryMenuTest)
#include "RecordingRecoveryMenuTest.moc"
