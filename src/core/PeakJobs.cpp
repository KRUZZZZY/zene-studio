/*
 * PeakJobs.cpp - waveform analysis of a browser file as a background job
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

#include "PeakJobs.h"

#include <atomic>
#include <utility>

#include <QCoreApplication>
#include <QFileInfo>
#include <QMetaObject>

#include "BrowserCatalog.h"
#include "BrowserPeakCache.h"
#include "TaskCenter.h"

namespace lmms
{

struct PeakJobs::Job
{
	int id = -1;  //!< the TaskCenter id everybody calls this job by
	QString path;
	TaskToken token;
	std::atomic<bool> cancel{false};
	//! Set by whichever of the worker and a cancel gets there first: a queued job the cancel
	//! claimed is closed at once, and the worker skips it when its turn comes.
	std::atomic<bool> claimed{false};
	std::atomic<double> progress{0.0};
};

PeakJobs& PeakJobs::instance()
{
	// Never destroyed, like the stem engine's manager: joining a worker during static destruction is not safe.
	static auto* jobs = new PeakJobs();
	return *jobs;
}

PeakJobs::PeakJobs()
{
	m_pool.setMaxThreadCount(1);  // one disk read at a time, in order
	if (QCoreApplication::instance() == nullptr) { return; }
	connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
		for (const std::shared_ptr<Job>& job : std::as_const(m_jobs)) { job->cancel.store(true); }
		m_pool.waitForDone();
	});
}

int PeakJobs::start(const QString& path)
{
	const QString key = browserCanonicalKey(path);
	// A job already told to stop will end cancelled: asking again means a new job, not that one.
	const int running = m_jobByPath.value(key, -1);
	const auto open = m_jobs.constFind(running);
	if (open != m_jobs.constEnd() && !(*open)->cancel.load()) { return running; }

	auto job = std::make_shared<Job>();
	job->path = key;
	const int local = m_nextLocal++;
	job->token = TaskCenter::instance().begin(this, local, {QStringLiteral("browser_peak"),
		tr("Analyse %1").arg(QFileInfo(key).fileName()),
		[this, job] { if (job->id >= 0) { cancel(job->id); } },
		[job] { return job->progress.load(); }, true});
	job->id = TaskCenter::instance().idOf(this, local);

	if (BrowserPeakCache::instance().isCurrent(key))
	{
		TaskCenter::instance().finish(job->token, TaskState::Done);
		return job->id;
	}

	m_jobs.insert(job->id, job);
	m_jobByPath.insert(key, job->id);
	m_pool.start([this, job] { runOnWorker(job); });
	return job->id;
}

bool PeakJobs::cancel(int id)
{
	const auto found = m_jobs.constFind(id);
	if (found == m_jobs.constEnd()) { return false; }
	const std::shared_ptr<Job> job = *found;
	job->cancel.store(true);
	// Running: the worker stops at its next block, and its report - or a result already on its
	// way here - finds the flag set. Queued: nothing else will ever report, so close it now.
	if (!job->claimed.exchange(true)) { close(job, false, true, QString()); }
	return true;
}

bool PeakJobs::isOpen(int id) const
{
	return m_jobs.contains(id);
}

void PeakJobs::setBeforeComputeHookForTesting(std::function<void()> hook)
{
	m_beforeCompute = std::move(hook);
}

bool PeakJobs::waitForWorkerForTesting(int timeoutMs)
{
	return m_pool.waitForDone(timeoutMs);
}

void PeakJobs::runOnWorker(const std::shared_ptr<Job>& job)
{
	if (job->claimed.exchange(true)) { return; }
	TaskCenter::instance().progress(job->token, 0.0);
	if (m_beforeCompute) { m_beforeCompute(); }

	auto entry = std::make_shared<BrowserPeakCache::Entry>();
	QString error;
	const QFileInfo info(job->path);
	entry->modifiedMs = info.lastModified().toMSecsSinceEpoch();
	entry->sizeBytes = info.size();
	const bool ok = BrowserPeakCache::computeFor(job->path, &job->cancel, &job->progress, entry.get(), &error);

	QMetaObject::invokeMethod(this, [this, job, entry, ok, error] {
		// Checked here, on the thread that publishes: a cancel after the read must still win.
		if (job->cancel.load())
		{
			close(job, false, true, QString());
			return;
		}
		if (ok) { BrowserPeakCache::instance().adopt(std::move(*entry)); }
		close(job, ok, false, error);
	}, Qt::QueuedConnection);
}

void PeakJobs::close(const std::shared_ptr<Job>& job, bool ok, bool cancelled, const QString& error)
{
	m_jobs.remove(job->id);
	if (m_jobByPath.value(job->path, -1) == job->id) { m_jobByPath.remove(job->path); }
	if (ok)
	{
		TaskCenter::instance().finish(job->token, TaskState::Done);
		emit ready(job->id, job->path);
		return;
	}
	TaskCenter::instance().finish(job->token, cancelled ? TaskState::Cancelled : TaskState::Failed, error);
	emit ended(job->id, job->path, error, cancelled);
}

} // namespace lmms
