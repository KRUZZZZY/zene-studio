/*
 * TaskCenter.cpp - the long jobs the interface runs in the background, in one list
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

#include "TaskCenter.h"

#include <algorithm>
#include <utility>

#include <QCoreApplication>
#include <QDateTime>
#include <QStringList>
#include <QThread>

namespace lmms
{

namespace
{

qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }

} // namespace

TaskCenter& TaskCenter::instance()
{
	// Made on first use and handed to the main thread, wherever that use was: its slots run there.
	static TaskCenter* center = [] {
		auto* made = new TaskCenter;
		if (QCoreApplication::instance() != nullptr && made->thread() != QCoreApplication::instance()->thread())
		{
			made->moveToThread(QCoreApplication::instance()->thread());
		}
		return made;
	}();
	return *center;
}

QString TaskCenter::stateName(TaskState state)
{
	static const QStringList names{QStringLiteral("queued"), QStringLiteral("running"), QStringLiteral("done"),
		QStringLiteral("failed"), QStringLiteral("cancelled")};
	return names.value(static_cast<int>(state));
}

QJsonObject TaskCenter::toJson(const TaskInfo& task)
{
	return {{QStringLiteral("id"), task.id}, {QStringLiteral("kind"), task.kind}, {QStringLiteral("title"), task.title},
		{QStringLiteral("status"), task.status}, {QStringLiteral("progress"), task.progress},
		{QStringLiteral("state"), stateName(task.state)}, {QStringLiteral("cancellable"), task.cancellable && task.active()},
		{QStringLiteral("started_ms"), static_cast<double>(task.startedMs)},
		{QStringLiteral("finished_ms"), static_cast<double>(task.finishedMs)}};
}

void TaskCenter::onMainThread(std::function<void()> work)
{
	if (QThread::currentThread() == thread())
	{
		work();
		return;
	}
	QMetaObject::invokeMethod(this, std::move(work), Qt::QueuedConnection);
}

TaskCenter::Entry* TaskCenter::findActive(const void* owner, int localId)
{
	const auto it = std::find_if(m_entries.begin(), m_entries.end(), [owner, localId](const Entry& e) {
		return e.owner == owner && e.localId == localId && e.info.active();
	});
	return it != m_entries.end() ? &*it : nullptr;
}

TaskCenter::Entry* TaskCenter::findActive(const void* owner, int localId, quint64 generation)
{
	const auto it = std::find_if(m_entries.begin(), m_entries.end(), [owner, localId, generation](const Entry& e) {
		return e.owner == owner && e.localId == localId && e.generation == generation && e.info.active();
	});
	return it != m_entries.end() ? &*it : nullptr;
}

quint64 TaskCenter::currentGeneration(const void* owner, int localId) const
{
	std::lock_guard lock(m_generationMutex);
	const auto ownerIt = m_generations.find(owner);
	if (ownerIt == m_generations.end()) { return 0; }
	const auto taskIt = ownerIt->second.find(localId);
	return taskIt != ownerIt->second.end() ? taskIt->second : 0;
}

void TaskCenter::clearGeneration(const void* owner, int localId, quint64 generation)
{
	std::lock_guard lock(m_generationMutex);
	auto ownerIt = m_generations.find(owner);
	if (ownerIt == m_generations.end()) { return; }
	auto taskIt = ownerIt->second.find(localId);
	if (taskIt == ownerIt->second.end() || taskIt->second != generation) { return; }
	ownerIt->second.erase(taskIt);
	if (ownerIt->second.empty()) { m_generations.erase(ownerIt); }
}

TaskToken TaskCenter::begin(const void* owner, int localId, TaskSpec spec)
{
	quint64 generation;
	{
		std::lock_guard lock(m_generationMutex);
		generation = m_nextGeneration++;
		m_generations[owner][localId] = generation;
	}
	onMainThread([this, owner, localId, generation, spec = std::move(spec)]() mutable {
		doBegin(owner, localId, generation, std::move(spec));
	});
	return TaskToken(owner, localId, generation);
}

void TaskCenter::doBegin(const void* owner, int localId, quint64 generation, TaskSpec spec)
{
	if (generation != currentGeneration(owner, localId)) { return; }  // a newer begin() for this id has been called
	if (Entry* stale = findActive(owner, localId))
	{
		const quint64 staleGeneration = stale->generation;
		doFinish(owner, localId, staleGeneration, TaskState::Cancelled, QString());  // a stale one
	}
	if (generation != currentGeneration(owner, localId)) { return; }
	Entry entry;
	entry.owner = owner;
	entry.localId = localId;
	entry.generation = generation;
	entry.info.id = m_nextId++;
	entry.info.kind = spec.kind;
	entry.info.title = spec.title;
	entry.info.state = spec.queued ? TaskState::Queued : TaskState::Running;
	entry.info.status = spec.queued ? tr("Queued") : QString();
	entry.info.cancellable = static_cast<bool>(spec.cancel);
	entry.info.startedMs = now();
	entry.cancel = std::move(spec.cancel);
	entry.probe = std::move(spec.probe);
	m_entries.push_back(std::move(entry));
	emit changed();
}

void TaskCenter::progress(const TaskToken& token, double fraction, const QString& status)
{
	onMainThread([this, token, fraction, status] {
		Entry* entry = findActive(token.m_owner, token.m_localId, token.m_generation);
		if (entry == nullptr) { return; }
		entry->info.state = TaskState::Running;
		entry->info.progress = fraction < 0.0 ? -1.0 : std::min(fraction, 1.0);
		entry->info.status = status;
		emit changed();
	});
}

void TaskCenter::finish(const TaskToken& token, TaskState state, const QString& status)
{
	onMainThread([this, token, state, status] {
		doFinish(token.m_owner, token.m_localId, token.m_generation, state, status);
	});
}

void TaskCenter::doFinish(const void* owner, int localId, quint64 generation, TaskState state, const QString& status)
{
	Entry* entry = findActive(owner, localId, generation);
	if (entry == nullptr) { return; }
	entry->info.state = state == TaskState::Queued || state == TaskState::Running ? TaskState::Done : state;
	entry->info.status = status;
	if (entry->info.state == TaskState::Done) { entry->info.progress = 1.0; }
	entry->info.finishedMs = now();
	entry->cancel = nullptr;  // the job's own objects may be gone from here on
	entry->probe = nullptr;
	clearGeneration(owner, localId, generation);
	trimFinished();
	emit changed();
}

void TaskCenter::forget(const void* owner)
{
	onMainThread([this, owner] {
		std::vector<std::pair<int, quint64>> open;
		for (const Entry& e : m_entries)
		{
			if (e.owner == owner && e.info.active()) { open.emplace_back(e.localId, e.generation); }
		}
		for (const auto& [localId, generation] : open)
		{
			doFinish(owner, localId, generation, TaskState::Cancelled, QString());
		}
	});
}

std::vector<TaskInfo> TaskCenter::tasks() const
{
	std::vector<TaskInfo> out;
	out.reserve(m_entries.size());
	for (const Entry& e : m_entries)
	{
		out.push_back(e.info);
		if (e.info.state == TaskState::Running && e.probe) { out.back().progress = std::min(e.probe(), 1.0); }
	}
	return out;
}

int TaskCenter::activeCount() const
{
	return static_cast<int>(std::count_if(m_entries.begin(), m_entries.end(), [](const Entry& e) { return e.info.active(); }));
}

int TaskCenter::idOf(const void* owner, int localId) const
{
	const Entry* entry = const_cast<TaskCenter*>(this)->findActive(owner, localId);  // a read: nothing is changed
	return entry != nullptr ? entry->info.id : -1;
}

bool TaskCenter::cancel(int id)
{
	const auto it = std::find_if(m_entries.begin(), m_entries.end(), [id](const Entry& e) { return e.info.id == id; });
	if (it == m_entries.end() || !it->info.active() || !it->cancel) { return false; }
	it->info.status = tr("Cancelling...");
	const std::function<void()> stop = it->cancel;  // a copy: the job may report its end, and so trim the list, inside it
	emit changed();
	stop();
	return true;
}

int TaskCenter::clearFinished()
{
	const auto before = m_entries.size();
	m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [](const Entry& e) { return !e.info.active(); }),
		m_entries.end());
	const int removed = static_cast<int>(before - m_entries.size());
	if (removed > 0) { emit changed(); }
	return removed;
}

void TaskCenter::trimFinished()
{
	auto finished = static_cast<int>(std::count_if(m_entries.begin(), m_entries.end(), [](const Entry& e) { return !e.info.active(); }));
	for (auto it = m_entries.begin(); it != m_entries.end() && finished > MaxFinished;)
	{
		if (it->info.active()) { ++it; continue; }
		it = m_entries.erase(it);
		--finished;
	}
}

} // namespace lmms
