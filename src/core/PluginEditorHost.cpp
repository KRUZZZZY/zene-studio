/*
 * PluginEditorHost.cpp - R4.1: the run loop a plugin editor asks its host for
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

#include "PluginEditorHost.h"

#include <algorithm>

#include <QPointer>
#include <QSocketNotifier>
#include <QTimer>

namespace lmms
{

struct PluginEditorHost::FdEntry
{
	unsigned flags = 0;
	FdCallback callback;
	std::unique_ptr<QSocketNotifier> notifiers[3];
};

struct PluginEditorHost::TimerEntry
{
	TimerCallback callback;
	std::unique_ptr<QTimer> timer;
};


PluginEditorHost::PluginEditorHost(QObject* parent) :
	QObject(parent)
{
}


PluginEditorHost::~PluginEditorHost() = default;


bool PluginEditorHost::registerFd(int fd, unsigned flags, FdCallback callback)
{
	if (fd < 0 || (flags & (Read | Write | Error)) == 0 || !callback || m_fds.count(fd) != 0) { return false; }
	auto entry = std::make_unique<FdEntry>();
	entry->callback = std::move(callback);
	entry->flags = flags;
	applyFlags(fd, *entry);
	m_fds.emplace(fd, std::move(entry));
	return true;
}


bool PluginEditorHost::modifyFd(int fd, unsigned flags)
{
	const auto it = m_fds.find(fd);
	if (it == m_fds.end() || (flags & (Read | Write | Error)) == 0) { return false; }
	it->second->flags = flags;
	applyFlags(fd, *it->second);
	return true;
}


bool PluginEditorHost::unregisterFd(int fd)
{
	const auto it = m_fds.find(fd);
	if (it == m_fds.end()) { return false; }
	// A notifier may be the sender of the signal this call is running under (a callback
	// that unregisters its own descriptor): it is disabled now and deleted by the loop.
	for (auto& notifier : it->second->notifiers)
	{
		if (notifier == nullptr) { continue; }
		notifier->setEnabled(false);
		notifier.release()->deleteLater();
	}
	m_fds.erase(it);
	return true;
}


void PluginEditorHost::applyFlags(int fd, FdEntry& entry)
{
	static constexpr QSocketNotifier::Type types[3] = {QSocketNotifier::Read, QSocketNotifier::Write,
		QSocketNotifier::Exception};
	static constexpr unsigned bits[3] = {Read, Write, Error};
	for (int i = 0; i < 3; ++i)
	{
		const bool wanted = (entry.flags & bits[i]) != 0;
		if (wanted && entry.notifiers[i] == nullptr)
		{
			entry.notifiers[i] = std::make_unique<QSocketNotifier>(fd, types[i]);
			const unsigned bit = bits[i];
			connect(entry.notifiers[i].get(), &QSocketNotifier::activated, this, [this, fd, bit] { fire(fd, bit); });
		}
		else if (!wanted && entry.notifiers[i] != nullptr)
		{
			entry.notifiers[i]->setEnabled(false);
			entry.notifiers[i].release()->deleteLater();
		}
	}
}


void PluginEditorHost::fire(int fd, unsigned flag)
{
	const auto it = m_fds.find(fd);
	if (it == m_fds.end()) { return; }
	// A copy: the callback may unregister (and so destroy) its own entry.
	const FdCallback callback = it->second->callback;
	callback(fd, flag);
}


bool PluginEditorHost::registerTimer(std::uint32_t periodMs, TimerCallback callback, std::uint32_t* id)
{
	if (!callback || id == nullptr) { return false; }
	const std::uint32_t timerId = m_nextTimerId++;
	auto entry = std::make_unique<TimerEntry>();
	entry->callback = std::move(callback);
	entry->timer = std::make_unique<QTimer>();
	entry->timer->setInterval(static_cast<int>(std::max<std::uint32_t>(periodMs, 1)));
	// Precise: an editor's repaint timer is asked for by its interval, and Qt's default
	// CoarseTimer lets macOS coalesce a 10 ms timer to a third of the ticks (hosted run
	// 36720131455, macos-arm64: 3 of the >= 5 calls in 150 ms).
	entry->timer->setTimerType(Qt::PreciseTimer);
	connect(entry->timer.get(), &QTimer::timeout, this, [this, timerId] {
		const auto it = m_timers.find(timerId);
		if (it == m_timers.end()) { return; }
		const TimerCallback callback = it->second->callback;
		callback(timerId);
	});
	entry->timer->start();
	m_timers.emplace(timerId, std::move(entry));
	*id = timerId;
	return true;
}


bool PluginEditorHost::unregisterTimer(std::uint32_t id)
{
	const auto it = m_timers.find(id);
	if (it == m_timers.end()) { return false; }
	it->second->timer->stop();
	it->second->timer.release()->deleteLater();
	m_timers.erase(it);
	return true;
}

} // namespace lmms
