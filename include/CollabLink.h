/*
 * CollabLink.h - one TLS-PSK connection of a collaboration session, framed as
 *                newline-delimited JSON (0.5.0 F6, WF-050/052)
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

#ifndef LMMS_COLLAB_LINK_H
#define LMMS_COLLAB_LINK_H

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QSslConfiguration>
#include <QString>

#include "lmms_export.h"

class QSslSocket;

namespace lmms::collab
{

//! Largest line a peer may send the host (a command request) - the control socket's own request cap.
constexpr int MaxPeerLineBytes = 1024 * 1024;
//! Largest line the host may send a peer: the project snapshot travels base64-encoded in one line.
constexpr int MaxHostLineBytes = 64 * 1024 * 1024;
//! Bytes queued for one slow reader before it is dropped, so a peer that stops reading cannot grow memory.
constexpr qint64 MaxQueuedBytes = 160 * 1024 * 1024;
//! A connection whose TLS-PSK handshake has not completed by then is dropped (and counted against its address).
constexpr int PskHandshakeTimeoutMs = 8000;

/*! The TLS settings both ends use: TLS 1.2 exactly, and only ECDHE-PSK cipher suites, so the session keys are
 *  forward secret and no certificate is involved. The pre-shared key comes from the invite. */
LMMS_EXPORT QSslConfiguration pskConfiguration();

/*! Empty when this process can speak TLS-PSK, else why it cannot. Selects Qt's OpenSSL TLS plug-in where the
 *  platform default is another one (Schannel and Secure Transport have no PSK support). */
LMMS_EXPORT QString tlsPskUnavailableReason();
LMMS_EXPORT QString tlsBackendName();

/*! One encrypted connection after its handshake: reads complete lines (bounded by \a maxLineBytes, over which
 *  the connection is closed), parses each as a JSON object and writes JSON lines. Built only once the socket
 *  reports `encrypted`, so nothing that arrives before the handshake completes is ever read. */
class LMMS_EXPORT CollabLink : public QObject
{
	Q_OBJECT
public:
	//! Takes ownership of \a socket.
	CollabLink(QSslSocket* socket, int maxLineBytes, QObject* parent = nullptr);
	~CollabLink() override;

	void send(const QJsonObject& message);
	//! \a line must not contain a newline; one is appended.
	void sendLine(const QByteArray& line);
	void close(const QString& reason);
	bool isOpen() const;
	QString peerAddress() const;

signals:
	//! \a message is empty when \a line is not a JSON object; the receiver decides what that means.
	void received(const QJsonObject& message, const QByteArray& line);
	void closed(const QString& reason);

private:
	void onReadyRead();
	void onDisconnected();
	bool takeLine(QByteArray* line);

	QSslSocket* m_socket;
	QByteArray m_buffer;
	int m_maxLineBytes;
	bool m_closed = false;
};

} // namespace lmms::collab

#endif // LMMS_COLLAB_LINK_H
