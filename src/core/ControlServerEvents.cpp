/*
 * ControlServerEvents.cpp - the POSIX transport's half of project-change events (Q050-334.1).
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

#include "ControlServer.h"

#include <QMetaObject>

#include "ControlEventHub.h"

namespace lmms
{

// Pull only while no reply tail is waiting: a line the socket would not take stays in Client::pending and the rest
// stay in the hub's bounded queue, so a client that stops reading overflows there and pending never grows for it.
void ControlServer::flushClientEvents(int fd, quint64 connection)
{
#if !defined(Q_OS_UNIX)
	Q_UNUSED(fd);
	Q_UNUSED(connection);
#else
	control::ControlEventHub& hub = control::ControlEventHub::instance();
	while (true)
	{
		// Looked up each turn: a failed send retires the client, and its fd number may be a new client's by now.
		const auto it = m_clients.constFind(fd);
		if (it == m_clients.constEnd() || it->eventConnection != connection || !it->pending.isEmpty()) { return; }
		QByteArray line;
		if (!hub.nextLine(connection, &line)) { return; }
		if (!sendBytes(fd, line + '\n'))
		{
			dropClient(fd);
			return;
		}
	}
#endif
}

void ControlServer::postEventFlush(int fd, quint64 connection)
{
	// Queued: an edit's event follows the reply to the request that made it.
	QMetaObject::invokeMethod(this, [this, fd, connection]() { flushClientEvents(fd, connection); }, Qt::QueuedConnection);
}

} // namespace lmms
