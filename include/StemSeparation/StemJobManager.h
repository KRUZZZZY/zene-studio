/*
 * StemJobManager.h - background job manager for offline stem separation
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

#ifndef LMMS_STEM_JOB_MANAGER_H
#define LMMS_STEM_JOB_MANAGER_H

#include <atomic>
#include <deque>
#include <functional>
#include <memory>

#include <QMutex>
#include <QHash>
#include <QObject>
#include <QString>
#include <QWaitCondition>

#include "StemSeparation/StemSeparator.h"
#include "StemSeparation/StemTypes.h"
#include "lmms_export.h"

class QThread;

namespace lmms
{

class SampleBuffer;

// Runs stem-separation jobs on one dedicated worker thread. Everything here is
// strictly offline: the manager never touches the audio thread, never blocks
// the GUI thread, and every job is cancellable between inference segments.
//
// The manager owns the inference backend (StemSeparator). A job moves through
//
//   Queued -> Running -> Completed
//                     -> CancelRequested -> Cancelled
//                     -> Failed
//
// and reports progress through the jobProgress() Qt signal. Signals are
// emitted from the worker thread and delivered to the receiver's thread
// (Qt::AutoConnection => queued for a manager living in the GUI thread).
class LMMS_EXPORT StemJobManager : public QObject
{
	Q_OBJECT
public:
	enum class State
	{
		Queued,
		Running,
		CancelRequested,
		Completed,
		Cancelled,
		Failed
	};
	Q_ENUM(State)

	// One separation job. Progress and cancellation are atomics so that the
	// worker thread can update them without locking; everything else is only
	// touched by the worker thread and the owning thread after completion.
	struct Job
	{
		int id = -1;
		std::shared_ptr<const SampleBuffer> mix;
		int sampleRate = StemModelSampleRate;
		int segmentFrames = HTDemucsSegmentFrames;
		std::atomic<State> state{State::Queued};
		std::atomic<float> progress{0.0f};
		std::atomic<bool> cancelRequested{false};
		float lastEmittedProgress = -1.0f;
		//! Owner decision 14: true while the worker is fetching the model this
		//! job needs (first use), before separation starts; and how far it got.
		std::atomic<bool> fetchingModel{false};
		std::atomic<float> downloadProgress{0.0f};
		double elapsedSeconds = 0.0;
		StemSet stems{};
		QString error;
	};

	explicit StemJobManager(QObject* parent = nullptr);
	~StemJobManager() override;

	// Takes ownership of the inference backend. Must be called before the first
	// submit(); primarily used to inject the backend (and test doubles).
	void setSeparator(std::unique_ptr<StemSeparator> separator);

	// Empty string when no backend was set.
	QString backendName() const;

	/*! Owner decision 14: run before every job's separation, ON THE WORKER
	 *  THREAD, to make the model present (fetch it on first use). It returns at
	 *  once when the model is already there; it reports progress in [0, 1] and
	 *  must give up when `cancel` becomes true. A false return fails the job
	 *  with `error`. Unset (the default, and every test double): no step. */
	using ModelProvisioner = std::function<bool(const std::atomic<bool>& cancel,
		const std::function<void(float)>& progress, QString* error)>;
	void setModelProvisioner(ModelProvisioner provisioner);

	//! The first-use fetch of a job, for a caller reporting it (false/0 when
	//! the job is unknown or fetched nothing).
	bool fetchingModel(int jobId) const;
	float downloadProgress(int jobId) const;

	// Queues a job and returns its id immediately. The caller keeps ownership
	// of the mix buffer through the shared_ptr. `mix` must not be null.
	int submit(std::shared_ptr<const SampleBuffer> mix,
		int sampleRate,
		int segmentFrames = HTDemucsSegmentFrames);

	void cancel(int jobId);
	void cancelAll();

	State state(int jobId) const;
	float progress(int jobId) const;
	StemSet result(int jobId) const;
	QString error(int jobId) const;

	// Number of jobs currently in Running or CancelRequested state. The manager
	// guarantees this never exceeds 1 (single worker thread).
	int runningJobCount() const;

signals:
	void jobStarted(int jobId);
	void jobProgress(int jobId, float fraction);
	void jobFinished(int jobId, double elapsedSeconds);
	void jobCancelled(int jobId);
	void jobFailed(int jobId, const QString& error);

private:
	void workerLoop();
	void runJob(const std::shared_ptr<Job>& job);
	//! The provisioner step of runJob: false when it ended the job (the
	//! terminal state and its signal are already published).
	bool provisionModel(const std::shared_ptr<Job>& job);
	std::shared_ptr<Job> findJob(int jobId) const;

	QThread* m_thread = nullptr;
	mutable QMutex m_mutex;
	QWaitCondition m_jobAvailable;
	std::deque<std::shared_ptr<Job>> m_queue;
	mutable QHash<int, std::shared_ptr<Job>> m_jobs;
	std::unique_ptr<StemSeparator> m_separator;
	ModelProvisioner m_provisioner;
	int m_nextJobId = 1;
	bool m_quit = false;
};

} // namespace lmms

#endif // LMMS_STEM_JOB_MANAGER_H
