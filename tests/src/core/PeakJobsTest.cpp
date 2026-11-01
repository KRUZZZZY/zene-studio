/*
 * PeakJobsTest.cpp - browser.peaks_start through app.tasks: lifecycle, ids, cancel, failure, responsiveness
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
#include <thread>

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

#include "BrowserPeakCache.h"
#include "BrowserTestSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "PeakJobs.h"
#include "TaskCenter.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The app.tasks row called @a id; empty when it is not listed.
QJsonObject row(const QString& id)
{
	for (const QJsonValue& value : run(QStringLiteral("app.tasks")).result.value(QStringLiteral("tasks")).toArray())
	{
		if (value.toObject().value(QStringLiteral("id")).toString() == id) { return value.toObject(); }
	}
	return {};
}

QString stateOf(const QString& id)
{
	return row(id).value(QStringLiteral("state")).toString();
}

//! Holds PeakJobs' worker before its read until released, so a test decides when the read happens.
class WorkerGate
{
public:
	WorkerGate()
	{
		PeakJobs::instance().setBeforeComputeHookForTesting([this] {
			m_entered.store(true);
			m_worker.store(std::this_thread::get_id());
			QElapsedTimer bound;
			bound.start();
			while (!m_release.load() && bound.elapsed() < 20000) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
		});
	}
	~WorkerGate()
	{
		m_release.store(true);
		PeakJobs::instance().waitForWorkerForTesting(30000);
		QCoreApplication::processEvents();
		PeakJobs::instance().setBeforeComputeHookForTesting({});
	}
	void release() { m_release.store(true); }
	bool entered() const { return m_entered.load(); }
	std::thread::id worker() const { return m_worker.load(); }

private:
	std::atomic<bool> m_release{false};
	std::atomic<bool> m_entered{false};
	std::atomic<std::thread::id> m_worker{};
};

} // namespace

class PeakJobsTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;
	int m_otherOwner = 0;  //!< its address owns the task that moves TaskCenter's ids away from PeakJobs' own

	QString wave(const QString& name, int frames = 4096)
	{
		const QString path = m_dir.filePath(name);
		browserfixture::WaveSpec spec;
		spec.frames = frames;
		spec.burstStartFrame = frames / 2;
		return browserfixture::writeWave(path, spec) ? path : QString();
	}

	QString start(const QString& path)
	{
		const ControlResult started = run(QStringLiteral("browser.peaks_start"), {{QStringLiteral("path"), path}});
		return started.ok ? started.result.value(QStringLiteral("task")).toString() : started.errorMessage;
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		BrowserPeakCache::instance().clear();
		TaskCenter::instance().forget(&m_otherOwner);
		TaskCenter::instance().clearFinished();
	}

	void startListsRunsOffThisThreadAndCompletes()
	{
		const QString path = wave(QStringLiteral("a.wav"));
		QVERIFY(!path.isEmpty());
		WorkerGate gate;
		const ControlResult started = run(QStringLiteral("browser.peaks_start"), {{QStringLiteral("path"), path}});
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		QCOMPARE(started.result.value(QStringLiteral("cached")).toBool(), false);
		const QString id = started.result.value(QStringLiteral("task")).toString();
		QCOMPARE(row(id).value(QStringLiteral("kind")).toString(), QStringLiteral("browser_peak"));

		// The reply came back while the worker has not read a byte, and the read is on another thread.
		QTRY_VERIFY_WITH_TIMEOUT(gate.entered(), 5000);
		QVERIFY(gate.worker() != std::this_thread::get_id());
		QTRY_COMPARE_WITH_TIMEOUT(stateOf(id), QStringLiteral("running"), 5000);
		QVERIFY(!BrowserPeakCache::instance().isCurrent(path));

		gate.release();
		QTRY_COMPARE_WITH_TIMEOUT(stateOf(id), QStringLiteral("done"), 10000);
		QCOMPARE(row(id).value(QStringLiteral("progress")).toDouble(), 1.0);
		const ControlResult peaks = run(QStringLiteral("browser.peaks"), {{QStringLiteral("path"), path}, {QStringLiteral("buckets"), 32}});
		QVERIFY2(peaks.ok, qPrintable(peaks.errorMessage));
		QVERIFY(peaks.result.value(QStringLiteral("cached")).toBool());

		// What the worker read is what the synchronous read reads.
		const QJsonArray workerPeaks = peaks.result.value(QStringLiteral("peaks")).toArray();
		BrowserPeakCache::instance().clear();
		const ControlResult direct = run(QStringLiteral("browser.peaks"), {{QStringLiteral("path"), path}, {QStringLiteral("buckets"), 32}});
		QCOMPARE(direct.result.value(QStringLiteral("cached")).toBool(), false);
		QCOMPARE(workerPeaks, direct.result.value(QStringLiteral("peaks")).toArray());
	}

	void theReturnedIdIsTheTaskCentersAfterAnotherTaskTookOne()
	{
		const TaskToken other = TaskCenter::instance().begin(&m_otherOwner, 1, {QStringLiteral("render"), QStringLiteral("Other"), [] {}, {}, false});
		const int otherId = TaskCenter::instance().idOf(&m_otherOwner, 1);
		const QString path = wave(QStringLiteral("b.wav"));
		WorkerGate gate;
		const ControlResult started = run(QStringLiteral("browser.peaks_start"), {{QStringLiteral("path"), path}});
		QVERIFY(started.ok);
		const int id = started.result.value(QStringLiteral("task_id")).toInt();
		QVERIFY(id != otherId);
		QCOMPARE(started.result.value(QStringLiteral("task")).toString(), QStringLiteral("peak-%1").arg(id));
		bool found = false;
		for (const TaskInfo& info : TaskCenter::instance().tasks()) { found = found || (info.id == id && info.kind == QLatin1String("browser_peak")); }
		QVERIFY(found);
		// A cancel through that id stops this job and leaves the other task running.
		QVERIFY(run(QStringLiteral("app.task_cancel"), {{QStringLiteral("id"), QStringLiteral("peak-%1").arg(id)}}).ok);
		gate.release();
		QTRY_COMPARE_WITH_TIMEOUT(stateOf(QStringLiteral("peak-%1").arg(id)), QStringLiteral("cancelled"), 5000);
		QCOMPARE(TaskCenter::instance().activeCount(), 1);
		QCOMPARE(TaskCenter::instance().idOf(&m_otherOwner, 1), otherId);
		TaskCenter::instance().finish(other, TaskState::Done);
	}

	void aQueuedJobBehindABusyWorkerCancelsAtOnce()
	{
		const QString busy = wave(QStringLiteral("busy.wav"));
		const QString queued = wave(QStringLiteral("queued.wav"));
		WorkerGate gate;
		const QString first = start(busy);
		QTRY_VERIFY_WITH_TIMEOUT(gate.entered(), 5000);
		const QString second = start(queued);
		QVERIFY(first != second);
		QCOMPARE(stateOf(second), QStringLiteral("queued"));
		QVERIFY(run(QStringLiteral("app.task_cancel"), {{QStringLiteral("id"), second}}).ok);
		QCOMPARE(stateOf(second), QStringLiteral("cancelled"));  // with the worker still held
		gate.release();
		QTRY_COMPARE_WITH_TIMEOUT(stateOf(first), QStringLiteral("done"), 10000);
		QVERIFY(!BrowserPeakCache::instance().isCurrent(queued));  // the worker skipped it
		QCOMPARE(stateOf(second), QStringLiteral("cancelled"));
	}

	void aRunningCancelLeavesTheCacheAlone()
	{
		const QString path = wave(QStringLiteral("running.wav"));
		WorkerGate gate;
		const QString id = start(path);
		QTRY_VERIFY_WITH_TIMEOUT(gate.entered(), 5000);
		QVERIFY(run(QStringLiteral("app.task_cancel"), {{QStringLiteral("id"), id}}).ok);
		gate.release();
		QTRY_COMPARE_WITH_TIMEOUT(stateOf(id), QStringLiteral("cancelled"), 5000);
		QVERIFY(!BrowserPeakCache::instance().isCurrent(path));
		QCOMPARE(BrowserPeakCache::instance().entryCount(), 0);
	}

	//! The read has finished and its result is queued for this thread; a cancel now must still win.
	void aCancelAfterTheReadBeforeAdoptionWins()
	{
		const QString path = wave(QStringLiteral("late.wav"));
		const QString id = start(path);
		QVERIFY(PeakJobs::instance().waitForWorkerForTesting(30000));  // no event turn: the result is not adopted yet
		QVERIFY(!BrowserPeakCache::instance().isCurrent(path));
		QVERIFY(run(QStringLiteral("app.task_cancel"), {{QStringLiteral("id"), id}}).ok);
		QCoreApplication::processEvents();
		QTRY_COMPARE_WITH_TIMEOUT(stateOf(id), QStringLiteral("cancelled"), 5000);
		QVERIFY(!BrowserPeakCache::instance().isCurrent(path));
	}

	void aFileThatCannotBeReadFailsItsTaskWithTheReason()
	{
		const QString path = m_dir.filePath(QStringLiteral("garbage.wav"));
		QFile garbage(path);
		QVERIFY(garbage.open(QIODevice::WriteOnly));
		garbage.write("this is not audio");
		garbage.close();
		const QString id = start(path);
		QTRY_COMPARE_WITH_TIMEOUT(stateOf(id), QStringLiteral("failed"), 5000);
		QVERIFY(!row(id).value(QStringLiteral("status")).toString().isEmpty());
		QCOMPARE(BrowserPeakCache::instance().entryCount(), 0);
	}

	void aCachedFileEndsAtOnceAndEndedOrUnknownIdsAreRefused()
	{
		const QString path = wave(QStringLiteral("cached.wav"));
		const QString first = start(path);
		QTRY_COMPARE_WITH_TIMEOUT(stateOf(first), QStringLiteral("done"), 10000);
		const ControlResult again = run(QStringLiteral("browser.peaks_start"), {{QStringLiteral("path"), path}});
		QVERIFY(again.result.value(QStringLiteral("cached")).toBool());
		QCOMPARE(stateOf(again.result.value(QStringLiteral("task")).toString()), QStringLiteral("done"));
		QCOMPARE(run(QStringLiteral("app.task_cancel"), {{QStringLiteral("id"), first}}).errorKind, ControlErrorKind::Refused);
		QCOMPARE(run(QStringLiteral("app.task_cancel"), {{QStringLiteral("id"), QStringLiteral("peak-999999")}}).errorKind, ControlErrorKind::NotFound);
		QVERIFY(!run(QStringLiteral("browser.peaks_start"), {{QStringLiteral("path"), m_dir.filePath(QStringLiteral("nope.wav"))}}).ok);
	}

	void aFileAlreadyBeingReadIsNotStartedTwiceUntilCancelled()
	{
		const QString path = wave(QStringLiteral("twice.wav"));
		WorkerGate gate;
		const QString first = start(path);
		QCOMPARE(start(path), first);
		QVERIFY(run(QStringLiteral("app.task_cancel"), {{QStringLiteral("id"), first}}).ok);
		const QString second = start(path);  // the first will end cancelled: a new request is a new job
		QVERIFY(second != first);
		gate.release();
		QTRY_COMPARE_WITH_TIMEOUT(stateOf(second), QStringLiteral("done"), 10000);
		QCOMPARE(stateOf(first), QStringLiteral("cancelled"));
		QVERIFY(BrowserPeakCache::instance().isCurrent(path));
	}

	//! The interface thread's event loop keeps turning for as long as the worker holds the read.
	void theEventLoopTurnsWhileAReadIsInProgress()
	{
		const QString path = wave(QStringLiteral("ticks.wav"));
		int ticks = 0;
		QTimer ticker;
		ticker.setInterval(1);
		connect(&ticker, &QTimer::timeout, this, [&ticks] { ++ticks; });
		ticker.start();
		WorkerGate gate;
		const QString id = start(path);
		QTRY_VERIFY_WITH_TIMEOUT(gate.entered(), 5000);
		QTRY_VERIFY_WITH_TIMEOUT(ticks >= 20, 5000);
		QVERIFY(TaskCenter::instance().activeCount() >= 1);  // and the job is still open
		gate.release();
		QTRY_COMPARE_WITH_TIMEOUT(stateOf(id), QStringLiteral("done"), 10000);
	}
};

QTEST_GUILESS_MAIN(PeakJobsTest)
#include "PeakJobsTest.moc"
