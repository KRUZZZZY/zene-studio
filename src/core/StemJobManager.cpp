/*
 * StemJobManager.cpp - background job manager for offline stem separation
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

#include "StemSeparation/StemJobManager.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <thread>

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QWaitCondition>

#include "SampleBuffer.h"
#include "TaskCenter.h"

namespace lmms
{

struct StemJobManager::SharedState
{
	mutable QMutex mutex;
	//! Serialises signal forwarding with the manager's destructor: once
	//! signalTarget is null, no worker signal reaches the manager.
	QMutex signalMutex;
	QWaitCondition jobAvailable;
	std::deque<std::shared_ptr<Job>> queue;
	QHash<int, std::shared_ptr<Job>> jobs;
	std::unique_ptr<StemSeparator> separator;
	ModelProvisioner provisioner;
	int nextJobId = 1;
	bool quit = false;
	StemJobManager* signalTarget = nullptr;
};

namespace
{

/*! A backend's (or a download's) fraction as a value a listener can use.
 *  std::clamp does NOT do this on its own: every comparison against a NaN is
 *  false, so clamp(NaN, 0, 1) hands the NaN straight back. A non-finite value
 *  is a backend that computed its fraction badly, not progress, so it is
 *  refused here - otherwise it reaches the GUI (a NaN turned into a percentage
 *  by the dialog's own arithmetic) and into the atomic a poller reads. */
float usableFraction(float fraction)
{
	if (!std::isfinite(fraction))
	{
		return 0.0f;
	}
	return std::clamp(fraction, 0.0f, 1.0f);
}

} // namespace

StemJobManager::StemJobManager(QObject* parent) :
	QObject(parent),
	m_state(std::make_shared<SharedState>())
{
	m_state->signalTarget = this;
	std::thread([state = m_state] { workerLoop(state); }).detach();
	reportToTaskArea();
}




void StemJobManager::reportToTaskArea()
{
	// 0.5.0 theme 24: the job signals the worker already emits, queued to the task area on the main thread. A live
	// manager resolves the job's stored token, so each report stays bound to the generation submitted for that job.
	const QPointer<StemJobManager> manager(this);
	TaskCenter* tasks = &TaskCenter::instance();
	const auto reportProgress = [manager](int id, double fraction) {
		if (manager != nullptr) { TaskCenter::instance().progress(manager->taskTokenFor(id), fraction); }
	};
	const auto reportFinish = [manager](int id, TaskState state, const QString& status) {
		if (manager != nullptr) { TaskCenter::instance().finish(manager->taskTokenFor(id), state, status); }
	};
	connect(this, &StemJobManager::jobStarted, tasks, [reportProgress](int id) { reportProgress(id, 0.0); });
	connect(this, &StemJobManager::jobProgress, tasks, [reportProgress](int id, float fraction) { reportProgress(id, fraction); });
	connect(this, &StemJobManager::jobFinished, tasks,
		[reportFinish](int id, double) { reportFinish(id, TaskState::Done, QString()); });
	connect(this, &StemJobManager::jobCancelled, tasks,
		[reportFinish](int id) { reportFinish(id, TaskState::Cancelled, QString()); });
	connect(this, &StemJobManager::jobFailed, tasks,
		[reportFinish](int id, const QString& error) { reportFinish(id, TaskState::Failed, error); });
}




StemJobManager::~StemJobManager()
{
	// Stop signals before QObject teardown. The detached worker owns state and
	// the backend independently, so cancelling here never joins inference.
	auto state = m_state;
	{
		QMutexLocker locker(&state->signalMutex);
		state->signalTarget = nullptr;
	}
	TaskCenter::instance().forget(this);
	{
		QMutexLocker locker(&state->mutex);
		state->quit = true;
		for (auto& job : state->jobs)
		{
			job->cancelRequested.store(true);
		}
	}
	state->jobAvailable.wakeAll();
}




void StemJobManager::setSeparator(std::unique_ptr<StemSeparator> separator)
{
	m_state->separator = std::move(separator);
}




