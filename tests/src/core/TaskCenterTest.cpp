/*
 * TaskCenterTest.cpp - the background jobs' one list and the task.* commands
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

/*! 0.5.0 UX decision (theme 24): a job reports begin, progress and end by its owner's own id; a report from a worker
 *  thread reaches the list on the main thread; Cancel runs the job's own cancel and is refused for a job that has none
 *  or has ended; an owner going away ends its tasks; the list keeps at most MaxFinished ended tasks. task.list,
 *  task.cancel and task.clear_finished answer over the registry, and a render-queue job is listed from the moment it
 *  is queued and cancelled through the list. */

#include <QtTest>

#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThreadPool>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "ControlRegistry.h"
#include "Engine.h"
#include "RenderQueue.h"
#include "SampleDecoder.h"
#include "TaskCenter.h"
#include "BrowserTestSupport.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The listed task @a id; a default one (id 0) when there is none.
TaskInfo taskWithId(int id)
{
	for (const TaskInfo& task : TaskCenter::instance().tasks())
	{
		if (task.id == id) { return task; }
	}
	return TaskInfo{};
}

void drainTaskCenterEvents(TaskCenter& tasks)
{
	bool drained = false;
	QMetaObject::invokeMethod(&tasks, [&drained] { drained = true; }, Qt::QueuedConnection);
	QTRY_VERIFY(drained);
}

int writeFakeRenderPid()
{
	const QString pidPath = qEnvironmentVariable("ZENE_FAKE_RENDER_PID_PATH");
	if (pidPath.isEmpty()) { return 0; }
	QFile pidFile(pidPath);
	if (!pidFile.open(QIODevice::WriteOnly)) { return 6; }
	pidFile.write(QByteArray::number(QCoreApplication::applicationPid()));
	return 0;
}

bool waitForFakeRenderRelease()
{
	const QString holdPath = qEnvironmentVariable("ZENE_FAKE_RENDER_HOLD_PATH");
	if (holdPath.isEmpty()) { return true; }
	QEventLoop loop;
	QFileSystemWatcher watcher;
	if (!watcher.addPath(QFileInfo(holdPath).absolutePath())) { return false; }
	QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged, &loop, [&loop, holdPath] {
		if (QFileInfo::exists(holdPath)) { loop.quit(); }
	});
	if (!QFileInfo::exists(holdPath)) { loop.exec(); }
	return true;
}

QString fakeRenderOutput(int argc, char** argv)
{
	for (int i = 2; i + 1 < argc; ++i)
	{
		if (QString::fromLocal8Bit(argv[i]) == QLatin1String("-o")) { return QString::fromLocal8Bit(argv[++i]); }
	}
	return QString();
}

int writeFakeRenderOutput(int argc, char** argv)
{
	QFile rendered(fakeRenderOutput(argc, argv));
	if (!rendered.open(QIODevice::WriteOnly)) { return 5; }
	rendered.write("rendered output");
	return 0;
}

int fakeRender(int argc, char** argv)
{
	const int pidError = writeFakeRenderPid();
	if (pidError != 0) { return pidError; }
	if (!waitForFakeRenderRelease()) { return 7; }
	if (std::getenv("ZENE_FAKE_RENDER_SUCCESS") != nullptr) { return writeFakeRenderOutput(argc, argv); }
	const char* diagnostic = std::getenv("ZENE_FAKE_RENDER_DIAGNOSTIC");
	if (diagnostic != nullptr) { std::fputs(diagnostic, stderr); std::fputc('\n', stderr); }
	return 3;
}

class ThreadPoolGate
{
public:
	ThreadPoolGate() : m_pool(*QThreadPool::globalInstance()), m_previousLimit(m_pool.maxThreadCount())
	{
		m_pool.setMaxThreadCount(1);
		m_pool.start([this] { m_entered.release(); m_release.acquire(); });
		m_entered.acquire();
	}

	~ThreadPoolGate()
	{
		releaseAndWait();
		m_pool.setMaxThreadCount(m_previousLimit);
	}

