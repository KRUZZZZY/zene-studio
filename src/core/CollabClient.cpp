/*
 * CollabClient.cpp - the joining end of a peer-to-peer collaboration session
 *                    (0.5.0 F6, WF-050/052)
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

#include "CollabClient.h"

#include <QSslPreSharedKeyAuthenticator>
#include <QSslSocket>
#include <QThread>
#include <QTimer>

#include "CollabLink.h"

namespace lmms::collab
{

namespace
{

constexpr int MaxInflight = 256;

//! What a failed connection attempt means to the person who pasted the invite.
QString explainSocketError(QAbstractSocket::SocketError error, const QString& where)
{
	switch (error)
	{
	case QAbstractSocket::ConnectionRefusedError:
		return QStringLiteral("nothing is listening at %1: the host stopped, or its router does not forward the port").arg(where);
	case QAbstractSocket::SslHandshakeFailedError:
		return QStringLiteral("the host refused this invite's key: it was mistyped, or the host made new invites");
	case QAbstractSocket::RemoteHostClosedError:
		return QStringLiteral("the host closed the connection during the handshake (it is full, or this address made "
			"too many attempts - wait two minutes)");
	default:
		return QStringLiteral("could not reach %1: the host's router may not have opened the port. On the same network, "
			"ask for a LAN invite; otherwise the host can forward the port by hand").arg(where);
	}
}

} // namespace

CollabClient::CollabClient(QObject* parent) :
	QObject(parent),
	m_drain(new QTimer(this))
{
	m_drain->setSingleShot(true);
	m_drain->setInterval(50);
	connect(m_drain, &QTimer::timeout, this, &CollabClient::drainBacklog);
}

CollabClient::~CollabClient()
{
	leave();
}

QString CollabClient::stateName() const
{
	switch (m_state)
	{
	case State::Idle: return QStringLiteral("idle");
	case State::Connecting: return QStringLiteral("connecting");
	case State::Joined: return QStringLiteral("joined");
	case State::Failed: return QStringLiteral("failed");
	}
	return QString();
}

void CollabClient::setState(State state, const QString& noticeText)
{
	m_state = state;
	if (!noticeText.isEmpty()) { m_notice = noticeText; emit notice(noticeText); }
	emit stateChanged();
}

void CollabClient::join(const Invite& invite, const QString& name)
{
	leave();
	m_key = invite.key;
	m_role = invite.role;
	m_name = name;
	m_hostLabel = QStringLiteral("%1:%2").arg(invite.address.toString()).arg(invite.port);
	m_socket = new QSslSocket(this);
	m_socket->setSslConfiguration(pskConfiguration());
	connect(m_socket, &QSslSocket::preSharedKeyAuthenticationRequired, this, [this](QSslPreSharedKeyAuthenticator* auth) {
		auth->setIdentity(roleName(m_role).toLatin1());
		auth->setPreSharedKey(m_key);
	});
	connect(m_socket, &QSslSocket::encrypted, this, &CollabClient::onEncrypted);
	connect(m_socket, &QAbstractSocket::errorOccurred, this, &CollabClient::onSocketError);
	QSslSocket* attempt = m_socket;
	QTimer::singleShot(PskHandshakeTimeoutMs * 2, attempt, [this, attempt] {
		if (attempt == m_socket && m_state == State::Connecting)
		{
			dropSocket();
			setState(State::Failed, QStringLiteral("timed out reaching %1: the host's router may not have opened the "
				"port; on the same network ask for a LAN invite").arg(m_hostLabel));
		}
	});
	setState(State::Connecting, QStringLiteral("connecting to %1").arg(m_hostLabel));
	m_socket->connectToHostEncrypted(invite.address.toString(), invite.port);
}

void CollabClient::dropSocket()
{
	if (m_socket == nullptr) { return; }
	m_socket->disconnect(this);
	m_socket->abort();
	m_socket->deleteLater();
	m_socket = nullptr;
}

void CollabClient::leave()
{
	dropSocket();
	if (m_link != nullptr)
	{
		m_link->disconnect(this);
		m_link->close(QStringLiteral("left"));
		m_link->deleteLater();
		m_link = nullptr;
	}
	m_key.fill('\0');
	m_key.clear();
	m_peers = QJsonArray();
	m_inflight.clear();
	m_backlog.clear();
	m_haveSnapshot = false;
	m_peerId = 0;
	if (m_state != State::Idle) { setState(State::Idle, QString()); }
}

void CollabClient::onSocketError()
{
	if (m_socket == nullptr || m_state != State::Connecting) { return; }
	// The host can finish the handshake and close in one packet (a refused join says why, then hangs up): the
	// error then arrives before `encrypted` was handled, and the reason is in the buffered bytes.
	if (m_socket->isEncrypted()) { onEncrypted(); return; }
	const QAbstractSocket::SocketError error = m_socket->error();
	dropSocket();
	setState(State::Failed, explainSocketError(error, m_hostLabel));
}

void CollabClient::onEncrypted()
{
	QSslSocket* socket = m_socket;
	socket->disconnect(this);
	m_socket = nullptr;
	m_link = new CollabLink(socket, MaxHostLineBytes, this);
	connect(m_link, &CollabLink::received, this, &CollabClient::onMessage);
	connect(m_link, &CollabLink::closed, this, &CollabClient::onClosed);
	m_link->send({{QStringLiteral("type"), QStringLiteral("hello")}, {QStringLiteral("name"), m_name}, {QStringLiteral("proto"), 1}});
	setState(State::Joined, QStringLiteral("connected to %1 as %2").arg(m_hostLabel, roleName(m_role)));
}

void CollabClient::onClosed(const QString& reason)
{
	if (m_link != nullptr) { m_link->deleteLater(); m_link = nullptr; }
	m_haveSnapshot = false;
	m_peers = QJsonArray();
	emit peersChanged();
	setState(State::Failed, QStringLiteral("disconnected from %1 (%2); join again with the same invite").arg(m_hostLabel, reason));
}

void CollabClient::onMessage(const QJsonObject& message, const QByteArray&)
{
	m_backlog.append(message);
	drainBacklog();
}

void CollabClient::drainBacklog()
{
	// The main loop is level 1; anything deeper is a nested loop some handler or dialog is running.
	while (!m_backlog.isEmpty() && QThread::currentThread()->loopLevel() <= 1)
	{
		dispatch(m_backlog.takeFirst());
	}
	if (!m_backlog.isEmpty()) { m_drain->start(); }
}

void CollabClient::dispatch(const QJsonObject& message)
{
	using Handler = void (CollabClient::*)(const QJsonObject&);
	static const QHash<QString, Handler> handlers{{QStringLiteral("welcome"), &CollabClient::onWelcome},
		{QStringLiteral("snapshot"), &CollabClient::onSnapshot}, {QStringLiteral("cmd"), &CollabClient::onCommand},
		{QStringLiteral("peers"), &CollabClient::onPeers}, {QStringLiteral("role"), &CollabClient::onRole},
		{QStringLiteral("bye"), &CollabClient::onBye}};
	const Handler handler = handlers.value(message.value(QStringLiteral("type")).toString(), nullptr);
	if (handler != nullptr) { (this->*handler)(message); return; }
	if (message.contains(QStringLiteral("ok"))) { onReply(message); }
}

void CollabClient::onWelcome(const QJsonObject& message)
{
	m_peerId = message.value(QStringLiteral("peer")).toInt();
	roleFromName(message.value(QStringLiteral("role")).toString(), &m_role);
}

void CollabClient::onSnapshot(const QJsonObject& message)
{
	const QByteArray project = QByteArray::fromBase64(message.value(QStringLiteral("data")).toString().toLatin1());
	if (project.isEmpty() || project.size() != message.value(QStringLiteral("bytes")).toInt(-1))
	{
		setState(m_state, QStringLiteral("the host's project arrived damaged; asking again"));
		requestResync(QStringLiteral("damaged snapshot"));
		return;
	}
	QString error;
	if (m_snapshotSink && !m_snapshotSink(project, &error))
	{
		setState(m_state, QStringLiteral("could not open the host's project here: %1").arg(error));
		return;
	}
	m_seq = static_cast<quint64>(message.value(QStringLiteral("seq")).toDouble());
	m_haveSnapshot = true;
	emit snapshotApplied();
}

void CollabClient::onCommand(const QJsonObject& message)
{
	// A command that arrives before the first snapshot has nothing to apply to; the snapshot that follows
	// carries its effect.
	if (!m_haveSnapshot) { return; }
	m_seq = static_cast<quint64>(message.value(QStringLiteral("seq")).toDouble());
	QString error;
	const QString command = message.value(QStringLiteral("cmd")).toString();
	if (m_commandSink && !m_commandSink(command, message.value(QStringLiteral("args")).toObject(), &error))
	{
		setState(m_state, QStringLiteral("'%1' from the host failed here (%2); re-syncing").arg(command, error));
		requestResync(QStringLiteral("a streamed command failed"));
		return;
	}
	emit commandApplied(m_seq);
}

void CollabClient::onPeers(const QJsonObject& message)
{
	m_peers = message.value(QStringLiteral("peers")).toArray();
	emit peersChanged();
}

void CollabClient::onRole(const QJsonObject& message)
{
	if (roleFromName(message.value(QStringLiteral("role")).toString(), &m_role))
	{
		setState(m_state, QStringLiteral("the host made you a %1").arg(roleName(m_role)));
	}
}

void CollabClient::onBye(const QJsonObject& message)
{
	const QString reason = message.value(QStringLiteral("reason")).toString();
	if (m_link != nullptr) { m_link->disconnect(this); m_link->close(reason); m_link->deleteLater(); m_link = nullptr; }
	m_haveSnapshot = false;
	m_peers = QJsonArray();
	emit peersChanged();
	setState(State::Failed, QStringLiteral("the host ended the session: %1").arg(reason));
}

void CollabClient::onReply(const QJsonObject& message)
{
	const int id = message.value(QStringLiteral("id")).toInt();
	const QString command = m_inflight.take(id);
	const bool ok = message.value(QStringLiteral("ok")).toBool();
	if (!ok)
	{
		const QString why = message.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
		setState(m_state, QStringLiteral("the host refused '%1': %2").arg(command, why));
	}
	emit replied(id, ok, message);
}

int CollabClient::request(const QString& command, const QJsonObject& args)
{
	if (m_link == nullptr || !m_link->isOpen()) { return 0; }
	if (m_inflight.size() >= MaxInflight) { m_inflight.clear(); }
	const int id = ++m_nextRequest;
	m_inflight.insert(id, command);
	m_link->send({{QStringLiteral("id"), id}, {QStringLiteral("cmd"), command}, {QStringLiteral("args"), args}});
	return id;
}

void CollabClient::requestResync(const QString& why)
{
	if (m_link == nullptr) { return; }
	m_link->send({{QStringLiteral("type"), QStringLiteral("resync")}, {QStringLiteral("reason"), why}});
}

} // namespace lmms::collab
