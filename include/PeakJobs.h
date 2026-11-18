/*
 * PeakJobs.h - waveform analysis of a browser file as a background job
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

#ifndef LMMS_PEAK_JOBS_H
#define LMMS_PEAK_JOBS_H

#include <functional>
#include <memory>

#include <QHash>
#include <QObject>
#include <QString>
#include <QThreadPool>

#include "lmms_export.h"

namespace lmms
{

/*! Q050-010: reading a long file's peaks on the interface thread freezes the window, so
 *  browser.peaks_start and the browser's waveform view hand the read to one worker thread.
 *  The worker only fills a private BrowserPeakCache::Entry; the entry reaches the cache back on
 *  the interface thread, where a cancel that came in late still discards it.
 *
 *  The id start() returns IS the job's TaskCenter id, so the start reply, app.tasks ("peak-<id>")
 *  and the task area's Cancel all name the same job however many other tasks have started.
 *  Everything but the worker's read belongs to the main thread, so the job table needs no lock:
 *  the worker sees a job through its atomics only. Offline file I/O: never on the audio thread. */
class LMMS_EXPORT PeakJobs : public QObject
{
	Q_OBJECT
public:
	static PeakJobs& instance();

	/*! Analyse @a path; returns the task id. A file already being analysed returns that job's id, and
	 *  a file whose peaks are already cached ends its task as done before this returns. */
	int start(const QString& path);
	//! Stop job @a id (queued or running): the cache stays as it was. False when it is not open.
	bool cancel(int id);
	//! Whether job @a id is still queued or running.
	bool isOpen(int id) const;

	//! Test seam: runs on the worker before each read, so a test can hold the job there.
	void setBeforeComputeHookForTesting(std::function<void()> hook);
	//! Test seam: wait for the worker to go idle WITHOUT turning the event loop, leaving its result unadopted.
	bool waitForWorkerForTesting(int timeoutMs);

signals:
	//! @a path's peaks are in the cache now (main thread).
	void ready(int id, const QString& path);
	//! Job @a id ended without peaks: @a error says why; a cancel leaves @a cancelled true (main thread).
	void ended(int id, const QString& path, const QString& error, bool cancelled);

private:
	struct Job;

	PeakJobs();
	void runOnWorker(const std::shared_ptr<Job>& job);
	void close(const std::shared_ptr<Job>& job, bool ok, bool cancelled, const QString& error);

	QThreadPool m_pool;
	QHash<int, std::shared_ptr<Job>> m_jobs;
	QHash<QString, int> m_jobByPath;
	std::function<void()> m_beforeCompute;
	int m_nextLocal = 1;
};

} // namespace lmms

#endif // LMMS_PEAK_JOBS_H
