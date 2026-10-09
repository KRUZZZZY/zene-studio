/*
 * ControlRenderJobTest.cpp - asynchronous render jobs, main-thread apply, stale refusal and cancellation
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

#include <QDir>
#include <QDomDocument>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <vector>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

#include <sndfile.h>

#include "ArrangementEdits.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "RenderQueue.h"
#include "Song.h"
#include "TaskCenter.h"
#include "Track.h"

using namespace lmms;

namespace
{

constexpr int FakeRate = 44100;
constexpr int FakeSeconds = 2;

struct RenderRequest
{
	QString project;
	QString out;
	int rate = FakeRate;
};

RenderRequest parseRenderArguments(int argc, char** argv)
{
	RenderRequest request;
	if (argc > 2) { request.project = QString::fromLocal8Bit(argv[2]); }
	for (int i = 3; i + 1 < argc; ++i)
	{
		const QString arg = QString::fromLocal8Bit(argv[i]);
		if (arg == QLatin1String("-o")) { request.out = QString::fromLocal8Bit(argv[++i]); }
		else if (arg == QLatin1String("-s")) { request.rate = QString::fromLocal8Bit(argv[++i]).toInt(); }
	}
	return request;
}

int audibleTracks(const QString& project)
{
	QFile file(project);
	QDomDocument document;
	if (!file.open(QIODevice::ReadOnly) || !document.setContent(&file)) { return -1; }
	int audible = 0;
	const QDomNodeList tracks = document.elementsByTagName(QStringLiteral("track"));
	for (int i = 0; i < tracks.count(); ++i)
	{
		const QDomElement track = tracks.at(i).toElement();
		if (track.attribute(QStringLiteral("type")).toInt() <= 2 && track.attribute(QStringLiteral("muted")).toInt() == 0) { ++audible; }
	}
	return audible;
}

bool writeTone(const QString& path, int rate, double level)
{
	SF_INFO info{};
	info.channels = 2;
	info.samplerate = rate;
	info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
	SNDFILE* wav = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
	if (wav == nullptr) { return false; }
	std::vector<float> block(2 * 1024);
	const qint64 total = static_cast<qint64>(rate) * FakeSeconds;
	for (qint64 done = 0; done < total; done += 1024)
	{
		const int frames = static_cast<int>(std::min<qint64>(1024, total - done));
		for (int f = 0; f < frames; ++f)
		{
			block[2 * f] = block[2 * f + 1] = static_cast<float>(level * std::sin(2.0 * M_PI * 440.0 * static_cast<double>(done + f) / rate));
		}
		sf_writef_float(wav, block.data(), frames);
	}
	return sf_close(wav) == 0;
}

int fakeRender(int argc, char** argv)
{
	if (std::getenv("ZENE_FAKE_RENDER_FAIL") != nullptr) { return 3; }
	const QString pidPath = qEnvironmentVariable("ZENE_FAKE_RENDER_PID_PATH");
	if (!pidPath.isEmpty())
	{
		QFile pidFile(pidPath);
		if (!pidFile.open(QIODevice::WriteOnly)) { return 6; }
		pidFile.write(QByteArray::number(QCoreApplication::applicationPid()));
	}
	const int delay = qEnvironmentVariableIntValue("ZENE_FAKE_RENDER_DELAY_MS");
	if (delay > 0) { QThread::msleep(static_cast<unsigned long>(delay)); }
	const RenderRequest request = parseRenderArguments(argc, argv);
	const int audible = audibleTracks(request.project);
	if (request.out.isEmpty() || audible < 0) { return 4; }
	// main.cpp replaces the requested suffix with the selected render format;
	// the BounceInPlace child always asks for WAV.
	const QFileInfo requested(request.out);
	const QString output = requested.dir().filePath(requested.completeBaseName() + QStringLiteral(".wav"));
	return writeTone(output, request.rate, 0.1 * std::max(1, audible)) ? 0 : 5;
}

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonObject waitForJob(int id, int timeoutMs = 10000)
{
	QElapsedTimer timer;
	timer.start();
	QJsonObject last;
	while (timer.elapsed() < timeoutMs)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		const ControlResult report = run(QStringLiteral("render.queue_report"), {{QStringLiteral("job"), id}});
		if (!report.ok) { return {}; }
		last = report.result;
		const QString state = last.value(QStringLiteral("state")).toString();
		const bool cancelFinished = state == QLatin1String("cancelled")
			&& last.value(QStringLiteral("phase")).toString() != QLatin1String("cancelling");
		if (state == QLatin1String("done") || state == QLatin1String("failed") || cancelFinished) { return last; }
		QTest::qWait(5);
	}
	return last;
}

Track* trackWithClip(const QString& name)
{
	Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
	track->setName(name);
	track->createClip(TimePos(0));
	return track;
}

int trackCount() { return static_cast<int>(Engine::getSong()->tracks().size()); }

bool processIsRunning(qint64 pid)
{
#if defined(Q_OS_WIN)
	HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
	if (process == nullptr) { return false; }
	DWORD code = 0;
	const bool running = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
	CloseHandle(process);
	return running;
#else
	if (kill(static_cast<pid_t>(pid), 0) == 0) { return true; }
	return errno != ESRCH;
#endif
}

qint64 processIdIn(const QString& path)
{
	QFile pidFile(path);
	if (!pidFile.open(QIODevice::ReadOnly)) { return 0; }
	return pidFile.readAll().toLongLong();
}

ControlResult queueBounce(const QString& operation, const QString& trackId, const QString& out,
		const QJsonObject& extra = {})
{
	QJsonObject args = extra;
	args.insert(QStringLiteral("operation"), operation);
	if (!trackId.isEmpty()) { args.insert(QStringLiteral("track"), trackId); }
	if (!out.isEmpty()) { args.insert(QStringLiteral("out"), out); }
	return run(QStringLiteral("render.queue_bounce"), args);
}

ControlResult queueOutputBounce(const QString& trackId, const QString& out)
{
	return run(QStringLiteral("render.queue_add"), {{QStringLiteral("operation"), QStringLiteral("bounce")},
		{QStringLiteral("track"), trackId}, {QStringLiteral("out"), out}});
}

int jobId(const ControlResult& accepted) { return accepted.result.value(QStringLiteral("job")).toInt(); }

} // namespace

class ControlRenderJobTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;

	QString out(const QString& name) const { return QDir(m_dir.path()).filePath(name); }

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
		qunsetenv("ZENE_FAKE_RENDER_FAIL");
		qunsetenv("ZENE_FAKE_RENDER_DELAY_MS");
		qunsetenv("ZENE_FAKE_RENDER_PID_PATH");
		run(QStringLiteral("render.queue_cancel"), {{QStringLiteral("all"), true}});
		run(QStringLiteral("render.queue_clear"));
		ArrangerRange::instance().clear();
		Engine::getSong()->clearProject();
	}

	void acknowledgesImmediatelyAndMatchesTheSynchronousRender()
	{
		Track* source = trackWithClip(QStringLiteral("Solo"));
		const QString id = control::trackIdOf(source);
		const ControlResult synchronous = run(QStringLiteral("bounce.in_place"), {{QStringLiteral("track"), id},
			{QStringLiteral("out"), out(QStringLiteral("sync.wav"))}});
		QVERIFY2(synchronous.ok, qPrintable(synchronous.errorMessage));
		const int before = trackCount();
		const ControlResult accepted = queueOutputBounce(id, out(QStringLiteral("async.wav")));
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		const int idNumber = jobId(accepted);
		QVERIFY(idNumber > 0);
		QCOMPARE(trackCount(), before);
		const ControlResult listed = run(QStringLiteral("render.queue_list"));
		QVERIFY(listed.ok);
		QVERIFY(!listed.result.value(QStringLiteral("jobs")).toArray().isEmpty());
		const QJsonObject done = waitForJob(idNumber);
		QCOMPARE(done.value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		const QJsonObject facts = done.value(QStringLiteral("report")).toObject();
		QCOMPARE(facts.value(QStringLiteral("sha256")).toString(), synchronous.result.value(QStringLiteral("sha256")).toString());
	}

	void queuedMutationsApplyOnCompletionAndUndoOnce()
	{
		Track* frozen = trackWithClip(QStringLiteral("Freeze"));
		ControlResult accepted = queueBounce(QStringLiteral("freeze_track"), control::trackIdOf(frozen), out(QStringLiteral("freeze.wav")));
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		QCOMPARE(trackCount(), 1);
		QCOMPARE(waitForJob(jobId(accepted)).value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		QVERIFY(frozen->isFrozen());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!frozen->isFrozen());

		Engine::getSong()->clearProject();
		trackWithClip(QStringLiteral("Before"));
		Track* flatSource = trackWithClip(QStringLiteral("Flat"));
		trackWithClip(QStringLiteral("After"));
		accepted = queueBounce(QStringLiteral("flatten"), control::trackIdOf(flatSource), out(QStringLiteral("flatten.wav")));
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		QCOMPARE(waitForJob(jobId(accepted)).value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		QCOMPARE(trackCount(), 3);
		QCOMPARE(Engine::getSong()->tracks().at(1)->type(), Track::Type::Sample);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::getSong()->tracks().at(1)->type(), Track::Type::Instrument);
		QCOMPARE(Engine::getSong()->tracks().at(1)->name(), QStringLiteral("Flat"));

		Engine::getSong()->clearProject();
		Track* first = trackWithClip(QStringLiteral("A"));
		Track* second = trackWithClip(QStringLiteral("B"));
		Track* third = trackWithClip(QStringLiteral("C"));
		QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 192},
			{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(first), control::trackIdOf(second)}}}).ok);
		accepted = queueBounce(QStringLiteral("range_bounce"), QString(), out(QStringLiteral("range.wav")));
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		QCOMPARE(waitForJob(jobId(accepted)).value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		QCOMPARE(trackCount(), 4);
		QVERIFY(first->isMuted() && second->isMuted() && !third->isMuted());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(trackCount(), 3);
		QVERIFY(!first->isMuted() && !second->isMuted() && !third->isMuted());
	}

	void rangeBounceAtUndoCapIsOneStep()
	{
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		journal->clearJournal();
		QVERIFY(journal->setMaxUndoStates(100));
		Track* first = trackWithClip(QStringLiteral("Cap A"));
		Track* second = trackWithClip(QStringLiteral("Cap B"));
		trackWithClip(QStringLiteral("Cap C"));
		QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 192},
			{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(first), control::trackIdOf(second)}}}).ok);
		for (int i = 0; i < 100; ++i) { journal->addJournalAction([] {}, [] {}); }
		QCOMPARE(journal->undoDepth(), 100);
		const quint64 serialBefore = journal->topStepSerial();
		const ControlResult accepted = queueBounce(QStringLiteral("range_bounce"), QString(), out(QStringLiteral("cap-range.wav")));
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		QCOMPARE(waitForJob(jobId(accepted)).value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		QVERIFY(first->isMuted() && second->isMuted());
		QCOMPARE(journal->retainedStepsFrom(serialBefore + 1), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(journal->retainedStepsFrom(serialBefore + 1), 0);
		QCOMPARE(trackCount(), 3);
		QVERIFY(!first->isMuted() && !second->isMuted());
	}

	void regionFreezeAndBounceLandApplyAndUndoOnce()
	{
		Track* region = trackWithClip(QStringLiteral("Region"));
		ControlResult accepted = queueBounce(QStringLiteral("freeze_region"), control::trackIdOf(region),
			out(QStringLiteral("region.wav")), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 192}});
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		QCOMPARE(waitForJob(jobId(accepted)).value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		QVERIFY(region->isFrozen());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!region->isFrozen());

		Engine::getSong()->clearProject();
		Track* source = trackWithClip(QStringLiteral("Landed"));
		accepted = queueBounce(QStringLiteral("bounce_land"), control::trackIdOf(source), out(QStringLiteral("landed.wav")));
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		QCOMPARE(waitForJob(jobId(accepted)).value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		QCOMPARE(trackCount(), 2);
		QVERIFY(source->isMuted());
		QVERIFY(Engine::getSong()->tracks().back()->type() == Track::Type::Sample);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(trackCount(), 1);
		QVERIFY(!source->isMuted());
	}

	void cancellationUsesTaskAndAgentSurfacesAndPreservesDestination()
	{
		Track* source = trackWithClip(QStringLiteral("Pending"));
		const QString path = out(QStringLiteral("existing.wav"));
		QFile old(path);
		QVERIFY(old.open(QIODevice::WriteOnly));
		QCOMPARE(old.write("previous"), qint64(8));
		old.close();
		const QString taskPidFile = out(QStringLiteral("task-cancel.pid"));
		qputenv("ZENE_FAKE_RENDER_PID_PATH", taskPidFile.toLocal8Bit());
		qputenv("ZENE_FAKE_RENDER_DELAY_MS", "2000");
		ControlResult accepted = queueOutputBounce(control::trackIdOf(source), path);
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		const int id = jobId(accepted);
		QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(taskPidFile), 3000);
		const qint64 taskPid = processIdIn(taskPidFile);
		QVERIFY(taskPid > 0);
		const QString taskScratch = RenderQueue::instance().job(id)->bounce.scratch->path();
		const int task = TaskCenter::instance().idOf(&RenderQueue::instance(), id);
		QVERIFY(task > 0);
		QCOMPARE(TaskCenter::instance().cancel(task), true);
		QCOMPARE(waitForJob(id).value(QStringLiteral("state")).toString(), QStringLiteral("cancelled"));
		QVERIFY(!processIsRunning(taskPid));
		QVERIFY(!QFileInfo::exists(taskScratch));
		QFile preserved(path);
		QVERIFY(preserved.open(QIODevice::ReadOnly));
		QCOMPARE(preserved.readAll(), QByteArray("previous"));
		QCOMPARE(trackCount(), 1);

		const QString agentPidFile = out(QStringLiteral("agent-cancel.pid"));
		qputenv("ZENE_FAKE_RENDER_PID_PATH", agentPidFile.toLocal8Bit());
		accepted = queueOutputBounce(control::trackIdOf(source), out(QStringLiteral("agent-cancel.wav")));
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		const int agentId = jobId(accepted);
		QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(agentPidFile), 3000);
		const qint64 agentPid = processIdIn(agentPidFile);
		QVERIFY(agentPid > 0);
		const QString agentScratch = RenderQueue::instance().job(agentId)->bounce.scratch->path();
		QVERIFY(run(QStringLiteral("render.queue_cancel"), {{QStringLiteral("job"), jobId(accepted)}}).ok);
		QCOMPARE(waitForJob(agentId).value(QStringLiteral("state")).toString(), QStringLiteral("cancelled"));
		QVERIFY(!processIsRunning(agentPid));
		QVERIFY(!QFileInfo::exists(agentScratch));
	}

	void reloadingProjectDuringBounceRefusesStaleSession()
	{
		Song* song = Engine::getSong();
		Track* source = trackWithClip(QStringLiteral("Reloaded"));
		const int originalTrackId = source->id();
		const QString projectPath = out(QStringLiteral("reload.mmp"));
		QVERIFY(song->saveProjectFile(projectPath));
		const QString output = out(QStringLiteral("reload.wav"));
		QFile previous(output);
		QVERIFY(previous.open(QIODevice::WriteOnly));
		QCOMPARE(previous.write("previous"), qint64(8));
		previous.close();
		const QString pidPath = out(QStringLiteral("reload.pid"));
		qputenv("ZENE_FAKE_RENDER_PID_PATH", pidPath.toLocal8Bit());
		qputenv("ZENE_FAKE_RENDER_DELAY_MS", "1500");
		const ControlResult accepted = queueBounce(QStringLiteral("freeze_track"), control::trackIdOf(source), output);
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pidPath), 3000);
		song->loadProject(projectPath);
		QVERIFY(song->loadRefusal().isEmpty());
		QCOMPARE(song->tracks().front()->id(), originalTrackId);
		const QJsonObject failed = waitForJob(jobId(accepted));
		QCOMPARE(failed.value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
		QVERIFY(failed.value(QStringLiteral("error")).toString().contains(QStringLiteral("project was replaced or reloaded")));
		QVERIFY(!song->tracks().front()->isFrozen());
		QFile kept(output);
		QVERIFY(kept.open(QIODevice::ReadOnly));
		QCOMPARE(kept.readAll(), QByteArray("previous"));
	}

	void outputOnlyTrackBounceUsesNotMutatingQueueCommand()
	{
		const auto* outputClass = control::ReversibilityTable::instance().lookup(QStringLiteral("render.queue_add"));
		QVERIFY(outputClass != nullptr);
		QCOMPARE(outputClass->cls, control::ReversibilityClass::NotMutating);
		Track* source = trackWithClip(QStringLiteral("Output only"));
		const ControlResult oldRoute = queueBounce(QStringLiteral("bounce"), control::trackIdOf(source), out(QStringLiteral("old-route.wav")));
		QCOMPARE(oldRoute.ok, false);
		const int undoDepth = Engine::projectJournal()->undoDepth();
		const ControlResult accepted = run(QStringLiteral("render.queue_add"), {{QStringLiteral("operation"), QStringLiteral("bounce")},
			{QStringLiteral("track"), control::trackIdOf(source)}, {QStringLiteral("out"), out(QStringLiteral("output-only.wav"))}});
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		const int id = accepted.result.value(QStringLiteral("job")).toInt();
		QCOMPARE(RenderQueue::instance().job(id)->operation, QStringLiteral("bounce"));
		QCOMPARE(waitForJob(id).value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		QCOMPARE(Engine::projectJournal()->undoDepth(), undoDepth);
		QCOMPARE(trackCount(), 1);
	}

	void staleTargetAndCapacityRefusalAreSafeAndVisible()
	{
		Track* source = trackWithClip(QStringLiteral("Stale"));
		const QString sourceId = control::trackIdOf(source);
		const QString path = out(QStringLiteral("stale.wav"));
		QFile old(path);
		QVERIFY(old.open(QIODevice::WriteOnly));
		QCOMPARE(old.write("previous"), qint64(8));
		old.close();
		ControlResult accepted = queueBounce(QStringLiteral("flatten"), sourceId, path);
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		source->setName(QStringLiteral("Changed"));
		const QJsonObject failed = waitForJob(jobId(accepted));
		QCOMPARE(failed.value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
		QVERIFY(failed.value(QStringLiteral("error")).toString().contains(QStringLiteral("changed")));
		QCOMPARE(Engine::getSong()->tracks().front(), source);
		QCOMPARE(trackCount(), 1);
		QFile preserved(path);
		QVERIFY(preserved.open(QIODevice::ReadOnly));
		QCOMPARE(preserved.readAll(), QByteArray("previous"));

		Engine::getSong()->clearProject();
		Track* first = trackWithClip(QStringLiteral("Range A"));
		Track* second = trackWithClip(QStringLiteral("Range B"));
		QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 192},
			{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(first)}}}).ok);
		const QString rangePath = out(QStringLiteral("stale-range.wav"));
		QFile oldRange(rangePath);
		QVERIFY(oldRange.open(QIODevice::WriteOnly));
		QCOMPARE(oldRange.write("previous-range"), qint64(14));
		oldRange.close();
		accepted = queueBounce(QStringLiteral("range_bounce"), QString(), rangePath);
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 384},
			{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(first)}}}).ok);
		const QJsonObject staleRange = waitForJob(jobId(accepted));
		QCOMPARE(staleRange.value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
		QVERIFY(staleRange.value(QStringLiteral("error")).toString().contains(QStringLiteral("range")));
		QCOMPARE(trackCount(), 2);
		QVERIFY(!first->isMuted() && !second->isMuted());
		QFile keptRange(rangePath);
		QVERIFY(keptRange.open(QIODevice::ReadOnly));
		QCOMPARE(keptRange.readAll(), QByteArray("previous-range"));

		run(QStringLiteral("render.queue_clear"));
		for (int i = 0; i < RenderQueue::MaxJobs; ++i) { QVERIFY(RenderQueue::instance().add(RenderJob{}) > 0); }
		const ControlResult refused = queueOutputBounce(sourceId, out(QStringLiteral("full.wav")));
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QCOMPARE(RenderQueue::instance().capacityRefusals(), quint64(1));
		QCOMPARE(run(QStringLiteral("control.ping")).result.value(QStringLiteral("render_queue_capacity_refusals")).toInt(), 1);
		QVERIFY(run(QStringLiteral("render.queue_cancel"), {{QStringLiteral("all"), true}}).ok);
		QCOMPARE(RenderQueue::instance().clearFinished(), RenderQueue::MaxJobs);
	}
};

int main(int argc, char** argv)
{
	if (argc > 1 && std::strcmp(argv[1], "render") == 0) { return fakeRender(argc, argv); }
	QCoreApplication app(argc, argv);
	QTEST_SET_MAIN_SOURCE_PATH
	ControlRenderJobTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "ControlRenderJobTest.moc"
