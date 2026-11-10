/*
 * BatchFreeze.cpp - Q050-327: freeze several tracks as ONE background task that renders them concurrently
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

#include "BatchFreeze.h"

#include <algorithm>
#include <limits>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QMetaObject>
#include <QProcess>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

namespace
{

constexpr int RenderLimitMs = 600000;  // the render queue's own per-render limit (RenderQueueBounce.cpp)

TaskState taskState(BatchFreezeJob::State state)
{
	if (state == BatchFreezeJob::State::Done) { return TaskState::Done; }
	return state == BatchFreezeJob::State::Cancelled ? TaskState::Cancelled : TaskState::Failed;
}

bool sameTempoMap(const TempoMap& live, const TempoMap& captured)
{
	if (live.active() != captured.active() || live.size() != captured.size()) { return false; }
	for (int i = 0; i < live.size(); ++i)
	{
		if (live[i] != captured[i]) { return false; }
	}
	return true;
}

int countPhase(const BatchFreezeJob& job, BatchFreezeTarget::Phase phase)
{
	return static_cast<int>(std::count_if(job.targets.begin(), job.targets.end(),
		[phase](const BatchFreezeTarget& t) { return t.phase == phase; }));
}

//! Whether the session is still the one every target was rendered from; empty when it is.
QString staleReason(const BatchFreezeJob& job)
{
	Song* song = Engine::getSong();
	if (song == nullptr || song->documentIdentity() != job.documentIdentity)
	{
		return QStringLiteral("the project was replaced or reloaded while the tracks were rendering; no track was frozen");
	}
	if (job.tempo != static_cast<int>(song->getTempo()) || !sameTempoMap(song->tempoMap().map(), job.tempoMap))
	{
		return QStringLiteral("the project tempo changed while the tracks were rendering; no track was frozen");
	}
	for (const BatchFreezeTarget& target : job.targets)
	{
		Track* track = control::trackWithId(target.trackId);
		if (track == nullptr || track->isFrozen() || BatchFreeze::trackFingerprint(track) != target.fingerprint)
		{
			return QStringLiteral("trk-%1 changed while the tracks were rendering; no track was frozen").arg(target.trackId);
		}
	}
	return QString();
}

void rollbackOutputs(BatchFreezeJob& job)
{
	for (BatchFreezeTarget& target : job.targets) { BounceInPlace::rollbackPreparedOutput(&target.prepared); }
}

//! Every staged take moved to its output path (a previous file kept aside); on a failure the installed ones go back.
bool installOutputs(BatchFreezeJob& job, QString* error)
{
	for (BatchFreezeTarget& target : job.targets)
	{
		if (!BounceInPlace::installPreparedOutput(&target.prepared, error))
		{
			rollbackOutputs(job);
			return false;
		}
	}
	return true;
}

//! A Track checkpoint and the take on every target, inside the caller's open journal edit; false at the first refusal.
bool freezeTargets(const BatchFreezeJob& job, QJsonArray* frozen, QString* error)
{
	for (const BatchFreezeTarget& target : job.targets)
	{
		Track* track = control::trackWithId(target.trackId);
		track->addJournalCheckPoint();
		if (!track->freezeTo(target.rendered.path, 0, target.rendered.range.end, {}, error, false)) { return false; }
		frozen->append(QJsonObject{{QStringLiteral("track"), control::trackIdOf(track)},
			{QStringLiteral("audio"), target.rendered.path}, {QStringLiteral("sha256"), target.rendered.sha256},
			{QStringLiteral("end_ticks"), static_cast<double>(target.rendered.range.end)}});
	}
	return true;
}

//! Opens one journal edit; the serial its merge starts from (0 without a journal).
quint64 openEdit(ProjectJournal* journal)
{
	if (journal == nullptr) { return 0; }
	const quint64 serial = journal->topStepSerial();
	journal->beginEdit();
	return serial;
}

//! Closes the edit and merges every checkpoint it took into ONE step; whether a step was recorded.
bool closeEdit(ProjectJournal* journal, quint64 serialBefore)
{
	if (journal == nullptr) { return false; }
	journal->endEdit();
	journal->mergeCheckpointsSince(serialBefore);
	return journal->topStepSerial() != serialBefore;
}

//! The A16 transaction for the completed batch's single journal step.
void recordEdit(const BatchFreezeJob& job, quint64 step)
{
	QJsonArray ids;
	for (const BatchFreezeTarget& target : job.targets) { ids.append(QStringLiteral("trk-%1").arg(target.trackId)); }
	ControlRegistry::instance()->recordCompletedEdit(QStringLiteral("freeze.tracks"), job.args,
		QJsonObject{{QStringLiteral("batch"), job.id}, {QStringLiteral("tracks"), ids}, {QStringLiteral("frozen"), false}},
		job.report, step);
}

} // namespace

BatchFreeze& BatchFreeze::instance()
{
	static BatchFreeze* batches = new BatchFreeze();
	return *batches;
}

QString BatchFreeze::stateName(BatchFreezeJob::State state)
{
	switch (state)
	{
	case BatchFreezeJob::State::Running: return QStringLiteral("running");
	case BatchFreezeJob::State::Done: return QStringLiteral("done");
	case BatchFreezeJob::State::Failed: return QStringLiteral("failed");
	case BatchFreezeJob::State::Cancelled: return QStringLiteral("cancelled");
	}
	return QString();
}

QString BatchFreeze::phaseName(BatchFreezeTarget::Phase phase)
{
	switch (phase)
	{
	case BatchFreezeTarget::Phase::Waiting: return QStringLiteral("waiting");
	case BatchFreezeTarget::Phase::Rendering: return QStringLiteral("rendering");
	case BatchFreezeTarget::Phase::Finalizing: return QStringLiteral("finalizing");
	case BatchFreezeTarget::Phase::Rendered: return QStringLiteral("rendered");
	case BatchFreezeTarget::Phase::Failed: return QStringLiteral("failed");
	}
	return QString();
}

QByteArray BatchFreeze::trackFingerprint(const Track* track)
{
	QString xml;
	if (track == nullptr || !control::captureTrackXml(track, &xml, std::numeric_limits<int>::max())) { return {}; }
	return QCryptographicHash::hash(xml.toUtf8(), QCryptographicHash::Sha256).toHex();
}

int BatchFreeze::parallelLimit()
{
	return std::clamp(QThread::idealThreadCount() / 2, 2, 8);
}

QJsonObject BatchFreeze::toJson(const BatchFreezeJob& job)
{
	QJsonArray targets;
	for (const BatchFreezeTarget& target : job.targets)
	{
		targets.append(QJsonObject{{QStringLiteral("track"), QStringLiteral("trk-%1").arg(target.trackId)},
			{QStringLiteral("phase"), phaseName(target.phase)}, {QStringLiteral("out"), target.prepared.outputPath}});
	}
	QJsonObject json{{QStringLiteral("batch"), job.id}, {QStringLiteral("state"), stateName(job.state)},
		{QStringLiteral("settled"), job.ended() && job.inFlight == 0}, {QStringLiteral("targets"), targets},
		{QStringLiteral("rendered"), countPhase(job, BatchFreezeTarget::Phase::Rendered)},
		{QStringLiteral("peak_parallel"), job.peakParallel}};
	if (!job.error.isEmpty()) { json.insert(QStringLiteral("error"), job.error); }
	return json;
}

const BatchFreezeJob* BatchFreeze::job(int id) const
{
	const auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [id](const BatchFreezeJob& j) { return j.id == id; });
	return it != m_jobs.end() ? &*it : nullptr;
}

BatchFreezeJob* BatchFreeze::find(int id)
{
	return const_cast<BatchFreezeJob*>(job(id));
}

void BatchFreeze::forgetEnded()
{
	while (static_cast<int>(m_jobs.size()) >= MaxBatches)
	{
		const auto ended = std::find_if(m_jobs.begin(), m_jobs.end(),
			[](const BatchFreezeJob& j) { return j.ended() && j.inFlight == 0; });
		if (ended == m_jobs.end()) { return; }
		m_jobs.erase(ended);
	}
}

int BatchFreeze::start(BatchFreezeJob job)
{
	forgetEnded();
	if (static_cast<int>(m_jobs.size()) >= MaxBatches)
	{
		for (BatchFreezeTarget& target : job.targets) { BounceInPlace::discardPreparedRender(&target.prepared); }
		return -1;
	}
	job.id = m_nextId++;
	job.state = BatchFreezeJob::State::Running;
	const int id = job.id;
	const int count = static_cast<int>(job.targets.size());
	m_jobs.push_back(std::move(job));
	BatchFreezeJob& added = m_jobs.back();
	added.taskToken = TaskCenter::instance().begin(this, id, {QStringLiteral("freeze"),
		tr("Freeze %n track(s)", nullptr, count), [id] { instance().cancel(id); }, {}, false});
	reportProgress(added);
	launchNext(added);
	return id;
}

void BatchFreeze::reportProgress(const BatchFreezeJob& job)
{
	const int total = static_cast<int>(job.targets.size());
	const int done = countPhase(job, BatchFreezeTarget::Phase::Rendered);
	const int running = countPhase(job, BatchFreezeTarget::Phase::Rendering);
	TaskCenter::instance().progress(job.taskToken, total > 0 ? static_cast<double>(done) / total : -1.0,
		tr("%1 of %2 tracks rendered, %3 rendering").arg(done).arg(total).arg(running));
}

void BatchFreeze::launchNext(BatchFreezeJob& job)
{
	int running = countPhase(job, BatchFreezeTarget::Phase::Rendering);
	for (std::size_t i = 0; i < job.targets.size() && running < parallelLimit() && !job.ended(); ++i)
	{
		if (job.targets[i].phase != BatchFreezeTarget::Phase::Waiting) { continue; }
		launch(job, i);
		++running;
	}
	job.peakParallel = std::max(job.peakParallel, running);
}

void BatchFreeze::launch(BatchFreezeJob& job, std::size_t index)
{
	const int id = job.id;
	BatchFreezeTarget& target = job.targets[index];
	target.phase = BatchFreezeTarget::Phase::Rendering;
	auto* process = new QProcess(this);
	target.process = process;
	process->setStandardOutputFile(QProcess::nullDevice());
	process->setStandardErrorFile(QProcess::nullDevice());
	connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
		[this, id, index](int exitCode, QProcess::ExitStatus status) {
			processEnded(id, index, status == QProcess::NormalExit && exitCode == 0, status != QProcess::NormalExit
				? QStringLiteral("the render process crashed or was stopped")
				: QStringLiteral("the render failed (exit %1)").arg(exitCode));
		});
	connect(process, &QProcess::errorOccurred, this, [this, id, index](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) { processEnded(id, index, false, QStringLiteral("the render process could not be started")); }
	});
	target.timer = new QTimer(this);
	target.timer->setSingleShot(true);
	connect(target.timer, &QTimer::timeout, process, &QProcess::kill);
	target.timer->start(RenderLimitMs);
	++job.inFlight;
	process->start(QCoreApplication::applicationFilePath(), BounceInPlace::renderArguments(target.prepared));
}

void BatchFreeze::processEnded(int id, std::size_t index, bool ok, const QString& error)
{
	BatchFreezeJob* job = find(id);
	if (job == nullptr || index >= job->targets.size() || job->targets[index].process == nullptr) { return; }
	BatchFreezeTarget& target = job->targets[index];
	target.process->deleteLater();
	target.process = nullptr;
	target.timer->stop();
	target.timer->deleteLater();
	target.timer = nullptr;
	if (job->ended() || !ok)
	{
		--job->inFlight;
		target.phase = BatchFreezeTarget::Phase::Failed;
		if (!job->ended())
		{
			fail(*job, BatchFreezeJob::State::Failed, QStringLiteral("trk-%1: %2").arg(target.trackId).arg(error));
		}
		settle(*job);
		return;
	}
	target.phase = BatchFreezeTarget::Phase::Finalizing;
	const BounceInPlace::PreparedRender prepared = target.prepared;
	QThreadPool::globalInstance()->start([this, id, index, prepared] {
		QString errorText;
		BounceInPlace::Result rendered = BounceInPlace::finishPreparedRender(prepared, &errorText);
		QMetaObject::invokeMethod(this, [this, id, index, rendered = std::move(rendered), errorText] {
			finalized(id, index, rendered, errorText);
		}, Qt::QueuedConnection);
	});
	launchNext(*job);
}

void BatchFreeze::finalized(int id, std::size_t index, const BounceInPlace::Result& rendered, const QString& error)
{
	BatchFreezeJob* job = find(id);
	if (job == nullptr || index >= job->targets.size()) { return; }
	--job->inFlight;
	BatchFreezeTarget& target = job->targets[index];
	if (job->ended() || !error.isEmpty() || !rendered.ok)
	{
		target.phase = BatchFreezeTarget::Phase::Failed;
		if (!job->ended())
		{
			fail(*job, BatchFreezeJob::State::Failed,
				QStringLiteral("trk-%1: %2").arg(target.trackId).arg(!error.isEmpty() ? error : rendered.error));
		}
		settle(*job);
		return;
	}
	target.rendered = rendered;
	target.phase = BatchFreezeTarget::Phase::Rendered;
	reportProgress(*job);
	if (countPhase(*job, BatchFreezeTarget::Phase::Rendered) != static_cast<int>(job->targets.size())) { return; }
	QString problem;
	if (!applyAll(*job, &problem))
	{
		fail(*job, BatchFreezeJob::State::Failed, problem);
		settle(*job);
		return;
	}
	job->state = BatchFreezeJob::State::Done;
	settle(*job);
}

bool BatchFreeze::applyAll(BatchFreezeJob& job, QString* error)
{
	TaskCenter::instance().progress(job.taskToken, 1.0, tr("Applying %n frozen take(s)", nullptr,
		static_cast<int>(job.targets.size())));
	*error = staleReason(job);
	if (!error->isEmpty() || !installOutputs(job, error)) { return false; }
	ProjectJournal* journal = Engine::projectJournal();
	const quint64 serialBefore = openEdit(journal);
	QJsonArray frozen;
	const bool frozeAll = freezeTargets(job, &frozen, error);
	const bool recorded = closeEdit(journal, serialBefore);
	if (!frozeAll)
	{
		// The one merged step holds every checkpoint taken so far: undoing it thaws them all.
		if (recorded && journal->canUndo()) { journal->undo(); }
		rollbackOutputs(job);
		return false;
	}
	for (BatchFreezeTarget& target : job.targets) { BounceInPlace::commitPreparedOutput(&target.prepared); }
	job.report = QJsonObject{{QStringLiteral("frozen"), frozen}, {QStringLiteral("undo"), QStringLiteral("control.undo")}};
	if (recorded) { recordEdit(job, journal->topStepSerial()); }
	return true;
}

void BatchFreeze::fail(BatchFreezeJob& job, BatchFreezeJob::State state, const QString& error)
{
	job.state = state;
	job.error = error;
	stopProcesses(job);
}

void BatchFreeze::stopProcesses(BatchFreezeJob& job)
{
	for (BatchFreezeTarget& target : job.targets)
	{
		if (target.phase == BatchFreezeTarget::Phase::Waiting) { target.phase = BatchFreezeTarget::Phase::Failed; }
		// The process's finished signal comes back through processEnded, which counts it out of inFlight.
		if (target.process != nullptr) { target.process->kill(); }
	}
}

void BatchFreeze::settle(BatchFreezeJob& job)
{
	if (!job.ended() || job.inFlight > 0) { return; }
	// Every child and worker has reported back: nothing can write a staged file any more.
	for (BatchFreezeTarget& target : job.targets) { BounceInPlace::discardPreparedRender(&target.prepared); }
	TaskCenter::instance().finish(job.taskToken, taskState(job.state), job.error);
}

bool BatchFreeze::cancel(int id)
{
	BatchFreezeJob* job = find(id);
	if (job == nullptr || job->ended()) { return false; }
	fail(*job, BatchFreezeJob::State::Cancelled, QString());
	settle(*job);
	return true;
}

} // namespace lmms
