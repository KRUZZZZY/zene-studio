/*
 * StemJobManager.cpp - background job manager for offline stem separation
 *
 * Copyright (c) 2026 LMMS Developers
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

#include <QHash>
#include <QMutexLocker>
#include <QThread>

#include "SampleBuffer.h"

namespace lmms
{

StemJobManager::StemJobManager(QObject* parent) :
	QObject(parent)
{
	m_thread = QThread::create([this] { workerLoop(); });
	m_thread->setObjectName(QStringLiteral("StemJobManager"));
	m_thread->start();
}




StemJobManager::~StemJobManager()
{
	{
		QMutexLocker locker(&m_mutex);
		m_quit = true;
		for (auto& job : m_jobs)
		{
			job->cancelRequested.store(true);
		}
	}
	m_jobAvailable.wakeAll();
	m_thread->wait();
	delete m_thread;
	m_thread = nullptr;
}




void StemJobManager::setSeparator(std::unique_ptr<StemSeparator> separator)
{
	m_separator = std::move(separator);
}




QString StemJobManager::backendName() const
{
	return m_separator ? m_separator->backendName() : QString();
}




int StemJobManager::submit(std::shared_ptr<const SampleBuffer> mix,
	int sampleRate,
	int segmentFrames)
{
	if (!mix)
	{
		return -1;
	}

	auto job = std::make_shared<Job>();
	{
		QMutexLocker locker(&m_mutex);
		job->id = m_nextJobId++;
		job->mix = std::move(mix);
		job->sampleRate = sampleRate;
		job->segmentFrames = segmentFrames;
		m_jobs.insert(job->id, job);
		m_queue.push_back(job);
	}
	m_jobAvailable.wakeOne();
	return job->id;
}




void StemJobManager::cancel(int jobId)
{
	bool cancelledImmediately = false;
	{
		QMutexLocker locker(&m_mutex);
		auto it = m_jobs.find(jobId);
		if (it == m_jobs.end())
		{
			return;
		}
		const auto& job = *it;
		job->cancelRequested.store(true);
		if (job->state.load() == State::Queued)
		{
			// Still in the queue: drop it here instead of waiting for the
			// worker to reach it (the worker may be busy with another job).
			for (auto qit = m_queue.begin(); qit != m_queue.end(); ++qit)
			{
				if (*qit == job)
				{
					m_queue.erase(qit);
					break;
				}
			}
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
		QMutexLocker locker(&m_mutex);
		ids = m_jobs.keys();
	}
	for (const int id : ids)
	{
		cancel(id);
	}
}




StemJobManager::State StemJobManager::state(int jobId) const
{
	QMutexLocker locker(&m_mutex);
	auto it = m_jobs.find(jobId);
	return it == m_jobs.end() ? State::Failed : (*it)->state.load();
}




float StemJobManager::progress(int jobId) const
{
	QMutexLocker locker(&m_mutex);
	auto it = m_jobs.find(jobId);
	return it == m_jobs.end() ? 0.0f : (*it)->progress.load();
}




StemSet StemJobManager::result(int jobId) const
{
	QMutexLocker locker(&m_mutex);
	auto it = m_jobs.find(jobId);
	return it == m_jobs.end() ? StemSet{} : (*it)->stems;
}




QString StemJobManager::error(int jobId) const
{
	QMutexLocker locker(&m_mutex);
	auto it = m_jobs.find(jobId);
	return it == m_jobs.end() ? QString() : (*it)->error;
}




int StemJobManager::runningJobCount() const
{
	QMutexLocker locker(&m_mutex);
	int count = 0;
	for (auto it = m_jobs.cbegin(); it != m_jobs.cend(); ++it)
	{
		const auto s = (*it)->state.load();
		if (s == State::Running || s == State::CancelRequested)
		{
			++count;
		}
	}
	return count;
}




std::shared_ptr<StemJobManager::Job> StemJobManager::findJob(int jobId) const
{
	QMutexLocker locker(&m_mutex);
	auto it = m_jobs.find(jobId);
	return it == m_jobs.end() ? nullptr : *it;
}




void StemJobManager::workerLoop()
{
	for (;;)
	{
		std::shared_ptr<Job> job;
		{
			QMutexLocker locker(&m_mutex);
			while (m_queue.empty() && !m_quit)
			{
				m_jobAvailable.wait(&m_mutex);
			}
			if (m_queue.empty())
			{
				// m_quit and nothing left to do
				return;
			}
			job = m_queue.front();
			m_queue.pop_front();
		}

		if (job->cancelRequested.load())
		{
			job->state.store(State::Cancelled);
			emit jobCancelled(job->id);
			continue;
		}
		runJob(job);
	}
}




void StemJobManager::runJob(const std::shared_ptr<Job>& job)
{
	job->state.store(State::Running);
	emit jobStarted(job->id);

	if (m_separator == nullptr)
	{
		job->error = QStringLiteral("No stem-separation backend configured");
		job->state.store(State::Failed);
		emit jobFailed(job->id, job->error);
		return;
	}

	const auto startTime = std::chrono::steady_clock::now();

	StemSet stems;
	QString error;
	const auto status = m_separator->separate(*job->mix, job->sampleRate, job->segmentFrames,
		[this, job](float fraction)
		{
			if (job->cancelRequested.load())
			{
				job->state.store(State::CancelRequested);
				return false;
			}
			fraction = std::clamp(fraction, 0.0f, 1.0f);
			job->progress.store(fraction);
			// Throttle: at most one signal per percent of progress. The
			// worker thread never blocks on the receiver.
			if (fraction - job->lastEmittedProgress >= 0.01f || fraction >= 1.0f)
			{
				job->lastEmittedProgress = fraction;
				emit jobProgress(job->id, fraction);
			}
			return true;
		},
		stems, error);

	job->elapsedSeconds = std::chrono::duration<double>(
		std::chrono::steady_clock::now() - startTime).count();

	switch (status)
	{
	case StemSeparator::Status::Success:
		job->stems = stems;
		job->progress.store(1.0f);
		job->state.store(State::Completed);
		emit jobProgress(job->id, 1.0f);
		emit jobFinished(job->id, job->elapsedSeconds);
		break;
	case StemSeparator::Status::Cancelled:
		// Partial stems are kept when the backend produced them.
		job->stems = stems;
		job->state.store(State::Cancelled);
		emit jobCancelled(job->id);
		break;
	case StemSeparator::Status::Failed:
	default:
		job->error = error;
		job->state.store(State::Failed);
		emit jobFailed(job->id, error);
		break;
	}
}

} // namespace lmms
