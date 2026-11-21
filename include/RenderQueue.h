/*
 * RenderQueue.h - renders and file conversions queued and run one after another, with a report each
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
 *
 */

#ifndef LMMS_RENDER_QUEUE_H
#define LMMS_RENDER_QUEUE_H

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "BounceInPlace.h"
#include "ControlExportPresetSupport.h"
#include "TaskCenter.h"
#include "lmms_export.h"

class QProcess;
class QTimer;

namespace lmms
{

//! One queued job: a render of a project file, or a conversion of an audio file.
struct LMMS_EXPORT RenderJob
{
	enum class Kind { Render, Convert, Bounce };
	enum class State { Queued, Running, Done, Failed, Cancelled };

	int id = 0;
	Kind kind = Kind::Render;
	QString source;      //!< the project file rendered, or the audio file converted
	bool ownsSource = false;  //!< a session snapshot the queue deletes when the job ends
	QString out;
	QString format = QStringLiteral("wav");
	int rangeBegin = -1;
	int rangeEnd = -1;
	int sampleRate = 0;  //!< a conversion's rate (0 keeps the file's own)
	int bitDepth = 24;   //!< a conversion's sample format: 16, 24 or 32 (float)
	//! A render's export settings as in force when it was queued (RenderQueue::add): a preset applied while
	//! it waits is the next job's, not this one's.
	ControlExportPreset settings;
	State state = State::Queued;
	int exitCode = 0;
	QString error;
	QString operation;
	QJsonObject commandArgs;
	std::vector<int> targetIds;
	QString phase = QStringLiteral("queued");
	double progress = -1.0;
	qint64 queuedMs = 0;
	qint64 startedMs = 0;
	qint64 finishedMs = 0;
	//! What came out: the file's bytes and hash, and the loudness meter's reading of it.
	QJsonObject report;
	//! The task-area token of this job's current generation; its reports carry it (RenderQueue::add).
	TaskToken taskToken;
	//! This render or conversion's own staging file, beside its destination and named for this job alone;
	//! made when first asked for and never on a name anyone else could be using (RenderQueue::stagedPath).
	mutable QString staged;
	//! The render's resolved destination (so a symlink target stays stable while its staged output is written).
	mutable QString renderDestination;
	//! A running conversion's cooperative worker stop signal and lifecycle marker.
	std::shared_ptr<std::atomic_bool> conversionCancellation;
	bool conversionWorkerActive = false;
	//! Bounce inputs are captured on the main thread; callbacks re-check and apply model state there.
	BounceInPlace::PreparedRender bounce;
	std::function<bool(QString*)> validateBounce;
	std::function<QJsonObject(const BounceInPlace::Result&, QString*)> applyBounce;
};

/*! 0.5.0 L12 (WF-040): a render queue. Renders run as the shipped CLI render in a CHILD process, as
 *  render.render does, but started asynchronously (QProcess signals on the UI thread), so queueing
 *  and following them never blocks the control surface; conversions decode, resample and write on
 *  a worker thread. Jobs start in order; cancelling a conversion releases the queue slot immediately
 *  while its worker observes cancellation at stage and write-chunk boundaries. A finished job carries
 *  its report - bytes,
 *  sha256, seconds taken, and the loudness meter's reading of the file (meter.measure_file's
 *  payload). Session state only: jobs are not saved and nothing is journalled. Bounded: MaxJobs. */
class LMMS_EXPORT RenderQueue : public QObject
{
	Q_OBJECT
public:
	static constexpr int MaxJobs = 256;

	static RenderQueue& instance();
	static QString kindName(RenderJob::Kind kind);
	static QString stateName(RenderJob::State state);

	//! Queue @a job (its id is assigned); the id, or -1 when the queue is full.
	int add(RenderJob job);
	//! Start working through the queue (nothing when a job is already running).
	void start();
	//! Cancel @a id: drop queued jobs; kill renders and stop conversions cooperatively. False when not cancellable.
	bool cancel(int id);
	//! Remove every job that has ended; how many were removed.
	int clearFinished();
	bool isRunning() const { return m_current >= 0; }
	bool hasCapacity() const { return static_cast<int>(m_jobs.size()) < MaxJobs; }
	quint64 capacityRefusals() const { return m_capacityRefusals; }
	void recordCapacityRefusal() { ++m_capacityRefusals; }
	const std::vector<RenderJob>& jobs() const { return m_jobs; }
	const RenderJob* job(int id) const;
	//! Decode @a job's source and write it as @a job asks; empty on success, else the reason.
	static QString convert(const RenderJob& job);

private:
	RenderQueue() = default;
	RenderJob* find(int id);
	void next();
	void runRender(RenderJob& job);
	void runBounce(RenderJob& job);
	void bounceStarted(int id);
	void bounceTimedOut(int id);
	void bounceProcessError(int id, int error);
	void clearBounceProcess();
	void runConvert(RenderJob& job);
	void finish(const TaskToken& token, int id, bool ok, int exitCode, const QString& error);
	void bounceProcessEnded(int id, bool ok, int exitCode, const QString& error);
	void beginBounceFinalization(int id);
	void finishBounce(int id, BounceInPlace::PreparedRender prepared, BounceInPlace::Result rendered,
		const QString& error);
	bool applyFinishedBounce(RenderJob& job, BounceInPlace::PreparedRender* prepared,
		const BounceInPlace::Result& rendered, QJsonObject* applied, QString* error);
	void failBounce(RenderJob& job, int id, const QString& error);
	void finishBounceJob(RenderJob& job, RenderJob::State state, const QString& error);
	void cancelQueued(RenderJob& job);
	void releaseCurrent(int id);

	std::vector<RenderJob> m_jobs;
	int m_nextId = 1;
	int m_current = -1;
	QProcess* m_process = nullptr;
	QTimer* m_bounceTimer = nullptr;
	QString m_bounceStopReason;
	quint64 m_capacityRefusals = 0;
};

} // namespace lmms

#endif // LMMS_RENDER_QUEUE_H
