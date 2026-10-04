/*
 * ControlCommandsTask.cpp - task.*: the background jobs' list, over the socket
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

/*! 0.5.0 UX decision (theme 24): what the status bar's Tasks list shows (TaskCenter.h), for an agent and a script:
 *  - task.list: every long job of this session - render-queue renders and conversions, stem splits, mastering runs,
 *    library indexing, video audio extraction - with its state, progress and whether it can be cancelled;
 *  - task.cancel: stop one, where the job supports it (the job's own cancel: render.queue_cancel's, stem.job_cancel's,
 *    the index scan's, the extraction's);
 *  - task.clear_finished: forget the ended ones.
 *  Session state: nothing is journalled and nothing is saved. */

#include <QJsonArray>

#include "ControlCommandDef.h"
#include "ControlVocabulary.h"
#include "TaskCenter.h"
#include "TaskCommands.h"

namespace lmms
{

using namespace control;

namespace
{

ControlResult listTasks(const QJsonObject&)
{
	QJsonArray tasks;
	for (const TaskInfo& task : TaskCenter::instance().tasks()) { tasks.append(TaskCenter::toJson(task)); }
	return ControlResult::success({{QStringLiteral("tasks"), tasks}, {QStringLiteral("active"), TaskCenter::instance().activeCount()}});
}

//! The task @a id, or nullptr when the list has none.
const TaskInfo* findTask(const std::vector<TaskInfo>& tasks, int id)
{
	for (const TaskInfo& task : tasks)
	{
		if (task.id == id) { return &task; }
	}
	return nullptr;
}

ControlResult cancelTask(const QJsonObject& args)
{
	const int id = args.value(QStringLiteral("id")).toInt(-1);
	const std::vector<TaskInfo> tasks = TaskCenter::instance().tasks();
	const TaskInfo* task = findTask(tasks, id);
	if (task == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no task %1 (task.list)").arg(id)); }
	if (!task->active())
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("task %1 has already ended (%2)")
			.arg(id).arg(TaskCenter::stateName(task->state)));
	}
	if (!task->cancellable)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("task %1 (%2) cannot be cancelled: it runs to its end")
			.arg(id).arg(task->kind));
	}
	TaskCenter::instance().cancel(id);
	// The job ends itself - at once, or at its next safe point - and the list then shows it cancelled.
	return ControlResult::success({{QStringLiteral("id"), id}, {QStringLiteral("cancelling"), true}});
}

ControlResult clearFinished(const QJsonObject&)
{
	return ControlResult::success({{QStringLiteral("removed"), TaskCenter::instance().clearFinished()},
		{QStringLiteral("active"), TaskCenter::instance().activeCount()}});
}

} // namespace

void registerTaskCommands(ControlRegistry& registry)
{
	defineCommandWithSchema(registry, QStringLiteral("task.list"), QStringLiteral("The long jobs of this session, oldest first - "
		"render-queue renders and conversions, stem splits, mastering runs, library indexing, video audio extraction - each "
		"{id, kind, title, status, progress (0..1, or -1 while the job cannot say), state (queued, running, done, failed, "
		"cancelled), cancellable, started_ms, finished_ms}, and how many are 'active'. What the status bar's Tasks list "
		"shows. Read-only."), objectSchema({}), false, listTasks, false);
	defineCommandWithSchema(registry, QStringLiteral("task.cancel"), QStringLiteral("Stop task 'id' (task.list), if its job "
		"can be stopped: a queued one is dropped, a running one stops at its next safe point and removes what it had "
		"written. Refused for a task that has ended or whose job runs to its end (a mastering run). The project is not "
		"touched."), objectSchema({{QStringLiteral("id"), integerProperty()}}, QJsonArray{QStringLiteral("id")}), false,
		cancelTask, false);
	defineCommandWithSchema(registry, QStringLiteral("task.clear_finished"), QStringLiteral("Forget every ended task in the "
		"list (done, failed or cancelled); running and queued ones stay. Session state."), objectSchema({}), false,
		clearFinished, false);
	registerTaskWindowCommand(registry);
}

} // namespace lmms
