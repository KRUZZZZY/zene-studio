/*
 * OscServer.cpp - the opt-in OSC control server's UDP listener (Q050-205)
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

#include "OscServer.h"

#include <QCoreApplication>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

namespace lmms::osc
{

namespace
{

OscServer* s_instance = nullptr;

} // namespace

OscServer* OscServer::instance()
{
	return s_instance;
}

OscServer* OscServer::start(const QString& address, int port, Scope scope, QString* error)
{
	QHostAddress host;
	if (!host.setAddress(address))
	{
		*error = QStringLiteral("'%1' is not an IP address (use 127.0.0.1 for this machine)").arg(address);
		return nullptr;
	}
	if (scope == Scope::Full && !host.isLoopback())
	{
		*error = QStringLiteral("the 'full' scope is for this machine only: bind a loopback address, or use the "
			"'remote' scope on %1").arg(address);
		return nullptr;
	}
	stop();
	auto* server = new OscServer(scope, QCoreApplication::instance());
	if (!server->m_socket->bind(host, static_cast<quint16>(port)))
	{
		*error = QStringLiteral("cannot listen on %1:%2: %3").arg(address).arg(port).arg(server->m_socket->errorString());
		delete server;
		return nullptr;
	}
	s_instance = server;
	return server;
}

bool OscServer::stop()
{
	if (s_instance == nullptr) { return false; }
	OscServer* server = s_instance;
	s_instance = nullptr;
	// The port is released now; the object goes at the next event-loop turn, so a stop from inside a request
	// (a nested event loop under drain()) never deletes the server drain() is running on.
	server->m_socket->close();
	server->deleteLater();
	return true;
}

OscServer::OscServer(Scope scope, QObject* parent) :
	QObject(parent),
	m_socket(new QUdpSocket(this)),
	m_scope(scope)
{
	connect(m_socket, &QUdpSocket::readyRead, this, &OscServer::drain);
}

OscServer::~OscServer()
{
	m_socket->close();
}

void OscServer::drain()
{
	m_drainQueued = false;
	for (int handled = 0; handled < MaxDatagramsPerTurn && m_socket->hasPendingDatagrams(); ++handled)
	{
		const qint64 size = m_socket->pendingDatagramSize();
		// An oversized datagram is read with a cap (the rest is discarded by the socket) and refused unparsed.
		const QNetworkDatagram datagram = m_socket->receiveDatagram(MaxPacketBytes + 1);
		++m_received;
		Exchange exchange;
		if (size > MaxPacketBytes)
		{
			++m_oversized;
			exchange = handleRequest(QByteArray(MaxPacketBytes + 1, '\0'), m_scope);
		}
		else { exchange = handleRequest(datagram.data(), m_scope); }
		if (exchange.ok) { ++m_succeeded; }
		else
		{
			++m_errors;
			m_lastError = exchange.errorKind;
		}
		if (!datagram.senderAddress().isNull() && datagram.senderPort() > 0)
		{
			m_socket->writeDatagram(exchange.reply, datagram.senderAddress(), static_cast<quint16>(datagram.senderPort()));
		}
		// A handler may have stopped this server (a nested event loop): its socket is closed then.
		if (s_instance != this) { return; }
	}
	if (m_socket->hasPendingDatagrams() && !m_drainQueued)
	{
		// Leave the rest for the next turn of the event loop rather than starve it.
		m_drainQueued = true;
		QTimer::singleShot(0, this, &OscServer::drain);
	}
}

QJsonObject OscServer::state() const
{
	return {{QStringLiteral("running"), true}, {QStringLiteral("address"), m_socket->localAddress().toString()},
		{QStringLiteral("port"), static_cast<int>(m_socket->localPort())}, {QStringLiteral("scope"), scopeName(m_scope)},
		{QStringLiteral("received"), static_cast<double>(m_received)},
		{QStringLiteral("succeeded"), static_cast<double>(m_succeeded)},
		{QStringLiteral("errors"), static_cast<double>(m_errors)},
		{QStringLiteral("oversized"), static_cast<double>(m_oversized)},
		{QStringLiteral("last_error"), m_lastError}, {QStringLiteral("prefix"), addressPrefix()}};
}

} // namespace lmms::osc
