/*
 * StemSplitBackgroundTest.cpp - nothing a long stem source needs runs on the interface thread (Q050-010)
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

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>

#include "ControlRegistry.h"
#include "ControlStemSource.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleDecoder.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "Song.h"
#include "StemSplitController.h"
#include "TaskCenter.h"
#include "Track.h"

#include "StemSeparation/StemTypes.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The newest app.tasks row whose id starts with @a prefix; empty when there is none.
QJsonObject newestRow(const QString& prefix)
{
	QJsonObject found;
	for (const QJsonValue& value : run(QStringLiteral("app.tasks")).result.value(QStringLiteral("tasks")).toArray())
	{
		if (value.toObject().value(QStringLiteral("id")).toString().startsWith(prefix)) { found = value.toObject(); }
	}
	return found;
}

QStringList tempWavs()
{
	return QDir(QDir::tempPath()).entryList({QStringLiteral("zene-stem-*")}, QDir::Files);
}

QProgressDialog* progressDialog()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		auto* dialog = qobject_cast<QProgressDialog*>(widget);
		if (dialog != nullptr && dialog->isVisible()) { return dialog; }
	}
	return nullptr;
}

//! Accepts any message box (a modal one would hang the test) and records what it said.
QStringList s_messages;
void closeMessageBoxes()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		auto* message = qobject_cast<QMessageBox*>(widget);
		if (message != nullptr && message->isVisible())
		{
			s_messages << message->text();
			message->accept();
		}
	}
}

} // namespace

class StemSplitBackgroundTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;
	QTimer m_boxes;
	std::atomic<bool> m_entered{false};
	std::atomic<bool> m_release{false};
	std::atomic<std::thread::id> m_writer{};

	//! A long clip that came from no file: two minutes of stereo, 42 MB to write.
	SampleClip* longInMemoryClip()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		auto* clip = track == nullptr ? nullptr : dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		if (clip == nullptr) { return nullptr; }
		clip->setName(QStringLiteral("Recorded"));
		clip->setSampleBuffer(std::make_shared<const SampleBuffer>(
			std::vector<SampleFrame>(StemModelSampleRate * 120, SampleFrame(0.25f, -0.25f)), StemModelSampleRate));
		return clip;
	}

	void holdTheWrite()
	{
		m_entered.store(false);
		m_release.store(false);
		gui::StemSplitController::setPreparationHookForTest([this] {
			m_entered.store(true);
			m_writer.store(std::this_thread::get_id());
			QElapsedTimer bound;
			bound.start();
			while (!m_release.load() && bound.elapsed() < 20000) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
		});
	}

	//! The split's state settles: no dialog, nothing being written, no job polled.
	void settle()
	{
		m_release.store(true);
		QVERIFY(QThreadPool::globalInstance()->waitForDone(60000));
		QTRY_VERIFY_WITH_TIMEOUT(progressDialog() == nullptr, 60000);
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		qputenv("TMPDIR", m_dir.path().toLocal8Bit());
		qputenv("LMMS_STEM_MODEL", QFile::encodeName(QStringLiteral(LMMS_TEST_DATA_DIR) + QStringLiteral("/stub-4stem-linear.onnx")));
		qputenv("LMMS_STEM_OFFLINE", "1");
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_boxes.setInterval(10);
		connect(&m_boxes, &QTimer::timeout, &closeMessageBoxes);
		m_boxes.start();
	}

	void cleanupTestCase()
	{
		gui::StemSplitController::setPreparationHookForTest({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init() { s_messages.clear(); }

	void cleanup()
	{
		m_release.store(true);
		QThreadPool::globalInstance()->waitForDone(60000);
		QCoreApplication::processEvents();
		QVERIFY2(s_messages.isEmpty(), qPrintable(s_messages.join(QStringLiteral(" | "))));
	}

	//! No decoder runs on the caller for a source the header probe cannot open: a .ds is accepted unread, garbage refused.
	void theLengthOfASourceNeverDecodesOnTheCaller()
	{
		const QString ds = m_dir.filePath(QStringLiteral("long.ds"));
		QVERIFY(QFile::copy(QStringLiteral(LMMS_TEST_DS_FILE), ds));
		const QString garbage = m_dir.filePath(QStringLiteral("garbage.bin"));
		QFile file(garbage);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("this is not audio");
		file.close();

		SampleDecoder::resetDecodeCallCountsForTesting();
		qint64 frames = -1;
		int rate = 0;
		QString error;
		QVERIFY2(control::stemSourceLength(ds, &frames, &rate, &error), qPrintable(error));
		QCOMPARE(frames, qint64(0));
		QVERIFY(!control::stemSourceLength(garbage, &frames, &rate, &error));
		QVERIFY(error.contains(QStringLiteral("could not decode")));
		QCOMPARE(SampleDecoder::decodeCallCountsForTesting().full, std::uint64_t{0});
	}

	//! The accepted .ds is synthesised by the job, while this thread's event loop turns: its length appears once the
	//! worker has decoded it, and the start reply came back without one.
	void aDrumSynthSourceIsDecodedByTheJobWhileTheLoopTurns()
	{
		const QString ds = m_dir.filePath(QStringLiteral("job.ds"));
		QVERIFY(QFile::copy(QStringLiteral(LMMS_TEST_DS_FILE), ds));
		int ticks = 0;
		QTimer ticker;
		ticker.setInterval(1);
		connect(&ticker, &QTimer::timeout, this, [&ticks] { ++ticks; });
		ticker.start();
		SampleDecoder::resetDecodeCallCountsForTesting();
		const ControlResult started = run(QStringLiteral("stem.job_start"), {{QStringLiteral("source"), ds}});
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		QCOMPARE(started.result.value(QStringLiteral("frames")).toInt(), 0);
		const QJsonObject job{{QStringLiteral("job_id"), started.result.value(QStringLiteral("job_id"))}};
		const auto frames = [&job] {
			return run(QStringLiteral("stem.job_status"), job).result.value(QStringLiteral("jobs")).toArray()
				.first().toObject().value(QStringLiteral("frames")).toInt();
		};
		QTRY_VERIFY_WITH_TIMEOUT(frames() > 0, 30000);
		QVERIFY(ticks > 0);
		QVERIFY(SampleDecoder::decodeCallCountsForTesting().full >= 1);  // it was decoded - on the worker
		run(QStringLiteral("stem.job_cancel"), job);
	}

	void aLongInMemoryClipIsWrittenOnAWorkerThenBecomesAJob()
	{
		SampleClip* clip = longInMemoryClip();
		QVERIFY(clip != nullptr);
		holdTheWrite();
		int ticks = 0;
		QTimer ticker;
		ticker.setInterval(1);
		connect(&ticker, &QTimer::timeout, this, [&ticks] { ++ticks; });
		ticker.start();
		const int jobsBefore = run(QStringLiteral("stem.job_status")).result.value(QStringLiteral("count")).toInt();

		gui::StemSplitController::instance()->splitClipToStems(clip);
		const QJsonObject row = newestRow(QStringLiteral("stem-prepare-"));  // listed before a byte is written
		const QString id = row.value(QStringLiteral("id")).toString();
		QCOMPARE(row.value(QStringLiteral("state")).toString(), QStringLiteral("running"));
		QVERIFY(progressDialog() != nullptr);
		QTRY_VERIFY_WITH_TIMEOUT(m_entered.load(), 5000);
		QVERIFY(m_writer.load() != std::this_thread::get_id());
		QTRY_VERIFY_WITH_TIMEOUT(ticks >= 20, 5000);
		QCOMPARE(run(QStringLiteral("stem.job_status")).result.value(QStringLiteral("count")).toInt(), jobsBefore);

		m_release.store(true);
		QTRY_COMPARE_WITH_TIMEOUT(newestRow(id).value(QStringLiteral("state")).toString(), QStringLiteral("done"), 60000);
		QTRY_COMPARE_WITH_TIMEOUT(run(QStringLiteral("stem.job_status")).result.value(QStringLiteral("count")).toInt(), jobsBefore + 1, 10000);

		// The job went through the registered command with the written file; its Cancel removes that file.
		QCOMPARE(tempWavs().size(), 1);
		QProgressDialog* dialog = progressDialog();
		QVERIFY(dialog != nullptr);
		dialog->cancel();
		QTRY_VERIFY_WITH_TIMEOUT(tempWavs().isEmpty(), 60000);
		settle();
	}

	void cancellingThePreparationThroughAppTasksLeavesNoJobAndNoFile()
	{
		SampleClip* clip = longInMemoryClip();
		QVERIFY(clip != nullptr);
		holdTheWrite();
		const int jobsBefore = run(QStringLiteral("stem.job_status")).result.value(QStringLiteral("count")).toInt();
		gui::StemSplitController::instance()->splitClipToStems(clip);
		QTRY_VERIFY_WITH_TIMEOUT(m_entered.load(), 5000);
		const QString id = newestRow(QStringLiteral("stem-prepare-")).value(QStringLiteral("id")).toString();
		QVERIFY(run(QStringLiteral("app.task_cancel"), {{QStringLiteral("id"), id}}).ok);
		QCOMPARE(newestRow(id).value(QStringLiteral("state")).toString(), QStringLiteral("cancelled"));  // worker still held
		settle();
		QTRY_VERIFY_WITH_TIMEOUT(tempWavs().isEmpty(), 5000);
		QCOMPARE(run(QStringLiteral("stem.job_status")).result.value(QStringLiteral("count")).toInt(), jobsBefore);
	}

	void cancellingThePreparationFromItsDialogLeavesNoFile()
	{
		SampleClip* clip = longInMemoryClip();
		QVERIFY(clip != nullptr);
		holdTheWrite();
		gui::StemSplitController::instance()->splitClipToStems(clip);
		QProgressDialog* dialog = progressDialog();
		QVERIFY(dialog != nullptr);
		dialog->cancel();
		settle();
		QTRY_VERIFY_WITH_TIMEOUT(tempWavs().isEmpty(), 5000);
	}
};

QTEST_MAIN(StemSplitBackgroundTest)
#include "StemSplitBackgroundTest.moc"