QString StemJobManager::backendName() const
{
	return m_state->separator ? m_state->separator->backendName() : QString();
}




int StemJobManager::submit(std::shared_ptr<const SampleBuffer> mix,
	int sampleRate,
	int segmentFrames,
	StemQuality quality)
{
	if (!mix)
	{
		return -1;
	}

	auto job = std::make_shared<Job>();
	{
		QMutexLocker locker(&m_state->mutex);
		job->id = m_state->nextJobId++;
		job->mix = std::move(mix);
		job->sampleRate = sampleRate;
		job->segmentFrames = segmentFrames;
		job->quality = quality;
		m_state->jobs.insert(job->id, job);
		m_state->queue.push_back(job);
	}
	// Listed before the worker can start it, so the job's first signal finds its task.
	job->taskToken = TaskCenter::instance().begin(this, job->id, {QStringLiteral("stem_split"), tr("Split stems"),
		[manager = QPointer<StemJobManager>(this), id = job->id] { if (manager != nullptr) { manager->cancel(id); } }, {}, true});
	m_state->jobAvailable.wakeOne();
	return job->id;
}




void StemJobManager::cancel(int jobId)
{
	bool cancelledImmediately = false;
	{
		QMutexLocker locker(&m_state->mutex);
		auto it = m_state->jobs.find(jobId);
		if (it == m_state->jobs.end())
		{
			return;
		}
		const auto& job = *it;
		job->cancelRequested.store(true);
		if (job->state.load() == State::Queued)
		{
			// Still in the queue: drop it here instead of waiting for the
			// worker to reach it (the worker may be busy with another job).
			for (auto qit = m_state->queue.begin(); qit != m_state->queue.end(); ++qit)
			{
				if (*qit == job)
				{
					m_state->queue.erase(qit);
					break;
				}
			}
			job->mix.reset();
			job->state.store(State::Cancelled);
			cancelledImmediately = true;
		}
		else if (job->state.load() == State::Running)
		{
			job->state.store(State::CancelRequested);
		}
	}
	if (cancelledImmediately)
	{
		emit jobCancelled(jobId);
	}
}




void StemJobManager::cancelAll()
{
	QList<int> ids;
	{
		QMutexLocker locker(&m_state->mutex);
		ids = m_state->jobs.keys();
	}
	for (const int id : ids)
	{
		cancel(id);
	}
}




StemJobManager::State StemJobManager::state(int jobId) const
{
	QMutexLocker locker(&m_state->mutex);
	auto it = m_state->jobs.find(jobId);
	// Unknown ids read as Queued: "not started" is the least misleading answer
	// and keeps GUI polling of a just-submitted id race-free.
	return it == m_state->jobs.end() ? State::Queued : (*it)->state.load();
}




float StemJobManager::progress(int jobId) const
{
	QMutexLocker locker(&m_state->mutex);
	auto it = m_state->jobs.find(jobId);
	return it == m_state->jobs.end() ? 0.0f : (*it)->progress.load();
}




StemSet StemJobManager::result(int jobId) const
{
	QMutexLocker locker(&m_state->mutex);
	auto it = m_state->jobs.find(jobId);
	return it == m_state->jobs.end() ? StemSet{} : (*it)->stems;
}




QString StemJobManager::error(int jobId) const
{
	QMutexLocker locker(&m_state->mutex);
	auto it = m_state->jobs.find(jobId);
	return it == m_state->jobs.end() ? QString() : (*it)->error;
}




int StemJobManager::runningJobCount() const
{
	QMutexLocker locker(&m_state->mutex);
	int count = 0;
	for (auto it = m_state->jobs.cbegin(); it != m_state->jobs.cend(); ++it)
	{
		const auto s = (*it)->state.load();
		if (s == State::Running || s == State::CancelRequested)
		{
			++count;
		}
	}
	return count;
}




