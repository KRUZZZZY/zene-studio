/*
 * TaskCenter.h - the long jobs the interface runs in the background, in one list
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

#ifndef LMMS_TASK_CENTER_H
#define LMMS_TASK_CENTER_H

#include <functional>
#include <vector>

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

enum class TaskState
{
	Queued,
	Running,
	Done,
	Failed,
	Cancelled
};

//! One job as the task area shows it.
struct LMMS_EXPORT TaskInfo
{
	int id = 0;
	QString kind;           //!< render, convert, stem_split, mastering, index, video_audio
	QString title;          //!< what a person reads: "Render mix.wav"
	QString status;         //!< a line under the title: "Queued", "1200 files", a failure's reason
	double progress = -1.0; //!< 0..1, or -1 while the job cannot say
	TaskState state = TaskState::Running;
	bool cancellable = false;
	qint64 startedMs = 0;
	qint64 finishedMs = 0;

	bool active() const { return state == TaskState::Queued || state == TaskState::Running; }
};

//! What a job hands the task area when it starts.
struct TaskSpec
{
	QString kind;
	QString title;
	//! Stops the job; runs on the main thread. Empty when the job cannot be cancelled - the task then has no Cancel.
	std::function<void()> cancel;
	//! The job's progress now (0..1, or -1), read on the main thread when the list is read - for a job that keeps its
	//! progress in an atomic or behind its own status call, so its worker never reports anything. Optional.
	std::function<double()> probe;
	bool queued = false;  //!< waiting its turn: shown "Queued" until its first progress
};

/*! 0.5.0 UX decision (theme 24): the one place the interface's long jobs report to - renders and conversions in the
 *  render queue, stem splits, mastering runs, library indexing, video audio extraction - so a person sees them in one
 *  non-modal list (the status bar's Tasks indicator, TaskIndicator.h) with their progress and, where the job supports
 *  it, Cancel. Short operations do not report: they stay synchronous.
 *
 *  The reporting API is four calls naming a job by its owner and the owner's own id for it, so a job never keeps a task
 *  id of its own. They may be called from any thread except the audio thread: off the main thread a call is posted to
 *  it (one posted event, the cost of the queued signal a job already emits), and the list has no lock of its own -
 *  only the main thread ever touches it. A job with a hot loop reports nothing from it: it keeps its progress in an
 *  atomic the task area reads through TaskSpec::probe. The list,
 *  cancel() and clearFinished() belong to the main thread. Session state only; at most MaxFinished ended tasks are kept. */
class LMMS_EXPORT TaskCenter : public QObject
{
	Q_OBJECT
public:
	static constexpr int MaxFinished = 20;

	static TaskCenter& instance();
	static QString stateName(TaskState state);
	static QJsonObject toJson(const TaskInfo& task);

	//! A job of @a owner, known to it as @a localId, started (or was queued).
	void begin(const void* owner, int localId, TaskSpec spec);
	//! It is @a fraction done (0..1, or -1 when it cannot say), with an optional status line; a queued job is running now.
	void progress(const void* owner, int localId, double fraction, const QString& status = QString());
	//! It ended in @a state (Done, Failed or Cancelled), with @a status - a failure's reason - when there is one.
	void finish(const void* owner, int localId, TaskState state, const QString& status = QString());
	//! @a owner is going away: its open tasks end as cancelled and its cancel functions are dropped.
	void forget(const void* owner);

	//! Every task, oldest first, with each running one's probe read.
	std::vector<TaskInfo> tasks() const;
	int activeCount() const;
	//! The id of @a owner's open task @a localId (main thread), or -1 when it has none.
	int idOf(const void* owner, int localId) const;
	//! Ask task @a id to stop; false when there is no such task, it has ended, or it cannot be cancelled.
	bool cancel(int id);
	//! Forget every ended task; how many were removed.
	int clearFinished();

signals:
	//! A task began, moved or ended (always emitted on the main thread).
	void changed();

private:
	struct Entry
	{
		TaskInfo info;
		const void* owner = nullptr;
		int localId = 0;
		std::function<void()> cancel;
		std::function<double()> probe;
	};

	TaskCenter() = default;
	//! @a work here, now, on the main thread; posted to it from any other.
	void onMainThread(std::function<void()> work);
	Entry* findActive(const void* owner, int localId);
	void doBegin(const void* owner, int localId, TaskSpec spec);
	void doFinish(const void* owner, int localId, TaskState state, const QString& status);
	void trimFinished();

	std::vector<Entry> m_entries;
	int m_nextId = 1;
};

} // namespace lmms

#endif // LMMS_TASK_CENTER_H
