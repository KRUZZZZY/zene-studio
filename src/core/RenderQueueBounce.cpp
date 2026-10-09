/*
 * RenderQueueBounce.cpp - asynchronous in-place render jobs
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

#include "RenderQueue.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QMetaObject>
#include <QProcess>
#include <QThreadPool>
#include <QTimer>

#include "BounceInPlace.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "TaskCenter.h"

namespace lmms
{

namespace
{

qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }

TaskState taskState(RenderJob::State state)
{
	if (state == RenderJob::State::Done) { return TaskState::Done; }
	return state == RenderJob::State::Cancelled ? TaskState::Cancelled : TaskState::Failed;
}

bool bounceFinishing(const RenderJob& job)
{
	return job.phase == QLatin1String("finalizing") || job.phase == QLatin1String("applying")
		|| job.phase == QLatin1String("done") || job.phase == QLatin1String("failed")
		|| job.phase == QLatin1String("cancelled");
}

QJsonObject bounceReport(const BounceInPlace::Result& rendered, const QJsonObject& applied)
{
	QJsonObject report{{QStringLiteral("path"), rendered.path},
		{QStringLiteral("sample_rate"), rendered.sampleRate}, {QStringLiteral("frames"), static_cast<double>(rendered.frames)},
		{QStringLiteral("bytes"), static_cast<double>(rendered.bytes)}, {QStringLiteral("sha256"), rendered.sha256},
		{QStringLiteral("start_ticks"), static_cast<double>(rendered.range.start)},
		{QStringLiteral("end_ticks"), static_cast<double>(rendered.range.end)}};
	for (auto it = applied.constBegin(); it != applied.constEnd(); ++it) { report.insert(it.key(), it.value()); }
	return report;
}

bool validateBounceTarget(RenderJob& job, QString* error)
{
	if (job.validateBounce && job.validateBounce(error)) { return true; }
	if (error->isEmpty()) { *error = QStringLiteral("the target changed while it was rendering"); }
	return false;
}

bool applyBounceModel(RenderJob& job, const BounceInPlace::Result& rendered, QJsonObject* applied, QString* error)
{
	if (!job.applyBounce) { *error = QStringLiteral("the render has no completion action"); return false; }
	*applied = job.applyBounce(rendered, error);
	return error->isEmpty();
}

void closeBounceEdit(ProjectJournal* journal, int depthBefore)
{
	if (journal == nullptr) { return; }
	journal->endEdit();
	journal->mergeCheckpointsFrom(depthBefore);
}

void rollbackBounceEdit(ProjectJournal* journal, quint64 serialBefore, BounceInPlace::PreparedRender* prepared)
{
	if (journal != nullptr && journal->topStepSerial() != serialBefore && journal->canUndo()) { journal->undo(); }
	BounceInPlace::rollbackPreparedOutput(prepared);
}

void recordBounceEdit(const RenderJob& job, const BounceInPlace::Result& rendered, const QJsonObject& applied,
	ProjectJournal* journal, quint64 serialBefore)
{
	if (journal == nullptr || journal->topStepSerial() == serialBefore) { return; }
	QJsonArray targetIds;
	for (const int id : job.targetIds) { targetIds.append(id); }
	const QJsonObject before{{QStringLiteral("job"), job.id}, {QStringLiteral("operation"), job.operation},
		{QStringLiteral("target_ids"), targetIds}, {QStringLiteral("out"), job.out}};
	ControlRegistry::instance()->recordCompletedEdit(QStringLiteral("render.queue_bounce"), job.commandArgs,
		before, bounceReport(rendered, applied), journal->topStepSerial());
}

} // namespace

void RenderQueue::runBounce(RenderJob& job)
{
	const int id = job.id;
	m_process = new QProcess(this);
	m_process->setStandardOutputFile(QProcess::nullDevice());
	m_process->setStandardErrorFile(QProcess::nullDevice());
	connect(m_process, &QProcess::started, this, [this, id] { bounceStarted(id); });
	connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
		[this, id](int exitCode, QProcess::ExitStatus status) {
			bounceProcessEnded(id, status == QProcess::NormalExit && exitCode == 0, exitCode,
				status == QProcess::NormalExit ? QString() : QStringLiteral("the render process crashed"));
		});
	connect(m_process, &QProcess::errorOccurred, this,
		[this, id](QProcess::ProcessError error) { bounceProcessError(id, static_cast<int>(error)); });
	m_bounceStopReason.clear();
	m_bounceTimer = new QTimer(this);
	m_bounceTimer->setSingleShot(true);
	connect(m_bounceTimer, &QTimer::timeout, this, [this, id] { bounceTimedOut(id); });
	m_bounceTimer->start(30000);
	m_process->start(QCoreApplication::applicationFilePath(), BounceInPlace::renderArguments(job.bounce));
}

void RenderQueue::bounceStarted(int id)
{
	RenderJob* job = find(id);
	if (job == nullptr || job->state != RenderJob::State::Running || job->phase != QLatin1String("starting")) { return; }
	job->phase = QStringLiteral("rendering");
	TaskCenter::instance().progress(this, id, -1.0, tr("Rendering track audio"));
	if (m_bounceTimer != nullptr) { m_bounceTimer->start(600000); }
}

void RenderQueue::bounceTimedOut(int id)
{
	RenderJob* job = find(id);
	if (job == nullptr || job->state != RenderJob::State::Running) { return; }
	if (job->phase == QLatin1String("starting")) { m_bounceStopReason = QStringLiteral("the render process did not start within 30 seconds"); }
	else if (job->phase == QLatin1String("rendering")) { m_bounceStopReason = QStringLiteral("the render exceeded the 10 minute limit"); }
	else { return; }
	job->phase = QStringLiteral("terminating");
	if (m_process != nullptr) { m_process->kill(); }
}

void RenderQueue::bounceProcessError(int id, int error)
{
	if (error == static_cast<int>(QProcess::FailedToStart))
	{
		bounceProcessEnded(id, false, -1, QStringLiteral("the render process could not be started"));
	}
}

void RenderQueue::clearBounceProcess()
{
	if (m_bounceTimer != nullptr)
	{
		m_bounceTimer->stop();
		m_bounceTimer->deleteLater();
		m_bounceTimer = nullptr;
	}
	if (m_process != nullptr)
	{
		m_process->deleteLater();
		m_process = nullptr;
	}
}

void RenderQueue::bounceProcessEnded(int id, bool ok, int exitCode, const QString& error)
{
	RenderJob* job = find(id);
	if (job == nullptr || id != m_current || bounceFinishing(*job)) { return; }
	clearBounceProcess();
	job->exitCode = exitCode;
	if (job->state == RenderJob::State::Cancelled)
	{
		finishBounce(id, job->bounce, BounceInPlace::Result::failure(QStringLiteral("cancelled")), QString());
		return;
	}
	const QString processError = !m_bounceStopReason.isEmpty() ? m_bounceStopReason : error;
	if (!ok || job->phase == QLatin1String("terminating"))
	{
		finishBounce(id, job->bounce, BounceInPlace::Result::failure(processError.isEmpty()
			? QStringLiteral("the render failed (exit %1)").arg(exitCode) : processError), processError);
		return;
	}
	beginBounceFinalization(id);
}


void RenderQueue::beginBounceFinalization(int id)
{
	RenderJob* job = find(id);
	if (job == nullptr || id != m_current) { return; }
	job->phase = QStringLiteral("finalizing");
	job->progress = -1.0;
	TaskCenter::instance().progress(this, id, -1.0, tr("Finalising rendered audio"));
	const BounceInPlace::PreparedRender prepared = job->bounce;
	QThreadPool::globalInstance()->start([this, id, prepared] {
		QString errorText;
		BounceInPlace::Result rendered = BounceInPlace::finishPreparedRender(prepared, &errorText);
		QMetaObject::invokeMethod(this, [this, id, prepared, rendered = std::move(rendered), errorText] {
			finishBounce(id, prepared, rendered, errorText);
		}, Qt::QueuedConnection);
	});
}

void RenderQueue::finishBounce(int id, BounceInPlace::PreparedRender prepared,
		BounceInPlace::Result rendered, const QString& error)
{
	RenderJob* job = find(id);
	if (job == nullptr || id != m_current)
	{
		BounceInPlace::discardPreparedRender(&prepared);
		return;
	}
	if (job->state == RenderJob::State::Cancelled)
	{
		BounceInPlace::discardPreparedRender(&job->bounce);
		finishBounceJob(*job, RenderJob::State::Cancelled, QString());
		releaseCurrent(id);
		return;
	}
	if (!error.isEmpty() || !rendered.ok)
	{
		failBounce(*job, id, !error.isEmpty() ? error : rendered.error);
		return;
	}
	QJsonObject applied;
	QString problem;
	if (!applyFinishedBounce(*job, &prepared, rendered, &applied, &problem))
	{
		failBounce(*job, id, problem);
		return;
	}
	job->report = bounceReport(rendered, applied);
	BounceInPlace::discardPreparedRender(&prepared);
	BounceInPlace::discardPreparedRender(&job->bounce);
	finishBounceJob(*job, RenderJob::State::Done, QString());
	releaseCurrent(id);
}

bool RenderQueue::applyFinishedBounce(RenderJob& job, BounceInPlace::PreparedRender* prepared,
		const BounceInPlace::Result& rendered, QJsonObject* applied, QString* error)
{
	if (!validateBounceTarget(job, error)) { return false; }
	job.phase = QStringLiteral("applying");
	TaskCenter::instance().progress(this, job.id, -1.0, tr("Applying rendered audio"));
	if (!BounceInPlace::installPreparedOutput(prepared, error)) { return false; }
	ProjectJournal* journal = Engine::projectJournal();
	const int depthBefore = journal != nullptr ? journal->undoDepth() : -1;
	const quint64 serialBefore = journal != nullptr ? journal->topStepSerial() : 0;
	if (journal != nullptr) { journal->beginEdit(); }
	const bool appliedOk = applyBounceModel(job, rendered, applied, error);
	closeBounceEdit(journal, depthBefore);
	if (!appliedOk)
	{
		rollbackBounceEdit(journal, serialBefore, prepared);
		return false;
	}
	BounceInPlace::commitPreparedOutput(prepared);
	recordBounceEdit(job, rendered, *applied, journal, serialBefore);
	return true;
}

void RenderQueue::failBounce(RenderJob& job, int id, const QString& error)
{
	BounceInPlace::discardPreparedRender(&job.bounce);
	finishBounceJob(job, RenderJob::State::Failed, error);
	releaseCurrent(id);
}

void RenderQueue::finishBounceJob(RenderJob& job, RenderJob::State state, const QString& error)
{
	job.state = state;
	job.error = error;
	job.finishedMs = now();
	job.progress = state == RenderJob::State::Done ? 1.0 : job.progress;
	job.phase = state == RenderJob::State::Done ? QStringLiteral("done")
		: state == RenderJob::State::Cancelled ? QStringLiteral("cancelled") : QStringLiteral("failed");
	job.validateBounce = {};
	job.applyBounce = {};
	TaskCenter::instance().finish(this, job.id, taskState(state), error);
}

void RenderQueue::releaseCurrent(int id)
{
	if (m_current == id) { m_current = -1; }
	QTimer::singleShot(0, this, &RenderQueue::next);
}


} // namespace lmms
