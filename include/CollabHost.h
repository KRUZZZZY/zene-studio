/*
 * CollabHost.h - the hosting end of a peer-to-peer collaboration session:
 *                a TLS-PSK listener, its peers, and the host's broadcast
 *                (0.5.0 F6, WF-050/052)
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

#ifndef LMMS_COLLAB_HOST_H
#define LMMS_COLLAB_HOST_H

#include <functional>
#include <memory>

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>

#include "CollabPolicy.h"
#include "lmms_export.h"

class QSslSocket;
class QSslPreSharedKeyAuthenticator;
class QTcpServer;
class QTimer;

namespace lmms::collab
{

class CollabLink;

//! Most peers one host serves, and most handshakes it lets run at once.
constexpr int MaxPeers = 8;
constexpr int MaxPendingHandshakes = 8;
//! A peer that asks for a fresh snapshot gets at most one per this interval.
constexpr qint64 ResyncIntervalMs = 2000;
//! How long a turned-away joiner has to read the reason and hang up before the host closes.
constexpr int ByeGraceMs = 2000;
//! Peer requests waiting (all peers together) before the one that sends more is dropped.
constexpr int MaxBacklog = 512;

/*! Join attempts per source address: more than MaxAttempts connections, or MaxFailures failed handshakes,
 *  inside WindowMs bans the address for BanMs. A 128-bit key is not guessable, so this is about load, not
 *  about the key: a flood of handshakes costs the host CPU, and a refused address costs one accept. */
class LMMS_EXPORT JoinLimiter
{
public:
	static constexpr int MaxAttempts = 20;
	static constexpr int MaxFailures = 5;
	static constexpr qint64 WindowMs = 60000;
	static constexpr qint64 BanMs = 120000;

	//! Counts an attempt from \a address; false when the address is refused (over a limit, or banned).
	bool admit(const QString& address, qint64 nowMs);
	void recordFailure(const QString& address, qint64 nowMs);

private:
	struct Entry
	{
		qint64 windowStart = 0;
		int attempts = 0;
		int failures = 0;
		qint64 bannedUntil = 0;
	};
	Entry& entryFor(const QString& address, qint64 nowMs);
	QHash<QString, Entry> m_entries;
};

class LMMS_EXPORT CollabHost : public QObject
{
	Q_OBJECT
public:
	//! The project's bytes, or empty with \a error filled.
	using SnapshotProvider = std::function<QByteArray(QString* error)>;
	//! Answers one request line from a peer with one reply line (the control-socket wire shape).
	using RequestHandler = std::function<QByteArray(const QByteArray& line, CollabRole role, const QString& origin)>;

	explicit CollabHost(QObject* parent = nullptr);
	~CollabHost() override;

	//! Nothing listens until this is called. False with \a error when the port cannot be bound.
	bool start(const QHostAddress& address, quint16 port, QString* error);
	void stop();
	bool isListening() const;
	quint16 port() const;

	void setKey(CollabRole role, const QByteArray& key);
	QByteArray key(CollabRole role) const { return m_keys.value(static_cast<int>(role)); }
	void setSnapshotProvider(SnapshotProvider provider) { m_snapshot = std::move(provider); }
	void setRequestHandler(RequestHandler handler) { m_requests = std::move(handler); }

	//! Sends one command the host applied to every project-holding peer, in the host's order.
	void broadcastCommand(const QString& command, const QJsonObject& args, const QString& origin);
	//! Sends a fresh snapshot to every project-holding peer (after an undo, a load, an untracked edit).
	void broadcastSnapshot(const QString& reason);
	//! True when changed. On failure, \a error is empty if \a peerId is absent, or holds the snapshot error if promotion failed.
	bool setPeerRole(int peerId, CollabRole role, QString* error = nullptr);
	QJsonArray peersJson() const;
	int peerCount() const { return static_cast<int>(m_peers.size()); }
	quint64 sequence() const { return m_seq; }

signals:
	void peersChanged();
	void notice(const QString& text);

private:
	struct Peer
	{
		int id = 0;
		QString name;
		CollabRole role = CollabRole::Editor;
		CollabLink* link = nullptr;
		qint64 joinedMs = 0;
		qint64 lastResyncMs = 0;  //!< a peer's own re-sync requests are served at most every ResyncIntervalMs
	};

	void acceptDescriptor(qintptr descriptor);
	void onPsk(QSslSocket* socket, QSslPreSharedKeyAuthenticator* authenticator);
	void onEncrypted(QSslSocket* socket);
	void turnAway(CollabLink* link, const QString& reason);
	void admit(CollabLink* link, CollabRole role, const QByteArray& snapshot);
	void onHandshakeFailed(QSslSocket* socket);
	void onMessage(int peerId, const QJsonObject& message, const QByteArray& line);
	void drainRequests();
	void onPeerClosed(int peerId, const QString& reason);
	void rename(Peer& peer, const QJsonObject& hello);
	void resyncPeer(Peer& peer);
	bool sendSnapshot(Peer& peer, const QByteArray& frame);
	QByteArray snapshotFrame(QString* error);
	void sendToProjectPeers(const QByteArray& line);
	void broadcastPeers();
	static bool holdsProject(const Peer& peer) { return peer.role != CollabRole::Remote; }

	std::unique_ptr<QTcpServer> m_server;
	QHash<int, QByteArray> m_keys;
	QHash<int, Peer> m_peers;
	QSet<QSslSocket*> m_pendingSockets;
	int m_pending = 0;
	int m_nextPeerId = 0;
	quint64 m_seq = 0;
	JoinLimiter m_limiter;
	QList<QPair<int, QByteArray>> m_backlog;  //!< (peer, request line), applied in arrival order
	QTimer* m_drain;
	SnapshotProvider m_snapshot;
	RequestHandler m_requests;
};

} // namespace lmms::collab

#endif // LMMS_COLLAB_HOST_H
