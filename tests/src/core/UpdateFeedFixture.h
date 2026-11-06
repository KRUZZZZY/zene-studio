/*
 * UpdateFeedFixture.h - a release feed on loopback, for the update-check tests
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

#ifndef LMMS_TESTS_UPDATE_FEED_FIXTURE_H
#define LMMS_TESTS_UPDATE_FEED_FIXTURE_H

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

namespace updatetest
{

//! One HTTP server on 127.0.0.1 that counts what it is asked and answers one set reply, or never when `silent`.
class FeedServer : public QObject
{
public:
	FeedServer()
	{
		m_server.listen(QHostAddress::LocalHost, 0);
		connect(&m_server, &QTcpServer::newConnection, this, [this] {
			while (QTcpSocket* socket = m_server.nextPendingConnection())
			{
				socket->setParent(this);
				connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onRead(socket); });
			}
		});
	}

	QString url() const { return QStringLiteral("http://127.0.0.1:%1/releases").arg(m_server.serverPort()); }

	void answer(const QByteArray& body, int status = 200)
	{
		silent = false;
		m_status = status;
		m_body = body;
	}

	bool silent = false;
	QList<QByteArray> requests;  //!< each request's head, verbatim

private:
	void onRead(QTcpSocket* socket)
	{
		m_buffer += socket->readAll();
		const auto end = m_buffer.indexOf("\r\n\r\n");
		if (end < 0) { return; }
		requests.append(m_buffer.left(end));
		m_buffer.clear();
		if (silent) { return; }
		socket->write("HTTP/1.1 " + QByteArray::number(m_status) + " Status\r\nContent-Type: application/json\r\nContent-Length: "
			+ QByteArray::number(m_body.size()) + "\r\nConnection: close\r\n\r\n" + m_body);
		socket->disconnectFromHost();
	}

	QTcpServer m_server;
	QByteArray m_buffer;
	QByteArray m_body;
	int m_status = 200;
};

//! A GitHub release as the feed lists it.
inline QByteArray release(const char* tag, bool prerelease = false, const char* page = "https://example.invalid/page")
{
	return QByteArray("{\"tag_name\":\"") + tag + "\",\"name\":\"Release " + tag + "\",\"body\":\"Notes of " + tag
		+ "\",\"html_url\":\"" + page + "\",\"draft\":false,\"prerelease\":" + (prerelease ? "true" : "false") + "}";
}

} // namespace updatetest

#endif // LMMS_TESTS_UPDATE_FEED_FIXTURE_H