	void releaseAndWait()
	{
		if (!m_released) { m_released = true; m_release.release(); }
		m_pool.waitForDone();
	}

private:
	QThreadPool& m_pool;
	int m_previousLimit;
	QSemaphore m_entered;
	QSemaphore m_release;
	bool m_released = false;
};

class ReleaseFileGuard
{
public:
	explicit ReleaseFileGuard(QString path) : m_path(std::move(path)) {}
	~ReleaseFileGuard() { release(); }

	bool release()
	{
		if (QFileInfo::exists(m_path)) { return true; }
		QFile file(m_path);
		if (!file.open(QIODevice::WriteOnly)) { return false; }
		file.close();
		return true;
	}

private:
	QString m_path;
};

} // namespace

class TaskCenterTest : public QObject
{
	Q_OBJECT

private:
	int m_ownerA = 0;  //!< their addresses are the owners' keys
	int m_ownerB = 0;

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

	void init()
	{
		qunsetenv("ZENE_FAKE_RENDER_DIAGNOSTIC");
		qunsetenv("ZENE_FAKE_RENDER_SUCCESS");
		qunsetenv("ZENE_FAKE_RENDER_PID_PATH");
		qunsetenv("ZENE_FAKE_RENDER_HOLD_PATH");
		TaskCenter::instance().clearFinished();
	}

	void aJobIsListedFromBeginToEnd()
	{
		TaskCenter& tasks = TaskCenter::instance();
		QSignalSpy changed(&tasks, &TaskCenter::changed);
		const TaskToken token = tasks.begin(&m_ownerA, 7, {QStringLiteral("render"), QStringLiteral("Render a.wav"), {}, {}, true});
		const int id = tasks.idOf(&m_ownerA, 7);
		QVERIFY(id > 0);
		QCOMPARE(taskWithId(id).state, TaskState::Queued);
		QVERIFY(!taskWithId(id).cancellable);
		tasks.progress(token, 0.25, QStringLiteral("a quarter"));
		QCOMPARE(taskWithId(id).state, TaskState::Running);
		QCOMPARE(taskWithId(id).progress, 0.25);
		QCOMPARE(taskWithId(id).status, QStringLiteral("a quarter"));
		tasks.finish(token, TaskState::Done);
		QCOMPARE(taskWithId(id).state, TaskState::Done);
		QCOMPARE(taskWithId(id).progress, 1.0);
		QVERIFY(taskWithId(id).finishedMs >= taskWithId(id).startedMs);
		QCOMPARE(tasks.idOf(&m_ownerA, 7), -1);  // ended: no open task under that id
		QVERIFY(changed.count() >= 3);
		tasks.progress(token, 0.5);  // a late report for an ended job changes nothing
		QCOMPARE(taskWithId(id).state, TaskState::Done);
	}

	void aWorkersReportReachesTheMainThread()
	{
		TaskCenter& tasks = TaskCenter::instance();
		const TaskToken token = tasks.begin(&m_ownerA, 1, {QStringLiteral("index"), QStringLiteral("Index"), {}, {}, false});
		const int id = tasks.idOf(&m_ownerA, 1);
		std::thread worker([token] {
			TaskCenter::instance().progress(token, 0.5);
			TaskCenter::instance().finish(token, TaskState::Failed, QStringLiteral("disk full"));
		});
		worker.join();
		QCOMPARE(taskWithId(id).state, TaskState::Running);  // posted, not applied on the worker
		QTRY_COMPARE(taskWithId(id).state, TaskState::Failed);
		QCOMPARE(taskWithId(id).status, QStringLiteral("disk full"));
	}

