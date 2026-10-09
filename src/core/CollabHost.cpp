/*
 * CollabHost.cpp - the hosting end of a peer-to-peer collaboration session:
 *                  a TLS-PSK listener, its peers, and the host's broadcast
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

/*! The host is the single authority of a session. A peer's edit arrives as a control-socket request line,
 *  passes the same registry validation a socket client's does (the RequestHandler is ControlServer's own
 *  dispatch, behind the role policy), and when it changes the project the registry's tap hands it back here
 *  to broadcast - to every project-holding peer, its sender included, in the one order the host applied it.
 *
 *  Nothing is read from a connection before its TLS-PSK handshake completes: the CollabLink that reads is
 *  only built on `encrypted`. All of this runs on the UI thread; nothing touches an audio path. */

#include "CollabHost.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QSslPreSharedKeyAuthenticator>
#include <QSslSocket>
#include <QTcpServer>
#include <QThread>
#include <QTimer>

#include "CollabLink.h"

namespace lmms::collab
{

namespace
{

//! A QTcpServer that hands the raw descriptor over, so the host can refuse a banned address before any TLS
//! object exists and can start the server-side handshake itself.
class DescriptorServer : public QTcpServer
{
public:
	std::function<void(qintptr)> onDescriptor;

protected:
	void incomingConnection(qintptr descriptor) override
	{
		if (onDescriptor) { onDescriptor(descriptor); }
	}
};

constexpr char RoleProperty[] = "zeneCollabRole";
//! Set once a pending socket was either promoted to a peer or counted as a failed handshake.
constexpr char DoneProperty[] = "zeneCollabDone";
constexpr int MaxNameLength = 40;

qint64 nowMs()
{
	return QDateTime::currentMSecsSinceEpoch();
}

QString cleanName(const QString& raw, int fallbackId)
{
	QString name;
	for (const QChar c : raw.trimmed().left(MaxNameLength))
	{
		if (c.isPrint()) { name.append(c); }
	}
	return name.isEmpty() ? QStringLiteral("peer %1").arg(fallbackId) : name;
}

QByteArray frameOf(const QJsonObject& message)
{
	return QJsonDocument(message).toJson(QJsonDocument::Compact);
}

} // namespace

JoinLimiter::Entry& JoinLimiter::entryFor(const QString& address, qint64 now)
{
	Entry& entry = m_entries[address];
	if (now - entry.windowStart > WindowMs) { entry.windowStart = now; entry.attempts = 0; entry.failures = 0; }
	return entry;
}

bool JoinLimiter::admit(const QString& address, qint64 now)
{
	Entry& entry = entryFor(address, now);
	if (now < entry.bannedUntil) { return false; }
	if (++entry.attempts > MaxAttempts) { entry.bannedUntil = now + BanMs; return false; }
	return true;
}

void JoinLimiter::recordFailure(const QString& address, qint64 now)
{
	Entry& entry = entryFor(address, now);
	if (++entry.failures >= MaxFailures) { entry.bannedUntil = now + BanMs; }
}

CollabHost::CollabHost(QObject* parent) :
	QObject(parent),
	m_drain(new QTimer(this))
{
	m_drain->setSingleShot(true);
	m_drain->setInterval(50);
	connect(m_drain, &QTimer::timeout, this, &CollabHost::drainRequests);
}

CollabHost::~CollabHost()
{
	stop();
}

bool CollabHost::start(const QHostAddress& address, quint16 port, QString* error)
{
	stop();
	auto server = std::make_unique<DescriptorServer>();
	server->onDescriptor = [this](qintptr descriptor) { acceptDescriptor(descriptor); };
	if (!server->listen(address, port))
	{
		*error = QStringLiteral("could not listen on port %1: %2").arg(port).arg(server->errorString());
		return false;
	}
	m_server = std::move(server);
	return true;
}

void CollabHost::stop()
{
	if (m_server) { m_server->close(); }
	m_server.reset();
	m_backlog.clear();
	const QHash<int, Peer> peers = m_peers;
	m_peers.clear();
	for (const Peer& peer : peers)
	{
		peer.link->send({{QStringLiteral("type"), QStringLiteral("bye")}, {QStringLiteral("reason"), QStringLiteral("the host stopped")}});
		peer.link->disconnect(this);
		peer.link->close(QStringLiteral("the host stopped"));
		peer.link->deleteLater();
	}
	if (!peers.isEmpty()) { emit peersChanged(); }
}

bool CollabHost::isListening() const
{
	return m_server && m_server->isListening();
}

quint16 CollabHost::port() const
{
	return m_server ? m_server->serverPort() : 0;
}

void CollabHost::setKey(CollabRole role, const QByteArray& key)
{
	m_keys.insert(static_cast<int>(role), key);
}

void CollabHost::acceptDescriptor(qintptr descriptor)
{
	auto* socket = new QSslSocket(this);
	if (!socket->setSocketDescriptor(descriptor)) { socket->deleteLater(); return; }
	const QString from = socket->peerAddress().toString();
	const bool full = m_pending >= MaxPendingHandshakes || m_peers.size() >= MaxPeers;
	if (full || !m_limiter.admit(from, nowMs()))
	{
		socket->abort();
		socket->deleteLater();
		return;
	}
	++m_pending;
	socket->setSslConfiguration(pskConfiguration());
	connect(socket, &QSslSocket::preSharedKeyAuthenticationRequired, this,
		[this, socket](QSslPreSharedKeyAuthenticator* authenticator) { onPsk(socket, authenticator); });
	connect(socket, &QSslSocket::encrypted, this, [this, socket] { onEncrypted(socket); });
	connect(socket, &QAbstractSocket::errorOccurred, this, [this, socket] { onHandshakeFailed(socket); });
	QTimer::singleShot(PskHandshakeTimeoutMs, socket, [this, socket] { onHandshakeFailed(socket); });
	socket->startServerEncryption();
}

void CollabHost::onPsk(QSslSocket* socket, QSslPreSharedKeyAuthenticator* authenticator)
{
	// The client's identity is the role it claims; the key it must hold is that role's. A claim the invite
	// does not back fails the handshake, so a role cannot be raised by editing the identity.
	CollabRole role = CollabRole::Viewer;
	if (!roleFromName(QString::fromLatin1(authenticator->identity()), &role)) { return; }
	const QByteArray key = m_keys.value(static_cast<int>(role));
	if (key.isEmpty()) { return; }
	socket->setProperty(RoleProperty, static_cast<int>(role));
	authenticator->setPreSharedKey(key);
}

void CollabHost::onHandshakeFailed(QSslSocket* socket)
{
	// Reached from errorOccurred and from the handshake timer; a socket that already became a peer, or was
	// already counted, is left alone (its CollabLink reports its own close).
	if (socket->isEncrypted() || socket->property(DoneProperty).toBool()) { return; }
	socket->setProperty(DoneProperty, true);
	--m_pending;
	m_limiter.recordFailure(socket->peerAddress().toString(), nowMs());
	socket->disconnect(this);
	socket->abort();
	socket->deleteLater();
}

void CollabHost::onEncrypted(QSslSocket* socket)
{
	socket->setProperty(DoneProperty, true);
	--m_pending;
	socket->disconnect(this);
	const auto role = static_cast<CollabRole>(socket->property(RoleProperty).toInt());
	auto* link = new CollabLink(socket, MaxPeerLineBytes, this);
	QString error;
	const QByteArray snapshot = role != CollabRole::Remote ? snapshotFrame(&error) : QByteArray();
	if (role != CollabRole::Remote && snapshot.isEmpty()) { turnAway(link, error); return; }
	admit(link, role, snapshot);
}

void CollabHost::turnAway(CollabLink* link, const QString& reason)
{
	// Say why, and let the joiner hang up: closing at once can reach a client still finishing its side of the
	// handshake, which then reports a bare "closed" instead of the reason (measured on loopback).
	link->send({{QStringLiteral("type"), QStringLiteral("bye")}, {QStringLiteral("reason"), reason}});
	connect(link, &CollabLink::closed, link, &QObject::deleteLater);
	QTimer::singleShot(ByeGraceMs, link, [link, reason] { link->close(reason); });
}

void CollabHost::admit(CollabLink* link, CollabRole role, const QByteArray& snapshot)
{
	Peer peer;
	peer.id = ++m_nextPeerId;
	peer.name = cleanName(QString(), peer.id);
	peer.role = role;
	peer.joinedMs = nowMs();
	peer.link = link;
	const int id = peer.id;
	connect(link, &CollabLink::received, this,
		[this, id](const QJsonObject& message, const QByteArray& line) { onMessage(id, message, line); });
	connect(link, &CollabLink::closed, this, [this, id](const QString& reason) { onPeerClosed(id, reason); });
	link->send({{QStringLiteral("type"), QStringLiteral("welcome")}, {QStringLiteral("peer"), id},
		{QStringLiteral("role"), roleName(role)}, {QStringLiteral("proto"), 1}});
	if (!snapshot.isEmpty()) { link->sendLine(snapshot); }
	m_peers.insert(id, peer);
	broadcastPeers();
}

void CollabHost::rename(Peer& peer, const QJsonObject& hello)
{
	peer.name = cleanName(hello.value(QStringLiteral("name")).toString(), peer.id);
	broadcastPeers();
}

void CollabHost::resyncPeer(Peer& peer)
{
	if (!holdsProject(peer) || nowMs() - peer.lastResyncMs < ResyncIntervalMs) { return; }
	peer.lastResyncMs = nowMs();
	QString error;
	if (!sendSnapshot(peer, snapshotFrame(&error))) { emit notice(QStringLiteral("could not re-sync %1: %2").arg(peer.name, error)); }
}

void CollabHost::onMessage(int peerId, const QJsonObject& message, const QByteArray& line)
{
	const auto it = m_peers.find(peerId);
	if (it == m_peers.end()) { return; }
	const QString type = message.value(QStringLiteral("type")).toString();
	if (type == QLatin1String("hello")) { rename(*it, message); return; }
	if (type == QLatin1String("resync")) { resyncPeer(*it); return; }
	if (line.trimmed().isEmpty()) { return; }
	// Everything else is a request in the control-socket shape; the handler answers malformed ones itself.
	// Requests wait while a nested event loop runs (a dialog, a handler that waits), so a peer's command never
	// runs inside another command; the per-peer order is kept because one queue holds them all.
	if (m_backlog.size() >= MaxBacklog) { it->link->close(QStringLiteral("too many requests waiting")); return; }
	m_backlog.append({peerId, line});
	drainRequests();
}

void CollabHost::drainRequests()
{
	while (!m_backlog.isEmpty() && QThread::currentThread()->loopLevel() <= 1)
	{
		const auto [peerId, line] = m_backlog.takeFirst();
		auto it = m_peers.find(peerId);
		if (it == m_peers.end()) { continue; }
		const QByteArray reply = m_requests ? m_requests(line, it->role, it->name)
			: QByteArrayLiteral("{\"id\":-1,\"ok\":false,\"error\":{\"kind\":\"refused\",\"message\":\"this host takes no requests\"}}");
		// The handler may have run a command that closed this peer: look it up again.
		it = m_peers.find(peerId);
		if (it != m_peers.end() && !reply.isEmpty()) { it->link->sendLine(reply); }
	}
	if (!m_backlog.isEmpty()) { m_drain->start(); }
}

void CollabHost::onPeerClosed(int peerId, const QString& reason)
{
	const auto it = m_peers.find(peerId);
	if (it == m_peers.end()) { return; }
	const QString name = it->name;
	it->link->deleteLater();
	m_peers.erase(it);
	emit notice(QStringLiteral("%1 left (%2)").arg(name, reason));
	broadcastPeers();
}

QByteArray CollabHost::snapshotFrame(QString* error)
{
	const QByteArray project = m_snapshot ? m_snapshot(error) : QByteArray();
	if (project.isEmpty())
	{
		if (error->isEmpty()) { *error = QStringLiteral("the host could not write its project"); }
		return QByteArray();
	}
	const QByteArray frame = frameOf({{QStringLiteral("type"), QStringLiteral("snapshot")},
		{QStringLiteral("seq"), static_cast<double>(m_seq)}, {QStringLiteral("bytes"), static_cast<int>(project.size())},
		{QStringLiteral("data"), QString::fromLatin1(project.toBase64())}});
	if (frame.size() >= MaxHostLineBytes)
	{
		*error = QStringLiteral("the project is too large to share (%1 MB; the limit is %2 MB)")
			.arg(project.size() / (1024 * 1024)).arg(MaxHostLineBytes / (1024 * 1024) * 3 / 4);
		return QByteArray();
	}
	return frame;
}

bool CollabHost::sendSnapshot(Peer& peer, const QByteArray& frame)
{
	if (frame.isEmpty()) { return false; }
	peer.link->sendLine(frame);
	return true;
}

void CollabHost::broadcastSnapshot(const QString& reason)
{
	QString error;
	const QByteArray frame = snapshotFrame(&error);
	if (frame.isEmpty()) { emit notice(QStringLiteral("could not re-sync the peers: %1").arg(error)); return; }
	sendToProjectPeers(frame);
	emit notice(QStringLiteral("peers re-synced (%1)").arg(reason));
}

void CollabHost::broadcastCommand(const QString& command, const QJsonObject& args, const QString& origin)
{
	++m_seq;
	sendToProjectPeers(frameOf({{QStringLiteral("type"), QStringLiteral("cmd")}, {QStringLiteral("seq"), static_cast<double>(m_seq)},
		{QStringLiteral("cmd"), command}, {QStringLiteral("args"), args}, {QStringLiteral("origin"), origin}}));
}

void CollabHost::sendToProjectPeers(const QByteArray& line)
{
	const QList<int> peerIds = m_peers.keys();
	for (int peerId : peerIds)
	{
		auto it = m_peers.find(peerId);
		if (it != m_peers.end() && holdsProject(*it)) { it->link->sendLine(line); }
	}
}

QJsonArray CollabHost::peersJson() const
{
	QJsonArray peers;
	for (const Peer& peer : m_peers)
	{
		peers.append(QJsonObject{{QStringLiteral("id"), peer.id}, {QStringLiteral("name"), peer.name},
			{QStringLiteral("role"), roleName(peer.role)}, {QStringLiteral("address"), peer.link->peerAddress()},
			{QStringLiteral("connected"), peer.link->isOpen()},
			{QStringLiteral("connected_s"), static_cast<int>((nowMs() - peer.joinedMs) / 1000)}});
	}
	return peers;
}

void CollabHost::broadcastPeers()
{
	sendToProjectPeers(frameOf({{QStringLiteral("type"), QStringLiteral("peers")}, {QStringLiteral("peers"), peersJson()}}));
	emit peersChanged();
}

bool CollabHost::setPeerRole(int peerId, CollabRole role)
{
	const auto it = m_peers.find(peerId);
	if (it == m_peers.end()) { return false; }
	const bool gainsProject = !holdsProject(*it) && role != CollabRole::Remote;
	it->role = role;
	it->link->send({{QStringLiteral("type"), QStringLiteral("role")}, {QStringLiteral("role"), roleName(role)}});
	if (gainsProject)
	{
		QString error;
		if (!sendSnapshot(*it, snapshotFrame(&error))) { emit notice(error); }
	}
	broadcastPeers();
	return true;
}

} // namespace lmms::collab