void StemJobManager::setModelProvisioner(ModelProvisioner provisioner)
{
	QMutexLocker locker(&m_state->mutex);
	m_state->provisioner = std::move(provisioner);
}




bool StemJobManager::fetchingModel(int jobId) const
{
	QMutexLocker locker(&m_state->mutex);
	auto it = m_state->jobs.find(jobId);
	return it != m_state->jobs.end() && (*it)->fetchingModel.load();
}




float StemJobManager::downloadProgress(int jobId) const
{
	QMutexLocker locker(&m_state->mutex);
	auto it = m_state->jobs.find(jobId);
	return it == m_state->jobs.end() ? 0.0f : (*it)->downloadProgress.load();
}




std::shared_ptr<StemJobManager::Job> StemJobManager::findJob(int jobId) const
{
	QMutexLocker locker(&m_state->mutex);
	auto it = m_state->jobs.find(jobId);
	return it == m_state->jobs.end() ? nullptr : *it;
}

TaskToken StemJobManager::taskTokenFor(int jobId) const
{
	const auto job = findJob(jobId);
	return job != nullptr ? job->taskToken : TaskToken();
}




void StemJobManager::emitJobStarted(const std::shared_ptr<SharedState>& state, int jobId)
{
	QMutexLocker locker(&state->signalMutex);
	if (auto* target = state->signalTarget)
	{
		QMetaObject::invokeMethod(target, [target, jobId] { emit target->jobStarted(jobId); }, Qt::QueuedConnection);
	}
}




void StemJobManager::emitJobProgress(const std::shared_ptr<SharedState>& state, int jobId, float fraction)
{
	QMutexLocker locker(&state->signalMutex);
	if (auto* target = state->signalTarget)
	{
		QMetaObject::invokeMethod(target, [target, jobId, fraction] { emit target->jobProgress(jobId, fraction); },
			Qt::QueuedConnection);
	}
}




void StemJobManager::emitJobFinished(const std::shared_ptr<SharedState>& state, int jobId, double elapsedSeconds)
{
	QMutexLocker locker(&state->signalMutex);
	if (auto* target = state->signalTarget)
	{
		QMetaObject::invokeMethod(target, [target, jobId, elapsedSeconds]
			{ emit target->jobFinished(jobId, elapsedSeconds); }, Qt::QueuedConnection);
	}
}




void StemJobManager::emitJobCancelled(const std::shared_ptr<SharedState>& state, int jobId)
{
	QMutexLocker locker(&state->signalMutex);
	if (auto* target = state->signalTarget)
	{
		QMetaObject::invokeMethod(target, [target, jobId] { emit target->jobCancelled(jobId); }, Qt::QueuedConnection);
	}
}




void StemJobManager::emitJobFailed(const std::shared_ptr<SharedState>& state, int jobId, const QString& error)
{
	QMutexLocker locker(&state->signalMutex);
	if (auto* target = state->signalTarget)
	{
		QMetaObject::invokeMethod(target, [target, jobId, error] { emit target->jobFailed(jobId, error); },
			Qt::QueuedConnection);
	}
}




void StemJobManager::workerLoop(const std::shared_ptr<SharedState>& state)
{
	for (;;)
	{
		std::shared_ptr<Job> job;
		{
			QMutexLocker locker(&state->mutex);
			while (state->queue.empty() && !state->quit)
			{
				state->jobAvailable.wait(&state->mutex);
			}
			if (state->queue.empty())
			{
				// quit and nothing left to do
				return;
			}
			job = state->queue.front();
			state->queue.pop_front();
			// Dequeue claims ownership of the queued-to-running transition while
			// holding the same mutex cancel() uses. A concurrent cancel therefore
			// sees Running and leaves terminal emission to this worker only.
			if (!job->cancelRequested.load())
			{
				job->state.store(State::Running);
			}
		}

		if (job->cancelRequested.load())
		{
			job->state.store(State::Cancelled);
			emitJobCancelled(state, job->id);
		}
		else
		{
			runJob(state, job);
		}
		// The input is read by this worker only and is spent once the job has ended, so a
		// finished job keeps only its result rather than its whole source mix as well.
		QMutexLocker locker(&state->mutex);
		job->mix.reset();
	}
}