	void aLateFinishDoesNotEndAReusedLocalId()
	{
		TaskCenter& tasks = TaskCenter::instance();
		const TaskToken oldToken = tasks.begin(&m_ownerA, 31, {QStringLiteral("index"), QStringLiteral("Old index"), {}, {}, false});
		std::thread worker([oldToken] {
			TaskCenter::instance().finish(oldToken, TaskState::Failed, QStringLiteral("old job"));
		});
		worker.join();

		const TaskToken newToken = tasks.begin(&m_ownerA, 31, {QStringLiteral("index"), QStringLiteral("New index"), {}, {}, false});
		const int newId = tasks.idOf(&m_ownerA, 31);
		drainTaskCenterEvents(tasks);
		const TaskInfo observed = taskWithId(newId);
		tasks.finish(newToken, TaskState::Done);  // ended before the asserts, so a failure leaves no open task
		QCOMPARE(observed.state, TaskState::Running);
		QVERIFY(observed.status.isEmpty());
	}

	void aLateProgressDoesNotChangeAReusedLocalId()
	{
		TaskCenter& tasks = TaskCenter::instance();
		const TaskToken oldToken = tasks.begin(&m_ownerA, 32, {QStringLiteral("index"), QStringLiteral("Old index"), {}, {}, false});
		std::thread worker([oldToken] {
			TaskCenter::instance().progress(oldToken, 0.75, QStringLiteral("old progress"));
		});
		worker.join();

		const TaskToken newToken = tasks.begin(&m_ownerA, 32, {QStringLiteral("index"), QStringLiteral("New index"), {}, {}, false});
		const int newId = tasks.idOf(&m_ownerA, 32);
		drainTaskCenterEvents(tasks);
		const TaskInfo observed = taskWithId(newId);
		tasks.finish(newToken, TaskState::Done);  // ended before the asserts, so a failure leaves no open task
		QCOMPARE(observed.state, TaskState::Running);
		QCOMPARE(observed.progress, -1.0);
		QVERIFY(observed.status.isEmpty());
	}

	void cancelRunsTheJobsOwnCancel()
	{
		TaskCenter& tasks = TaskCenter::instance();
		int calls = 0;
		const TaskToken cancellableToken = tasks.begin(&m_ownerA, 2, {QStringLiteral("stem_split"), QStringLiteral("Split"), [&calls] { ++calls; }, {}, false});
		const TaskToken fixedToken = tasks.begin(&m_ownerA, 3, {QStringLiteral("mastering"), QStringLiteral("Master"), {}, {}, false});
		const int cancellable = tasks.idOf(&m_ownerA, 2);
		const int fixed = tasks.idOf(&m_ownerA, 3);
		QVERIFY(taskWithId(cancellable).cancellable);
		QVERIFY(tasks.cancel(cancellable));
		QCOMPARE(calls, 1);
		QVERIFY(taskWithId(cancellable).active());  // the job ends itself, at its next safe point
		tasks.finish(cancellableToken, TaskState::Cancelled);
		QVERIFY(!tasks.cancel(cancellable));  // ended
		QVERIFY(!tasks.cancel(fixed));        // runs to its end
		QVERIFY(!tasks.cancel(-5));
		tasks.finish(fixedToken, TaskState::Done);
		QCOMPARE(calls, 1);
	}

	void aProbeIsReadWhenTheListIs()
	{
		TaskCenter& tasks = TaskCenter::instance();
		double now = 0.1;
		const TaskToken token = tasks.begin(&m_ownerB, 1, {QStringLiteral("video_audio"), QStringLiteral("Extract"), {}, [&now] { return now; }, false});
		const int id = tasks.idOf(&m_ownerB, 1);
		QCOMPARE(taskWithId(id).progress, 0.1);
		now = 0.8;
		QCOMPARE(taskWithId(id).progress, 0.8);
		tasks.finish(token, TaskState::Done);
		now = 0.2;
		QCOMPARE(taskWithId(id).progress, 1.0);  // an ended task's probe is dropped
	}

