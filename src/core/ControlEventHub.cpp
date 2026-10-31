/*
 * ControlEventHub.cpp - ordered, bounded project-change events for local control clients (Q050-334.1).
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

#include "ControlEventHub.h"

#include <atomic>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QThread>
#include <QUuid>

#include "ControlRegistry.h"
#include "ProjectJournal.h"

namespace lmms::control
{

namespace
{

//! The connection being served right now (UI thread; ConnectionScope is its only writer).
const ControlEventHub::Connection* s_currentConnection = nullptr;

QByteArray compact(const QJsonObject& object)
{
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

qint64 wire(quint64 value) { return static_cast<qint64>(value); }

//! ProjectJournal's change, in the word an event carries.
QString journalKind(ProjectJournal::Change change)
{
	switch (change)
	{
	case ProjectJournal::Change::Undo: return QStringLiteral("undo");
	case ProjectJournal::Change::Redo: return QStringLiteral("redo");
	case ProjectJournal::Change::ProjectReplaced: return QStringLiteral("project_replaced");
	case ProjectJournal::Change::Step: break;
	}
	return QStringLiteral("edit");
}

} // namespace

bool ControlEventQueue::push(quint64 seq, const QByteArray& line)
{
	if (overflowed() || m_lines.size() >= m_limit || m_bytes + line.size() > MaxQueuedBytes)
	{
		if (m_missed == 0) { m_firstMissed = seq; }
		++m_missed;
		return false;
	}
	m_lines.enqueue(Entry{seq, line});
	m_bytes += line.size();
	return true;
}

bool ControlEventQueue::take(QByteArray* line, quint64* seq)
{
	if (m_lines.isEmpty()) { return false; }
	const Entry entry = m_lines.dequeue();
	m_bytes -= entry.line.size();
	*line = entry.line;
	*seq = entry.seq;
	return true;
}

ControlEventHub* ControlEventHub::s_instance = nullptr;

ControlEventHub::ControlEventHub() :
	m_session(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
	s_instance = this;
	ProjectJournal::setChangeListener([](ProjectJournal::Change change) {
		if (s_instance != nullptr) { s_instance->journalChanged(journalKind(change)); }
	});
}

ControlEventHub::~ControlEventHub()
{
	ProjectJournal::setChangeListener(nullptr);
	s_instance = nullptr;
}

ControlEventHub& ControlEventHub::instance()
{
	static ControlEventHub hub;
	return hub;
}

quint64 ControlEventHub::nextConnectionKey()
{
	static std::atomic<quint64> next{1};
	return next.fetch_add(1);
}

ControlEventHub::ConnectionScope::ConnectionScope(const Connection& connection) :
	m_previous(s_currentConnection)
{
	s_currentConnection = &connection;
}

ControlEventHub::ConnectionScope::~ConnectionScope()
{
	s_currentConnection = m_previous;
}

void ControlEventHub::commandFinished(const QString& commandId, const QJsonObject& args, bool mutating, bool changed,
	bool enclosedByMutating, int depthAfter)
{
	if (s_instance != nullptr)
	{
		s_instance->onCommandFinished(commandId, args, mutating, changed, enclosedByMutating, depthAfter);
	}
}

void ControlEventHub::onCommandFinished(const QString& commandId, const QJsonObject& args, bool mutating, bool changed,
	bool enclosedByMutating, int depthAfter)
{
	// A mutating call nested in a mutating one is part of that one's change. One nested in a non-mutating wrapper
	// (arrangement.nudge, workflow.run) is its own change: nothing else would report it.
	if (mutating && changed && !enclosedByMutating)
	{
		publish(QStringLiteral("command"), QStringLiteral("edit"), commandId, args);
		m_publishedInDispatch = true;
	}
	if (depthAfter > 0) { return; }
	// The journal moved under a command that reported nothing itself: control.undo and control.redo.
	if (m_journalChangedInDispatch && !m_publishedInDispatch)
	{
		const QString kind = commandId == QLatin1String("control.undo") ? QStringLiteral("undo")
			: commandId == QLatin1String("control.redo") ? QStringLiteral("redo") : QStringLiteral("edit");
		publish(QStringLiteral("command"), kind, commandId, args);
	}
	m_journalChangedInDispatch = false;
	m_publishedInDispatch = false;
}

void ControlEventHub::journalChanged(const QString& kind)
{
	QCoreApplication* const app = QCoreApplication::instance();
	if (app != nullptr && QThread::currentThread() != app->thread())
	{
		QMetaObject::invokeMethod(app, [this, kind]() { journalChanged(kind); }, Qt::QueuedConnection);
		return;
	}
	if (ControlRegistry::runningHandlers() > 0)
	{
		m_journalChangedInDispatch = true;  // commandFinished reports it, by the command's name
		return;
	}
	publish(QStringLiteral("journal"), kind, QString(), QJsonObject());
}

void ControlEventHub::publish(const QString& source, const QString& kind, const QString& command,
	const QJsonObject& args)
{
	++m_seq;
	for (auto it = m_subscribers.begin(); it != m_subscribers.end(); ++it)
	{
		QJsonObject event{{QStringLiteral("event"), QStringLiteral("project_changed")},
			{QStringLiteral("subscription"), it->id}, {QStringLiteral("session"), m_session},
			{QStringLiteral("seq"), wire(m_seq)}, {QStringLiteral("source"), source}, {QStringLiteral("kind"), kind}};
		if (!command.isEmpty()) { event.insert(QStringLiteral("command"), command); }
		const QJsonObject bare = event;
		if (!command.isEmpty()) { event.insert(QStringLiteral("args"), args); }
		QByteArray line = compact(event);
		if (line.size() > MaxEventLineBytes)
		{
			QJsonObject trimmed = bare;
			trimmed.insert(QStringLiteral("args_omitted"), true);
			line = compact(trimmed);
		}
		it->queue.push(m_seq, line);
		if (!it->wakePosted)
		{
			it->wakePosted = true;
			it->connection.wake();
		}
	}
}

bool ControlEventHub::hasCurrentConnection() const
{
	return s_currentConnection != nullptr;
}

ControlEventHub::SubscribeResult ControlEventHub::subscribe(int eventLimit)
{
	SubscribeResult out;
	if (s_currentConnection == nullptr)
	{
		out.noConnection = true;
		return out;
	}
	if (m_subscribers.contains(s_currentConnection->key)) { return out; }
	Subscriber subscriber;
	subscriber.id = QStringLiteral("sub-%1").arg(m_nextSubscription++);
	subscriber.connection = *s_currentConnection;
	subscriber.queue = ControlEventQueue(eventLimit);
	subscriber.lastDelivered = m_seq;
	m_subscribers.insert(s_currentConnection->key, subscriber);
	out.ok = true;
	out.result = QJsonObject{{QStringLiteral("subscription"), subscriber.id}, {QStringLiteral("session"), m_session},
		{QStringLiteral("seq"), wire(m_seq)}, {QStringLiteral("queue_limit"), eventLimit},
		{QStringLiteral("queue_byte_limit"), ControlEventQueue::MaxQueuedBytes}};
	return out;
}

QJsonObject ControlEventHub::unsubscribe(const QString& subscription)
{
	if (s_currentConnection == nullptr) { return QJsonObject(); }
	const auto it = m_subscribers.find(s_currentConnection->key);
	if (it == m_subscribers.end() || it->id != subscription) { return QJsonObject(); }
	const QJsonObject result{{QStringLiteral("unsubscribed"), true}, {QStringLiteral("subscription"), subscription},
		{QStringLiteral("last_delivered_seq"), wire(it->lastDelivered)}, {QStringLiteral("discarded"), it->queue.size()}};
	m_subscribers.erase(it);
	return result;
}

bool ControlEventHub::nextLine(quint64 key, QByteArray* line)
{
	const auto it = m_subscribers.find(key);
	if (it == m_subscribers.end()) { return false; }
	it->wakePosted = false;
	quint64 seq = 0;
	if (it->queue.take(line, &seq))
	{
		it->lastDelivered = seq;
		++it->delivered;
		return true;
	}
	if (!it->queue.overflowed()) { return false; }
	*line = compact(QJsonObject{{QStringLiteral("event"), QStringLiteral("events_overflow")},
		{QStringLiteral("subscription"), it->id}, {QStringLiteral("session"), m_session},
		{QStringLiteral("last_delivered_seq"), wire(it->lastDelivered)},
		{QStringLiteral("first_missed_seq"), wire(it->queue.firstMissedSeq())},
		{QStringLiteral("missed"), wire(it->queue.missedCount())}, {QStringLiteral("seq"), wire(m_seq)},
		{QStringLiteral("message"), QStringLiteral("this subscription has ended and the events from first_missed_seq "
			"on were never delivered: call events.subscribe again, then reread the project (project.get_state and the other "
			"read commands)")}});
	++m_overflows;
	m_subscribers.erase(it);
	return true;
}

void ControlEventHub::connectionClosed(quint64 key)
{
	if (m_subscribers.remove(key) > 0) { ++m_disconnects; }
}

void ControlEventHub::ownerClosed(const void* owner)
{
	for (auto it = m_subscribers.begin(); it != m_subscribers.end();)
	{
		if (it->connection.owner == owner) { it = m_subscribers.erase(it); }
		else { ++it; }
	}
}

QJsonObject ControlEventHub::state() const
{
	QJsonArray subscriptions;
	for (auto it = m_subscribers.cbegin(); it != m_subscribers.cend(); ++it)
	{
		subscriptions.append(QJsonObject{{QStringLiteral("subscription"), it->id},
			{QStringLiteral("queued"), it->queue.size()}, {QStringLiteral("queued_bytes"), it->queue.bytes()},
			{QStringLiteral("queue_limit"), it->queue.eventLimit()}, {QStringLiteral("overflowed"), it->queue.overflowed()},
			{QStringLiteral("delivered"), wire(it->delivered)}, {QStringLiteral("last_delivered_seq"), wire(it->lastDelivered)}});
	}
	return QJsonObject{{QStringLiteral("session"), m_session}, {QStringLiteral("seq"), wire(m_seq)},
		{QStringLiteral("subscriptions"), subscriptions}, {QStringLiteral("overflows"), wire(m_overflows)},
		{QStringLiteral("disconnects"), wire(m_disconnects)},
		{QStringLiteral("default_queue_limit"), ControlEventQueue::DefaultEventLimit},
		{QStringLiteral("max_queue_limit"), ControlEventQueue::MaxEventLimit},
		{QStringLiteral("queue_byte_limit"), ControlEventQueue::MaxQueuedBytes}};
}

} // namespace lmms::control
