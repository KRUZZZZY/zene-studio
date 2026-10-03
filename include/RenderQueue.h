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

#include <vector>

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "lmms_export.h"

class QProcess;

namespace lmms
{

//! One queued job: a render of a project file, or a conversion of an audio file.
struct LMMS_EXPORT RenderJob
{
	enum class Kind { Render, Convert };
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
	State state = State::Queued;
	int exitCode = 0;
	QString error;
	qint64 queuedMs = 0;
	qint64 startedMs = 0;
	qint64 finishedMs = 0;
	//! What came out: the file's bytes and hash, and the loudness meter's reading of it.
	QJsonObject report;
};

/*! 0.5.0 L12 (WF-040): a render queue. Renders run as the shipped CLI render in a CHILD process, as
 *  render.render does, but started asynchronously (QProcess signals on the UI thread), so queueing
 *  and following them never blocks the control surface; conversions decode, resample and write on
 *  a worker thread. One job runs at a time, in order. A finished job carries its report - bytes,
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
	//! Cancel job @a id: dropped when queued, stopped when running. False when it is not cancellable.
	bool cancel(int id);
	//! Remove every job that has ended; how many were removed.
	int clearFinished();
	bool isRunning() const { return m_current >= 0; }
	const std::vector<RenderJob>& jobs() const { return m_jobs; }
	const RenderJob* job(int id) const;
	//! Decode @a job's source and write it as @a job asks; empty on success, else the reason.
	static QString convert(const RenderJob& job);

private:
	RenderQueue() = default;
	RenderJob* find(int id);
	void next();
	void runRender(RenderJob& job);
	void runConvert(RenderJob& job);
	void finish(int id, bool ok, int exitCode, const QString& error);

	std::vector<RenderJob> m_jobs;
	int m_nextId = 1;
	int m_current = -1;
	QProcess* m_process = nullptr;
};

} // namespace lmms

#endif // LMMS_RENDER_QUEUE_H