bool StemJobManager::provisionModel(const std::shared_ptr<SharedState>& state, const std::shared_ptr<Job>& job)
{
	// Owner decision 14: the model a first job needs is fetched HERE, on the
	// worker, so neither the GUI nor the control surface waits for 166 MB.
	ModelProvisioner provisioner;
	{
		QMutexLocker locker(&state->mutex);
		provisioner = state->provisioner;
	}
	if (!provisioner) { return true; }

	job->fetchingModel.store(true);
	QString provisionError;
	const bool provisioned = provisioner(job->cancelRequested,
		[job](float fraction) { job->downloadProgress.store(usableFraction(fraction)); },
		&provisionError);
	job->fetchingModel.store(false);
	if (job->cancelRequested.load())
	{
		job->state.store(State::Cancelled);
		emitJobCancelled(state, job->id);
		return false;
	}
	if (!provisioned)
	{
		{
			QMutexLocker locker(&state->mutex);
			job->error = provisionError;
			job->state.store(State::Failed);
		}
		emitJobFailed(state, job->id, provisionError);
		return false;
	}
	return true;
}




void StemJobManager::runJob(const std::shared_ptr<SharedState>& state, const std::shared_ptr<Job>& job)
{
	emitJobStarted(state, job->id);

	if (state->separator == nullptr)
	{
		const auto error = QStringLiteral("No stem-separation backend configured");
		{
			QMutexLocker locker(&state->mutex);
			job->error = error;
			job->state.store(State::Failed);
		}
		emitJobFailed(state, job->id, error);
		return;
	}

	const auto startTime = std::chrono::steady_clock::now();

	if (!provisionModel(state, job)) { return; }

	StemSet stems;
	QString error;
	const auto status = state->separator->separate(*job->mix, job->sampleRate, job->segmentFrames, job->quality,
		[state, job](float fraction)
		{
			if (job->cancelRequested.load())
			{
				job->state.store(State::CancelRequested);
				return false;
			}
			fraction = usableFraction(fraction);
			job->progress.store(fraction);
			// Throttle: at most one signal per percent of progress. The
			// worker thread never blocks on the receiver.
			if (fraction - job->lastEmittedProgress >= 0.01f || fraction >= 1.0f)
			{
				job->lastEmittedProgress = fraction;
				emitJobProgress(state, job->id, fraction);
			}
			return true;
		},
		stems, error);

	job->elapsedSeconds = std::chrono::duration<double>(
		std::chrono::steady_clock::now() - startTime).count();

	bool alreadyEmittedCompletion = false;
	switch (status)
	{
	case StemSeparator::Status::Success:
		{
			QMutexLocker locker(&state->mutex);
			job->stems = stems;
			job->progress.store(1.0f);
			job->state.store(State::Completed);
			// A backend that reported its own 1.0 (the common case: its last
			// step is step/steps) has already published it through the progress
			// callback, so publishing it here again would tell every listener
			// the job completed twice.
			alreadyEmittedCompletion = job->lastEmittedProgress >= 1.0f;
		}
		if (!alreadyEmittedCompletion)
		{
			emitJobProgress(state, job->id, 1.0f);
		}
		emitJobFinished(state, job->id, job->elapsedSeconds);
		break;
	case StemSeparator::Status::Cancelled:
		// Partial stems are kept when the backend produced them.
		{
			QMutexLocker locker(&state->mutex);
			job->stems = stems;
			job->state.store(State::Cancelled);
		}
		emitJobCancelled(state, job->id);
		break;
	case StemSeparator::Status::Failed:
	default:
		{
			QMutexLocker locker(&state->mutex);
			job->error = error;
			job->state.store(State::Failed);
		}
		emitJobFailed(state, job->id, error);
		break;
	}
}

} // namespace lmms
