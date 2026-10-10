/*
 * CollabLink.cpp - one TLS-PSK connection of a collaboration session, framed as
 *                  newline-delimited JSON (0.5.0 F6, WF-050/052)
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

#include "CollabLink.h"

#include <QJsonDocument>
#include <QSslCipher>
#include <QSslSocket>
#include <QTimer>

namespace lmms::collab
{

namespace
{

//! Measured with Qt 6.4 + OpenSSL 3.0 (a two-socket spike): both suites negotiate over TLS 1.2 with PSK, and a
//! wrong key fails the handshake with "bad record mac" before any application byte is exchanged.
const char* const PskCiphers[] = {"ECDHE-PSK-CHACHA20-POLY1305", "ECDHE-PSK-AES256-CBC-SHA384"};

bool selectPskBackend()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
	// The active backend can be chosen only before the first TLS object exists; when something already chose
	// another one (an HTTPS fetch at start-up on Windows/macOS), this stays false and the reason says so.
	if (QSslSocket::activeBackend() != QLatin1String("openssl")
		&& QSslSocket::availableBackends().contains(QStringLiteral("openssl")))
	{
		QSslSocket::setActiveBackend(QStringLiteral("openssl"));
	}
	return QSslSocket::isFeatureSupported(QSsl::SupportedFeature::Psk);
#else
	return QSslSocket::supportsSsl();
#endif
}

} // namespace

QSslConfiguration pskConfiguration()
{
	QSslConfiguration config = QSslConfiguration::defaultConfiguration();
	config.setPeerVerifyMode(QSslSocket::VerifyNone);
	config.setProtocol(QSsl::TlsV1_2);
	QList<QSslCipher> ciphers;
	for (const char* name : PskCiphers)
	{
		const QSslCipher cipher(QString::fromLatin1(name));
		if (!cipher.isNull()) { ciphers.append(cipher); }
	}
	config.setCiphers(ciphers);
	return config;
}

QString tlsBackendName()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
	return QSslSocket::activeBackend();
#else
	return QStringLiteral("openssl");
#endif
}

QString tlsPskUnavailableReason()
{
	if (!QSslSocket::supportsSsl())
	{
		return QStringLiteral("unavailable: this system has no TLS library Qt can load (install OpenSSL)");
	}
	if (!selectPskBackend())
	{
		return QStringLiteral("unavailable: the TLS backend in use ('%1') has no pre-shared-key support; collaboration "
			"needs Qt's OpenSSL TLS plug-in").arg(tlsBackendName());
	}
	if (pskConfiguration().ciphers().isEmpty())
	{
		return QStringLiteral("unavailable: the TLS library offers none of the ECDHE-PSK cipher suites collaboration uses");
	}
	return QString();
}

CollabLink::CollabLink(QSslSocket* socket, int maxLineBytes, QObject* parent) :
	QObject(parent),
	m_socket(socket),
	m_maxLineBytes(maxLineBytes)
{
	m_socket->setParent(this);
	m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
	m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
	connect(m_socket, &QIODevice::readyRead, this, &CollabLink::onReadyRead);
	connect(m_socket, &QAbstractSocket::disconnected, this, &CollabLink::onDisconnected);
	// Whatever arrived together with the end of the handshake is read on the next turn of the loop, after the
	// owner has connected to received(). A socket the other side already closed (it said goodbye in the same
	// packet as its Finished) still delivers what it buffered, and then reports the close.
	QTimer::singleShot(0, this, [this] {
		onReadyRead();
		if (m_socket->state() != QAbstractSocket::ConnectedState) { close(QStringLiteral("the connection closed")); }
	});
}

CollabLink::~CollabLink()
{
	m_closed = true;
	m_socket->disconnect(this);
	m_socket->abort();
}

bool CollabLink::isOpen() const
{
	return !m_closed && m_socket->state() == QAbstractSocket::ConnectedState;
}

QString CollabLink::peerAddress() const
{
	return m_socket->peerAddress().toString();
}

void CollabLink::send(const QJsonObject& message)
{
	sendLine(QJsonDocument(message).toJson(QJsonDocument::Compact));
}

void CollabLink::sendLine(const QByteArray& line)
{
	if (!isOpen()) { return; }
	if (m_socket->bytesToWrite() + line.size() + 1 > MaxQueuedBytes)
	{
		close(QStringLiteral("the other side stopped reading"));
		return;
	}
	m_socket->write(line);
	m_socket->write("\n", 1);
}

void CollabLink::close(const QString& reason)
{
	if (m_closed) { return; }
	m_closed = true;
	m_socket->flush();  // a goodbye written just before must leave even if the owner deletes this link next
	m_socket->disconnectFromHost();
	emit closed(reason);
}

void CollabLink::onDisconnected()
{
	close(QStringLiteral("the connection closed"));
}

bool CollabLink::takeLine(QByteArray* line)
{
	const qsizetype end = m_buffer.indexOf('\n');
	if (end < 0) { return false; }
	*line = m_buffer.left(end);
	m_buffer.remove(0, end + 1);
	return true;
}

void CollabLink::onReadyRead()
{
	if (m_closed) { return; }
	m_buffer.append(m_socket->readAll());
	QByteArray line;
	while (!m_closed && takeLine(&line))
	{
		const QJsonDocument document = QJsonDocument::fromJson(line.trimmed());
		emit received(document.isObject() ? document.object() : QJsonObject(), line);
	}
	// A line that never ends is bounded: past the cap the connection is closed rather than buffered.
	if (!m_closed && m_buffer.size() > m_maxLineBytes) { close(QStringLiteral("a message was over the size limit")); }
}

} // namespace lmms::collab
