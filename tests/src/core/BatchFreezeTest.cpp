/*
 * BatchFreezeTest.cpp - Q050-327: freeze.tracks renders concurrently as one task, applies as one undo step,
 *                       and leaves every track as it was on cancel, failure or a changed target
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

// The executable is also its own stand-in render child: BounceInPlace starts `<app> render <project> -o <out>`,
// and main() answers that by writing a tone (ControlRenderJobTest's pattern). Environment knobs:
//   ZENE_FAKE_BARRIER_DIR / ZENE_FAKE_BARRIER_COUNT - each child registers there and waits until COUNT children
//     are running at once; a child that waits 6 s alone exits 7, so a SERIAL batch cannot pass.
//   ZENE_FAKE_FAIL_TRACK / ZENE_FAKE_SLOW_TRACK (+ ZENE_FAKE_SLOW_MS) - the child rendering that track fails / waits.
//   ZENE_FAKE_PID_DIR - each child writes its pid there, named for its track.

#include <QtTest>

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>

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

#include "BatchFreeze.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "TaskCenter.h"
#include "Track.h"
#include "TrackRenderMenu.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! The names of the tracks the child's project leaves unmuted (BounceInPlace mutes every other one).
QStringList audibleNames(const QString& project)
{
	QFile file(project);
	QDomDocument document;
	QStringList names;
	if (!file.open(QIODevice::ReadOnly) || !document.setContent(&file)) { return names; }
	const QDomNodeList tracks = document.elementsByTagName(QStringLiteral("track"));
	for (int i = 0; i < tracks.count(); ++i)
	{
		const QDomElement track = tracks.at(i).toElement();
		if (track.attribute(QStringLiteral("muted")).toInt() == 0) { names << track.attribute(QStringLiteral("name")); }
	}
	return names;
}

bool writeTone(const QString& path, int rate)
{
	SF_INFO info{};
	info.channels = 2;
	info.samplerate = rate;
	info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
	SNDFILE* wav = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
	if (wav == nullptr) { return false; }
	std::vector<float> block(2 * 1024);
	for (int done = 0; done < rate; done += 1024)
	{
		for (int f = 0; f < 1024; ++f)
		{
			block[2 * f] = block[2 * f + 1] = static_cast<float>(0.1 * std::sin(2.0 * M_PI * 440.0 * (done + f) / rate));
		}
		sf_writef_float(wav, block.data(), 1024);
	}
	return sf_close(wav) == 0;
}

bool waitAtBarrier()
{
	const QString dir = qEnvironmentVariable("ZENE_FAKE_BARRIER_DIR");
	if (dir.isEmpty()) { return true; }
	const int count = qEnvironmentVariableIntValue("ZENE_FAKE_BARRIER_COUNT");
	QFile mark(QDir(dir).filePath(QString::number(QCoreApplication::applicationPid())));
	if (!mark.open(QIODevice::WriteOnly)) { return false; }
	mark.close();
	for (int waited = 0; waited < 6000; waited += 20)
	{
		if (QDir(dir).entryList(QDir::Files).size() >= count) { return true; }
		QThread::msleep(20);
	}
	return false;
}

//! The `-o` path of a render command line.
QString outputArgument(int argc, char** argv)
{
	for (int i = 3; i + 1 < argc; ++i)
	{
		if (std::strcmp(argv[i], "-o") == 0) { return QString::fromLocal8Bit(argv[i + 1]); }
	}
	return QString();
}

bool writePid(const QString& name)
{
	const QString pidDir = qEnvironmentVariable("ZENE_FAKE_PID_DIR");
	if (pidDir.isEmpty()) { return true; }
	QFile pid(QDir(pidDir).filePath(name));
	if (!pid.open(QIODevice::WriteOnly)) { return false; }
	pid.write(QByteArray::number(QCoreApplication::applicationPid()));
	return true;
}

int fakeRender(int argc, char** argv)
{
	const QStringList names = audibleNames(argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString());
	const QString out = outputArgument(argc, argv);
	if (out.isEmpty() || names.size() != 1) { return 4; }
	const QString name = names.front();
	if (!writePid(name)) { return 6; }
	if (!waitAtBarrier()) { return 7; }
	if (name == qEnvironmentVariable("ZENE_FAKE_SLOW_TRACK"))
	{
		QThread::msleep(static_cast<unsigned long>(qEnvironmentVariableIntValue("ZENE_FAKE_SLOW_MS")));
	}
	if (name == qEnvironmentVariable("ZENE_FAKE_FAIL_TRACK")) { return 3; }
	return writeTone(out, 44100) ? 0 : 5;
}

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

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

Track* trackWithClip(const QString& name)
{
	Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
	track->setName(name);
	track->createClip(TimePos(0));
	return track;
}

QJsonArray idsOf(std::initializer_list<const Track*> tracks)
{
	QJsonArray ids;
	for (const Track* track : tracks) { ids.append(control::trackIdOf(track)); }
	return ids;
}

int freezeTaskCount()
{
	int count = 0;
	for (const TaskInfo& task : TaskCenter::instance().tasks()) { count += task.kind == QLatin1String("freeze") ? 1 : 0; }
	return count;
}

const TaskInfo* taskWithId(const std::vector<TaskInfo>& tasks, int id)
{
	for (const TaskInfo& task : tasks)
	{
		if (task.id == id) { return &task; }
	}
	return nullptr;
}

//! Runs the event loop until batch @a id has ended and every child has reported back; records each progress seen.
const BatchFreezeJob* waitForBatch(int id, int taskId = -1, std::vector<double>* progress = nullptr)
{
	for (int waited = 0; waited < 20000; waited += 10)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		if (progress != nullptr && taskId > 0)
		{
			const std::vector<TaskInfo> tasks = TaskCenter::instance().tasks();
			if (const TaskInfo* task = taskWithId(tasks, taskId); task != nullptr
				&& (progress->empty() || progress->back() != task->progress)) { progress->push_back(task->progress); }
		}
		const BatchFreezeJob* job = BatchFreeze::instance().job(id);
		if (job == nullptr) { return nullptr; }
		if (job->ended() && job->inFlight == 0) { return job; }
		QThread::msleep(5);
	}
	return BatchFreeze::instance().job(id);
}

int undoDepth() { return Engine::projectJournal()->undoDepth(); }

QStringList partFiles(const QString& dir)
{
	return QDir(dir).entryList({QStringLiteral("*.part.*")}, QDir::Files);
}

} // namespace

class BatchFreezeTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;

	QString path(const QString& name) const { return QDir(m_dir.path()).filePath(name); }
	QString outDir() const { return path(QStringLiteral("takes")); }

	ControlResult freeze(const QJsonArray& tracks)
	{
		return run(QStringLiteral("freeze.tracks"), {{QStringLiteral("tracks"), tracks}, {QStringLiteral("out_dir"), outDir()}});
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
		for (const char* name : {"ZENE_FAKE_BARRIER_DIR", "ZENE_FAKE_BARRIER_COUNT", "ZENE_FAKE_FAIL_TRACK",
			"ZENE_FAKE_SLOW_TRACK", "ZENE_FAKE_SLOW_MS", "ZENE_FAKE_PID_DIR"}) { qunsetenv(name); }
		Engine::getSong()->clearProject();
		Engine::projectJournal()->clearJournal();
		QDir(outDir()).removeRecursively();
		QVERIFY(QDir().mkpath(outDir()));
	}

	void theCommandIsRegisteredAsOneUndoableEdit()
	{
		const auto* row = control::ReversibilityTable::instance().lookup(QStringLiteral("freeze.tracks"));
		QVERIFY(row != nullptr);
		QCOMPARE(row->cls, control::ReversibilityClass::TrueInverse);
		QVERIFY(BatchFreeze::parallelLimit() >= 2);
	}

	void refusesABatchItCannotFreezeWholeAndStartsNothing()
	{
		Track* a = trackWithClip(QStringLiteral("A"));
		Track* b = trackWithClip(QStringLiteral("B"));
		Track* automation = Track::create(Track::Type::Automation, Engine::getSong());
		const int tasks = freezeTaskCount();
		const int depth = undoDepth();
		QCOMPARE(freeze(idsOf({a})).errorKind, ControlErrorKind::Refused);
		QCOMPARE(freeze(QJsonArray{}).errorKind, ControlErrorKind::Refused);
		QCOMPARE(freeze(idsOf({a, a})).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(freeze(idsOf({a, automation})).errorKind, ControlErrorKind::Refused);
		QCOMPARE(freeze(QJsonArray{control::trackIdOf(a), QStringLiteral("trk-999999")}).ok, false);
		// b already frozen (a take installed directly, as a project load would): the batch must refuse it whole.
		const QString take = path(QStringLiteral("b-single.wav"));
		QVERIFY(writeTone(take, 44100));
		QString freezeError;
		QVERIFY2(b->freezeTo(take, 0, 192, {}, &freezeError, false), qPrintable(freezeError));
		const int afterSingle = undoDepth();
		const ControlResult frozenTarget = freeze(idsOf({a, b}));
		QCOMPARE(frozenTarget.errorKind, ControlErrorKind::Refused);
		QVERIFY(frozenTarget.errorMessage.contains(control::trackIdOf(b)));
		QVERIFY(!a->isFrozen());
		QCOMPARE(freezeTaskCount(), tasks);
		QCOMPARE(undoDepth(), afterSingle);
		QCOMPARE(afterSingle, depth);
		QVERIFY(partFiles(outDir()).isEmpty());
	}

	void rendersConcurrentlyAsOneTaskAndUndoesAsOneStep()
	{
		Track* a = trackWithClip(QStringLiteral("A"));
		Track* b = trackWithClip(QStringLiteral("B"));
		Track* c = trackWithClip(QStringLiteral("C"));
		Track* untouched = trackWithClip(QStringLiteral("Other"));
		const QString barrier = path(QStringLiteral("barrier-concurrent"));
		QVERIFY(QDir().mkpath(barrier));
		qputenv("ZENE_FAKE_BARRIER_DIR", barrier.toLocal8Bit());
		// Every child must see the others running before it renders: a serial batch would time out (exit 7).
		const int together = std::max(2, std::min(3, BatchFreeze::parallelLimit()));
		qputenv("ZENE_FAKE_BARRIER_COUNT", QByteArray::number(together));
		const int tasksBefore = freezeTaskCount();
		const int depth = undoDepth();

		const ControlResult accepted = freeze(idsOf({a, b, c}));
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		const int batch = accepted.result.value(QStringLiteral("batch")).toInt();
		const int taskId = accepted.result.value(QStringLiteral("task")).toInt();
		QVERIFY(taskId > 0);
		QCOMPARE(freezeTaskCount(), tasksBefore + 1);
		const QJsonArray listed = run(QStringLiteral("task.list")).result.value(QStringLiteral("tasks")).toArray();
		int matching = 0;
		for (const QJsonValue& task : listed)
		{
			if (task.toObject().value(QStringLiteral("id")).toInt() != taskId) { continue; }
			++matching;
			QCOMPARE(task.toObject().value(QStringLiteral("cancellable")).toBool(), true);
		}
		QCOMPARE(matching, 1);

		std::vector<double> progress;
		const BatchFreezeJob* job = waitForBatch(batch, taskId, &progress);
		QVERIFY(job != nullptr);
		QVERIFY2(job->state == BatchFreezeJob::State::Done, qPrintable(job->error));
		QVERIFY(job->peakParallel >= together);
		QCOMPARE(QDir(barrier).entryList(QDir::Files).size(), 3);
		QCOMPARE(freezeTaskCount(), tasksBefore + 1);  // still the one task, now done
		const TaskInfo* task = nullptr;
		const std::vector<TaskInfo> tasks = TaskCenter::instance().tasks();
		task = taskWithId(tasks, taskId);
		QVERIFY(task != nullptr);
		QCOMPARE(task->state, TaskState::Done);
		QVERIFY(!progress.empty());
		QVERIFY(progress.front() >= 0.0 && progress.front() < 1.0);  // aggregate progress, not "unknown"
		QCOMPARE(progress.back(), 1.0);

		for (Track* track : {a, b, c})
		{
			QVERIFY(track->isFrozen());
			QVERIFY(track->frozenAudioReady());
			const ControlResult state = run(QStringLiteral("track.get_state"), {{QStringLiteral("track"), control::trackIdOf(track)}});
			QVERIFY2(state.ok, qPrintable(state.errorMessage));
		}
		QVERIFY(!untouched->isFrozen());
		QCOMPARE(undoDepth(), depth + 1);
		QVERIFY(partFiles(outDir()).isEmpty());

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!a->isFrozen() && !b->isFrozen() && !c->isFrozen());
		QCOMPARE(undoDepth(), depth);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QVERIFY(a->isFrozen() && b->isFrozen() && c->isFrozen());
	}

	void cancelAfterOneTrackRenderedRestoresEveryTrack()
	{
		Track* a = trackWithClip(QStringLiteral("A"));
		Track* b = trackWithClip(QStringLiteral("B"));
		const QString aOut = QDir(outDir()).filePath(control::trackIdOf(a) + QStringLiteral("-frozen.wav"));
		QFile previous(aOut);
		QVERIFY(previous.open(QIODevice::WriteOnly));
		previous.write("previous");
		previous.close();
		const QString pids = path(QStringLiteral("pids-cancel"));
		QVERIFY(QDir().mkpath(pids));
		qputenv("ZENE_FAKE_PID_DIR", pids.toLocal8Bit());
		qputenv("ZENE_FAKE_SLOW_TRACK", "B");
		qputenv("ZENE_FAKE_SLOW_MS", "8000");
		const int depth = undoDepth();

		const ControlResult accepted = freeze(idsOf({a, b}));
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		const int batch = accepted.result.value(QStringLiteral("batch")).toInt();
		const BatchFreezeJob* job = BatchFreeze::instance().job(batch);
		const QString scratchA = job->targets[0].prepared.scratch->path();
		QTRY_VERIFY_WITH_TIMEOUT(BatchFreeze::instance().job(batch)->targets[0].phase == BatchFreezeTarget::Phase::Rendered, 8000);
		QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(QDir(pids).filePath(QStringLiteral("B"))), 3000);
		QFile pidFile(QDir(pids).filePath(QStringLiteral("B")));
		QVERIFY(pidFile.open(QIODevice::ReadOnly));
		const qint64 slowPid = pidFile.readAll().toLongLong();
		QVERIFY(slowPid > 0);

		// The agent path: the one task, cancelled through task.cancel.
		const ControlResult cancelled = run(QStringLiteral("task.cancel"), {{QStringLiteral("id"), accepted.result.value(QStringLiteral("task")).toInt()}});
		QVERIFY2(cancelled.ok, qPrintable(cancelled.errorMessage));
		job = waitForBatch(batch);
		QVERIFY(job != nullptr);
		QCOMPARE(job->state, BatchFreezeJob::State::Cancelled);
		QTRY_VERIFY_WITH_TIMEOUT(!processIsRunning(slowPid), 3000);
		QVERIFY(!a->isFrozen() && !b->isFrozen());
		QCOMPARE(undoDepth(), depth);
		QVERIFY(!QFileInfo::exists(scratchA));
		QVERIFY(partFiles(outDir()).isEmpty());
		QFile kept(aOut);
		QVERIFY(kept.open(QIODevice::ReadOnly));
		QCOMPARE(kept.readAll(), QByteArray("previous"));
		const std::vector<TaskInfo> tasks = TaskCenter::instance().tasks();
		const TaskInfo* task = taskWithId(tasks, accepted.result.value(QStringLiteral("task")).toInt());
		QVERIFY(task != nullptr);
		QCOMPARE(task->state, TaskState::Cancelled);
	}

	void aFailedRenderAfterAnotherSucceededRestoresEveryTrack()
	{
		Track* a = trackWithClip(QStringLiteral("A"));
		Track* b = trackWithClip(QStringLiteral("B"));
		Track* c = trackWithClip(QStringLiteral("C"));
		qputenv("ZENE_FAKE_SLOW_TRACK", "B");
		qputenv("ZENE_FAKE_SLOW_MS", "1500");
		qputenv("ZENE_FAKE_FAIL_TRACK", "B");
		const int depth = undoDepth();
		const ControlResult accepted = freeze(idsOf({a, b, c}));
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		const BatchFreezeJob* job = waitForBatch(accepted.result.value(QStringLiteral("batch")).toInt());
		QVERIFY(job != nullptr);
		QCOMPARE(job->state, BatchFreezeJob::State::Failed);
		QVERIFY2(job->error.contains(control::trackIdOf(b)), qPrintable(job->error));
		QCOMPARE(job->targets[0].phase, BatchFreezeTarget::Phase::Rendered);  // A had finished before B failed
		QVERIFY(!a->isFrozen() && !b->isFrozen() && !c->isFrozen());
		QCOMPARE(undoDepth(), depth);
		QVERIFY(partFiles(outDir()).isEmpty());
		QCOMPARE(QDir(outDir()).entryList(QDir::Files).size(), 0);
	}

	void aTargetChangedDuringTheRenderAppliesNothing()
	{
		Track* a = trackWithClip(QStringLiteral("A"));
		Track* b = trackWithClip(QStringLiteral("B"));
		qputenv("ZENE_FAKE_SLOW_TRACK", "B");
		qputenv("ZENE_FAKE_SLOW_MS", "1500");
		const int depth = undoDepth();
		const ControlResult accepted = freeze(idsOf({a, b}));
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		b->setName(QStringLiteral("B renamed"));
		const BatchFreezeJob* job = waitForBatch(accepted.result.value(QStringLiteral("batch")).toInt());
		QVERIFY(job != nullptr);
		QCOMPARE(job->state, BatchFreezeJob::State::Failed);
		QVERIFY2(job->error.contains(QStringLiteral("changed")), qPrintable(job->error));
		QVERIFY(!a->isFrozen() && !b->isFrozen());
		QCOMPARE(undoDepth(), depth);
		QCOMPARE(QDir(outDir()).entryList(QDir::Files).size(), 0);
	}

	void theTrackMenuOpensTheSelectionAndFreezesTheTickedTracks()
	{
		Track* a = trackWithClip(QStringLiteral("A"));
		Track* b = trackWithClip(QStringLiteral("B"));
		Track* c = trackWithClip(QStringLiteral("C"));
		QMenu menu;
		addTrackRenderActions(&menu, a);
		QAction* several = nullptr;
		for (QAction* action : menu.actions())
		{
			if (action->data().toString() == QLatin1String("freeze.tracks")) { several = action; }
		}
		QVERIFY(several != nullptr);
		several->trigger();
		QDialog* dialog = nullptr;
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			if (widget->objectName() == QLatin1String("batchFreezeDialog") && widget->isVisible()) { dialog = qobject_cast<QDialog*>(widget); }
		}
		QVERIFY(dialog != nullptr);
		auto* list = dialog->findChild<QListWidget*>(QStringLiteral("batchFreezeTracks"));
		QVERIFY(list != nullptr);
		QCOMPARE(list->count(), 3);
		QCOMPARE(list->item(0)->checkState(), Qt::Checked);  // the track whose menu was opened
		QPushButton* freezeButton = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
		QVERIFY(!freezeButton->isEnabled());  // one tick is not a batch
		list->item(2)->setCheckState(Qt::Checked);
		QVERIFY(freezeButton->isEnabled());
		const int tasksBefore = freezeTaskCount();
		const int depth = undoDepth();
		QTest::mouseClick(freezeButton, Qt::LeftButton);
		QCOMPARE(freezeTaskCount(), tasksBefore + 1);
		const std::vector<TaskInfo> tasks = TaskCenter::instance().tasks();
		const int batch = static_cast<int>(std::count_if(tasks.begin(), tasks.end(),
			[](const TaskInfo& t) { return t.kind == QLatin1String("freeze") && t.active(); }));
		QCOMPARE(batch, 1);
		QTRY_VERIFY_WITH_TIMEOUT(a->isFrozen() && c->isFrozen(), 15000);
		QVERIFY(!b->isFrozen());
		QCOMPARE(undoDepth(), depth + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!a->isFrozen() && !c->isFrozen());
	}
};

int main(int argc, char** argv)
{
	if (argc > 1 && std::strcmp(argv[1], "render") == 0)
	{
		QCoreApplication child(argc, argv);
		return fakeRender(argc, argv);
	}
	QApplication app(argc, argv);
	QTEST_SET_MAIN_SOURCE_PATH
	BatchFreezeTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "BatchFreezeTest.moc"