	void anOwnerGoingAwayEndsItsTasks()
	{
		TaskCenter& tasks = TaskCenter::instance();
		tasks.begin(&m_ownerB, 4, {QStringLiteral("render"), QStringLiteral("R4"), [] {}, {}, true});
		tasks.begin(&m_ownerB, 5, {QStringLiteral("render"), QStringLiteral("R5"), [] {}, {}, false});
		const int four = tasks.idOf(&m_ownerB, 4);
		tasks.forget(&m_ownerB);
		QCOMPARE(taskWithId(four).state, TaskState::Cancelled);
		QCOMPARE(tasks.idOf(&m_ownerB, 5), -1);
	}

	void theListKeepsABoundedHistory()
	{
		TaskCenter& tasks = TaskCenter::instance();
		for (int i = 0; i < TaskCenter::MaxFinished + 5; ++i)
		{
			const TaskToken token = tasks.begin(&m_ownerA, 100 + i, {QStringLiteral("index"), QStringLiteral("I"), {}, {}, false});
			tasks.finish(token, TaskState::Done);
		}
		QVERIFY(static_cast<int>(tasks.tasks().size()) <= TaskCenter::MaxFinished);
		QVERIFY(tasks.clearFinished() > 0);
		QVERIFY(tasks.tasks().empty());
	}

	void theCommandsListCancelAndClear()
	{
		TaskCenter& tasks = TaskCenter::instance();
		bool stopped = false;
		const TaskToken token = tasks.begin(&m_ownerA, 9, {QStringLiteral("stem_split"), QStringLiteral("Split mix"), [&stopped] { stopped = true; }, {}, false});
		const int id = tasks.idOf(&m_ownerA, 9);
		const ControlResult listed = run(QStringLiteral("task.list"));
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		QCOMPARE(listed.result.value(QStringLiteral("active")).toInt(), 1);
		const QJsonObject row = listed.result.value(QStringLiteral("tasks")).toArray().last().toObject();
		QCOMPARE(row.value(QStringLiteral("id")).toInt(), id);
		QCOMPARE(row.value(QStringLiteral("state")).toString(), QStringLiteral("running"));
		QVERIFY(row.value(QStringLiteral("cancellable")).toBool());

		QCOMPARE(run(QStringLiteral("task.cancel"), {{QStringLiteral("id"), 99999}}).errorKind, ControlErrorKind::NotFound);
		const ControlResult cancel = run(QStringLiteral("task.cancel"), {{QStringLiteral("id"), id}});
		QVERIFY2(cancel.ok, qPrintable(cancel.errorMessage));
		QVERIFY(stopped);
		tasks.finish(token, TaskState::Cancelled);
		QCOMPARE(run(QStringLiteral("task.cancel"), {{QStringLiteral("id"), id}}).errorKind, ControlErrorKind::Refused);
		const ControlResult cleared = run(QStringLiteral("task.clear_finished"));
		QVERIFY2(cleared.ok, qPrintable(cleared.errorMessage));
		QVERIFY(cleared.result.value(QStringLiteral("removed")).toInt() >= 1);
		QCOMPARE(taskWithId(id).id, 0);
	}

	void convertingAnOldSuffixedSourcePreservesIt()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString source = dir.filePath(QStringLiteral("song.wav.old"));
		const QString output = dir.filePath(QStringLiteral("song.wav"));
		QVERIFY(browserfixture::writeWave(source, browserfixture::WaveSpec{}));

		QFile sourceFile(source);
		QVERIFY(sourceFile.open(QIODevice::ReadOnly));
		const QByteArray original = sourceFile.readAll();
		sourceFile.close();

		RenderJob job;
		job.kind = RenderJob::Kind::Convert;
		job.source = source;
		job.out = output;
		const QString error = RenderQueue::convert(job);
		QVERIFY2(error.isEmpty(), qPrintable(error));

