/*
 * OscServer.h - the opt-in OSC control server's UDP listener (Q050-205)
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

#ifndef LMMS_OSC_SERVER_H
#define LMMS_OSC_SERVER_H

#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "OscBridge.h"
#include "lmms_export.h"

class QUdpSocket;

namespace lmms::osc
{

/*! The one OSC listener (Q050-205). Nothing listens until start() - osc.start, or the Customise menu's
 *  entry that runs it - so a fresh application, an old configuration and a project load never open a socket.
 *  It lives on the UI thread: datagrams are read there, each request runs through the command registry
 *  there (handlers run on the UI thread anyway) and the reply goes back to the sender. Nothing of it touches
 *  the audio thread. A burst is drained at most MaxDatagramsPerTurn at a time, so a flood cannot starve the
 *  event loop. Built only with Qt Network (ZENE_HAVE_COLLAB). */
class LMMS_EXPORT OscServer : public QObject
{
	Q_OBJECT
public:
	static constexpr int MaxDatagramsPerTurn = 64;

	//! The running server, or nullptr.
	static OscServer* instance();
	/*! Bind @a address:@a port (0 = any free port) with @a scope; replaces a running server. nullptr with
	 *  *error when the address is not an IP literal, the scope is Full on a non-loopback address, or the
	 *  bind fails. */
	static OscServer* start(const QString& address, int port, Scope scope, QString* error);
	//! Close the socket; true when a server was running.
	static bool stop();

	QJsonObject state() const;

private:
	OscServer(Scope scope, QObject* parent);
	~OscServer() override;
	void drain();

	QUdpSocket* m_socket = nullptr;
	Scope m_scope;
	qint64 m_received = 0;
	qint64 m_succeeded = 0;
	qint64 m_errors = 0;
	qint64 m_oversized = 0;
	QString m_lastError;
	bool m_drainQueued = false;
};

} // namespace lmms::osc

#endif // LMMS_OSC_SERVER_H
