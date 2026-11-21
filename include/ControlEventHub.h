/*
 * ControlEventHub.h - ordered, bounded project-change events for local control clients (Q050-334.1).
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

#ifndef LMMS_CONTROL_EVENT_HUB_H
#define LMMS_CONTROL_EVENT_HUB_H

#include <functional>

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QQueue>
#include <QString>

#include "lmms_export.h"

namespace lmms::control
{

/*! One subscriber's bounded delivery queue. Pure, so the bound is testable without a socket: once a push does not
 *  fit, the queue stops accepting for good and only counts what it refused. What it had already accepted stays in
 *  order, and a gap in the middle is impossible by construction - a subscriber sees a prefix, then the overflow. */
class LMMS_EXPORT ControlEventQueue
{
public:
	static constexpr int DefaultEventLimit = 256;
	static constexpr int MaxEventLimit = 4096;
	//! The queue's byte bound, on top of the count. No single event may reach it (MaxEventLineBytes).
	static constexpr int MaxQueuedBytes = 1024 * 1024;

	explicit ControlEventQueue(int eventLimit = DefaultEventLimit) : m_limit(eventLimit) {}

	//! Queue one line; false when it did not fit, which marks the queue overflowed.
	bool push(quint64 seq, const QByteArray& line);
	//! The oldest queued line, or false when none is left.
	bool take(QByteArray* line, quint64* seq);

	int size() const { return m_lines.size(); }
	int bytes() const { return m_bytes; }
	int eventLimit() const { return m_limit; }
	bool overflowed() const { return m_missed > 0; }
	quint64 firstMissedSeq() const { return m_firstMissed; }
	quint64 missedCount() const { return m_missed; }

private:
	struct Entry
	{
		quint64 seq;
		QByteArray line;
	};
	QQueue<Entry> m_lines;
	int m_limit;
	int m_bytes = 0;
	quint64 m_firstMissed = 0;
	quint64 m_missed = 0;
};

/*! The project-change event stream of one process (UI thread only; see journalChanged for the one exception).
 *
 *  Every project change gets the next `seq`, whether anybody is subscribed or not: that is what lets a client that
 *  reconnects compare the seq it last saw with the one events.subscribe answers, and tell it missed something.
 *  Runtime state only - nothing here reaches a project file. The transports hold no event state beyond the line
 *  they are writing: they pull with nextLine() only while their write path is idle, so all buffering is the bound
 *  above and a client that stops reading overflows its own queue, never the instance's memory. */
class LMMS_EXPORT ControlEventHub
{
public:
	//! Largest serialised event; past it the event goes out without its 'args' ('args_omitted':true).
	static constexpr int MaxEventLineBytes = 16 * 1024;

	//! The connection a request is being served for: who a subscription belongs to, and how to ask its transport
	//! to pull. \c wake must only post work: it runs inside publish.
	struct Connection
	{
		quint64 key = 0;
		const void* owner = nullptr;
		std::function<void()> wake;
	};

	//! While one is alive, events.* commands act for that connection. Transports put one around each dispatch.
	class LMMS_EXPORT ConnectionScope
	{
	public:
		explicit ConnectionScope(const Connection& connection);
		~ConnectionScope();
		ConnectionScope(const ConnectionScope&) = delete;
		ConnectionScope& operator=(const ConnectionScope&) = delete;
	private:
		const Connection* m_previous;
	};

	struct SubscribeResult
	{
		bool ok = false;
		bool noConnection = false;
		QJsonObject result;
	};

	~ControlEventHub();
	ControlEventHub(const ControlEventHub&) = delete;
	ControlEventHub& operator=(const ControlEventHub&) = delete;

	static ControlEventHub& instance();
	//! A key no other connection of this process has or had; any thread.
	static quint64 nextConnectionKey();

	//! Registry side: one finished call, of any depth. Does nothing until a control server has created the hub.
	static void commandFinished(const QString& commandId, const QJsonObject& args, bool mutating, bool changed,
		bool enclosedByMutating, int depthAfter);

	//! Journal side: a step was pushed, undone or redone, or the project was replaced. Outside a registered command
	//! this is the only sign of a GUI edit. Any thread: off the UI thread it is posted there first.
	void journalChanged(const QString& kind);

	SubscribeResult subscribe(int eventLimit);
	//! Ends \p subscription of the CURRENT connection; an empty object when it has no such subscription.
	QJsonObject unsubscribe(const QString& subscription);
	//! True when a dispatch is being served for a connection that can receive events.
	bool hasCurrentConnection() const;
	QJsonObject state() const;

	//! The transport's pull: the next line for \p key, ending with the overflow notice, which also ends the
	//! subscription. False when there is nothing to send now.
	bool nextLine(quint64 key, QByteArray* line);
	//! True while \p key has a live subscription; a transport keeps such an idle connection open.
	bool isSubscribed(quint64 key) const { return m_subscribers.contains(key); }
	//! The connection is gone: its queue is freed and the loss counted (`disconnects`).
	void connectionClosed(quint64 key);
	//! \p owner (a server) is closing: forget its connections, whose wake functions hold it.
	void ownerClosed(const void* owner);

	quint64 sequence() const { return m_seq; }
	QString session() const { return m_session; }

private:
	struct Subscriber
	{
		QString id;
		Connection connection;
		ControlEventQueue queue;
		quint64 lastDelivered = 0;
		quint64 delivered = 0;
		bool wakePosted = false;
	};

	ControlEventHub();
	//! The next seq, delivered to every subscriber as one project_changed line.
	void publish(const QString& source, const QString& kind, const QString& command, const QJsonObject& args);
	void onCommandFinished(const QString& commandId, const QJsonObject& args, bool mutating, bool changed,
		bool enclosedByMutating, int depthAfter);

	static ControlEventHub* s_instance;
	QString m_session;
	quint64 m_seq = 0;
	quint64 m_nextSubscription = 1;
	quint64 m_overflows = 0;
	quint64 m_disconnects = 0;
	bool m_journalChangedInDispatch = false;
	bool m_publishedInDispatch = false;
	QMap<quint64, Subscriber> m_subscribers;
};

} // namespace lmms::control

#endif // LMMS_CONTROL_EVENT_HUB_H
