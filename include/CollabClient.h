/*
 * CollabClient.h - the joining end of a peer-to-peer collaboration session
 *                  (0.5.0 F6, WF-050/052)
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

#ifndef LMMS_COLLAB_CLIENT_H
#define LMMS_COLLAB_CLIENT_H

#include <functional>

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "CollabInvite.h"
#include "CollabPolicy.h"
#include "lmms_export.h"

class QSslSocket;
class QTimer;

namespace lmms::collab
{

class CollabLink;

class LMMS_EXPORT CollabClient : public QObject
{
	Q_OBJECT
public:
	//! Replaces the local project with the host's bytes; false with \a error when it could not.
	using SnapshotSink = std::function<bool(const QByteArray& project, QString* error)>;
	//! Applies one command the host streamed; false with \a error when it failed here.
	using CommandSink = std::function<bool(const QString& command, const QJsonObject& args, QString* error)>;

	enum class State
	{
		Idle,
		Connecting,  //!< TCP + TLS-PSK handshake in flight
		Joined,      //!< handshake done; the snapshot, then the stream
		Failed,      //!< the last attempt failed; lastNotice() says why
	};

	explicit CollabClient(QObject* parent = nullptr);
	~CollabClient() override;

	void join(const Invite& invite, const QString& name);
	void leave();

	State state() const { return m_state; }
	QString stateName() const;
	CollabRole role() const { return m_role; }
	int peerId() const { return m_peerId; }
	QJsonArray peers() const { return m_peers; }
	quint64 sequence() const { return m_seq; }
	bool hasSnapshot() const { return m_haveSnapshot; }
	QString lastNotice() const { return m_notice; }
	QString hostAddress() const { return m_hostLabel; }

	void setSnapshotSink(SnapshotSink sink) { m_snapshotSink = std::move(sink); }
	void setCommandSink(CommandSink sink) { m_commandSink = std::move(sink); }

	//! Sends a request to the host (the control-socket shape); returns its id, 0 when not joined.
	int request(const QString& command, const QJsonObject& args);
	//! Asks the host for a fresh snapshot (after this copy changed outside the stream, or failed a command).
	void requestResync(const QString& why);

signals:
	void stateChanged();
	void peersChanged();
	void notice(const QString& text);
	void snapshotApplied();
	void commandApplied(quint64 seq);
	//! The host's reply to a request() of ours.
	void replied(int id, bool ok, const QJsonObject& reply);

private:
	void onEncrypted();
	void onSocketError();
	void onMessage(const QJsonObject& message, const QByteArray& line);
	void drainBacklog();
	void dispatch(const QJsonObject& message);
	void onClosed(const QString& reason);
	void onWelcome(const QJsonObject& message);
	void onSnapshot(const QJsonObject& message);
	void onCommand(const QJsonObject& message);
	void onPeers(const QJsonObject& message);
	void onRole(const QJsonObject& message);
	void onBye(const QJsonObject& message);
	void onReply(const QJsonObject& message);
	void setState(State state, const QString& noticeText);
	void dropSocket();

	State m_state = State::Idle;
	QSslSocket* m_socket = nullptr;  //!< while connecting
	CollabLink* m_link = nullptr;    //!< once encrypted
	QByteArray m_key;
	CollabRole m_role = CollabRole::Editor;
	QString m_name;
	QString m_hostLabel;
	int m_peerId = 0;
	int m_nextRequest = 0;
	quint64 m_seq = 0;
	bool m_haveSnapshot = false;
	QJsonArray m_peers;
	QHash<int, QString> m_inflight;  //!< request id -> command, bounded
	QString m_notice;
	//! Frames that arrived while a nested event loop was running (a modal dialog, or a handler that waits):
	//! applying a command inside another one would merge its undo step into that one's, so they wait, in order.
	QList<QJsonObject> m_backlog;
	QTimer* m_drain;
	SnapshotSink m_snapshotSink;
	CommandSink m_commandSink;
};

} // namespace lmms::collab

#endif // LMMS_COLLAB_CLIENT_H