		QFile preservedSource(source);
		QVERIFY(preservedSource.open(QIODevice::ReadOnly));
		QCOMPARE(preservedSource.readAll(), original);
		QVERIFY(QFile::exists(output));
	}

	void failedRenderSurfacesChildDiagnostics()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		qputenv("ZENE_FAKE_RENDER_DIAGNOSTIC", "encoder refused the requested output");
		RenderJob job;
		job.kind = RenderJob::Kind::Render;
		job.source = dir.filePath(QStringLiteral("project.mmp"));
		job.out = dir.filePath(QStringLiteral("output.wav"));
		RenderQueue& queue = RenderQueue::instance();
		const int id = queue.add(job);
		QVERIFY(id > 0);
		queue.start();
		QTRY_VERIFY_WITH_TIMEOUT(queue.job(id) != nullptr && queue.job(id)->state == RenderJob::State::Failed, 5000);
		QVERIFY(queue.job(id)->error.contains(QStringLiteral("encoder refused the requested output")));
		queue.clearFinished();
	}

	void cancellingRenderPreservesPreviousDestination()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString destination = dir.filePath(QStringLiteral("existing.wav"));
		QFile previous(destination);
		QVERIFY(previous.open(QIODevice::WriteOnly));
		QCOMPARE(previous.write("previous output"), qint64(15));
		previous.close();

		const QString pidPath = dir.filePath(QStringLiteral("child.pid"));
		qputenv("ZENE_FAKE_RENDER_PID_PATH", pidPath.toLocal8Bit());
		qputenv("ZENE_FAKE_RENDER_HOLD_PATH", dir.filePath(QStringLiteral("release-child")).toLocal8Bit());
		RenderJob job;
		job.kind = RenderJob::Kind::Render;
		job.source = dir.filePath(QStringLiteral("project.mmp"));
		job.out = destination;
		RenderQueue& queue = RenderQueue::instance();
		const int id = queue.add(job);
		QVERIFY(id > 0);
		queue.start();
		QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pidPath), 5000);
		QVERIFY(queue.cancel(id));
		const QDateTime futureTime = QDateTime::currentDateTimeUtc().addDays(2);
		QVERIFY(previous.open(QIODevice::ReadWrite));
		QVERIFY(previous.setFileTime(futureTime, QFileDevice::FileModificationTime));
		previous.close();
		QVERIFY(QFileInfo(destination).lastModified().toMSecsSinceEpoch() >= futureTime.toMSecsSinceEpoch());
		QVERIFY(QFileInfo(destination).lastModified().toMSecsSinceEpoch() >= queue.job(id)->startedMs);
		QTRY_VERIFY_WITH_TIMEOUT(!queue.isRunning(), 5000);
		QCOMPARE(queue.job(id)->state, RenderJob::State::Cancelled);
		QVERIFY(previous.open(QIODevice::ReadOnly));
		QCOMPARE(previous.readAll(), QByteArray("previous output"));
		if (!queue.job(id)->staged.isEmpty()) { QVERIFY(!QFileInfo::exists(queue.job(id)->staged)); }
		queue.clearFinished();
	}

	void successfulRenderReplacesDestinationFromStaging()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString destination = dir.filePath(QStringLiteral("render.wav"));
		QFile previous(destination);
		QVERIFY(previous.open(QIODevice::WriteOnly));
		QCOMPARE(previous.write("previous output"), qint64(15));
		previous.close();
		qputenv("ZENE_FAKE_RENDER_SUCCESS", "1");
		RenderJob job;
		job.kind = RenderJob::Kind::Render;
		job.source = dir.filePath(QStringLiteral("project.mmp"));
		job.out = destination;
		RenderQueue& queue = RenderQueue::instance();
		const int id = queue.add(job);
		QVERIFY(id > 0);
		queue.start();
		QTRY_VERIFY_WITH_TIMEOUT(queue.job(id) != nullptr && queue.job(id)->state == RenderJob::State::Done, 5000);
		QVERIFY(previous.open(QIODevice::ReadOnly));
		QCOMPARE(previous.readAll(), QByteArray("rendered output"));
		queue.clearFinished();
	}

	void cancellingConversionStopsWorkerAndAdvancesQueue()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString source = dir.filePath(QStringLiteral("source.wav"));
		QVERIFY(browserfixture::writeWave(source, browserfixture::WaveSpec{}));
		const QString pidPath = dir.filePath(QStringLiteral("render.pid"));
		const QString releasePath = dir.filePath(QStringLiteral("release-render"));
		ReleaseFileGuard renderRelease(releasePath);
		qputenv("ZENE_FAKE_RENDER_SUCCESS", "1");
		qputenv("ZENE_FAKE_RENDER_PID_PATH", pidPath.toLocal8Bit());
		qputenv("ZENE_FAKE_RENDER_HOLD_PATH", releasePath.toLocal8Bit());
		ThreadPoolGate workerGate;
		SampleDecoder::resetDecodeCallCountsForTesting();
		RenderQueue& queue = RenderQueue::instance();
		RenderJob conversion;
		conversion.kind = RenderJob::Kind::Convert;
		conversion.source = source;
		conversion.out = dir.filePath(QStringLiteral("converted.wav"));
		const int conversionId = queue.add(conversion);
		QVERIFY(conversionId > 0);
		queue.start();
		QCOMPARE(queue.job(conversionId)->state, RenderJob::State::Running);
		QVERIFY(queue.cancel(conversionId));

		RenderJob render;
		render.kind = RenderJob::Kind::Render;
		render.source = dir.filePath(QStringLiteral("project.mmp"));
		render.out = dir.filePath(QStringLiteral("next.wav"));
		const int renderId = queue.add(render);
		QVERIFY(renderId > 0);
		queue.start();
		const bool advancedWhileWorkerBlocked = queue.job(renderId)->state == RenderJob::State::Running;
		if (advancedWhileWorkerBlocked) { QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pidPath), 5000); }
		workerGate.releaseAndWait();
		QCoreApplication::sendPostedEvents(&queue, QEvent::MetaCall);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QCoreApplication::processEvents(QEventLoop::AllEvents);
		QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pidPath), 5000);
		QVERIFY(renderRelease.release());
		QTRY_VERIFY_WITH_TIMEOUT(queue.job(renderId)->state == RenderJob::State::Done, 5000);
		QCOMPARE(queue.job(conversionId)->state, RenderJob::State::Cancelled);
		QVERIFY(!QFileInfo::exists(queue.job(conversionId)->staged));
		QVERIFY(advancedWhileWorkerBlocked);
		QCOMPARE(SampleDecoder::decodeCallCountsForTesting().full, std::uint64_t(0));
		queue.clearFinished();
	}

	void aQueuedRenderIsListedAndCancelledThroughTheList()
	{
		QTemporaryDir dir;
		RenderJob job;
		job.kind = RenderJob::Kind::Convert;
		job.source = dir.filePath(QStringLiteral("in.wav"));
		job.out = dir.filePath(QStringLiteral("out.wav"));
		const int queued = RenderQueue::instance().add(job);  // not started: it waits its turn
		QVERIFY(queued > 0);
		const int id = TaskCenter::instance().idOf(&RenderQueue::instance(), queued);
		QVERIFY(id > 0);
		QCOMPARE(taskWithId(id).state, TaskState::Queued);
		QCOMPARE(taskWithId(id).kind, QStringLiteral("convert"));
		QCOMPARE(taskWithId(id).title, QStringLiteral("Convert out.wav"));
		QVERIFY(run(QStringLiteral("task.cancel"), {{QStringLiteral("id"), id}}).ok);
		QCOMPARE(RenderQueue::instance().job(queued)->state, RenderJob::State::Cancelled);
		QCOMPARE(taskWithId(id).state, TaskState::Cancelled);
		RenderQueue::instance().clearFinished();
	}
};

int main(int argc, char** argv)
{
	if (argc > 1 && std::strcmp(argv[1], "render") == 0)
	{
		QCoreApplication app(argc, argv);
		return fakeRender(argc, argv);
	}
	QCoreApplication app(argc, argv);
	QTEST_SET_MAIN_SOURCE_PATH
	TaskCenterTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "TaskCenterTest.moc"
