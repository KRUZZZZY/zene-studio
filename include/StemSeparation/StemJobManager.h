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
#include <functional>
#include <memory>

#include <QObject>
#include <QString>

#include "StemSeparation/StemSeparator.h"
#include "StemSeparation/StemTypes.h"
#include "lmms_export.h"

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
// forwarded from the worker thread to the manager and delivered to the
// receiver's thread (queued for a manager living in the GUI thread). Destroying
// the manager requests cancellation and returns without joining the worker; its
// shared state keeps the backend alive until the current call ends.
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
	//! Everything the worker touches: jobs, queue, backend, provisioner and the
	//! quit flag. The worker holds it by shared_ptr, so it outlives this object.
	struct SharedState;
	static void workerLoop(const std::shared_ptr<SharedState>& state);
	//! 0.5.0 theme 24: the job signals reported to the task area (TaskCenter.h).
	void reportToTaskArea();
	static void runJob(const std::shared_ptr<SharedState>& state, const std::shared_ptr<Job>& job);
	//! The provisioner step of runJob: false when it ended the job (the
	//! terminal state and its signal are already published).
	static bool provisionModel(const std::shared_ptr<SharedState>& state, const std::shared_ptr<Job>& job);
	//! The worker's signals: queued to this manager, dropped once it is destroyed.
	static void emitJobStarted(const std::shared_ptr<SharedState>& state, int jobId);
	static void emitJobProgress(const std::shared_ptr<SharedState>& state, int jobId, float fraction);
	static void emitJobFinished(const std::shared_ptr<SharedState>& state, int jobId, double elapsedSeconds);
	static void emitJobCancelled(const std::shared_ptr<SharedState>& state, int jobId);
	static void emitJobFailed(const std::shared_ptr<SharedState>& state, int jobId, const QString& error);
	std::shared_ptr<Job> findJob(int jobId) const;

	std::shared_ptr<SharedState> m_state;
};

} // namespace lmms

#endif // LMMS_STEM_JOB_MANAGER_H
