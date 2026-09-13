/*
 * LinkPeerTransport.h - the seam between the session-sync model and the way its
 *                      announcements travel (docs/LINK-SYNC.md section 3).
 *
 * The model (include/LinkSync.h) knows packets, revisions and a shared timeline.
 * It does not know whether they leave the process over UDP multicast, over the
 * Ableton Link library, or not at all. That is this interface, and it is the
 * whole reason the design is shaped this way: real Link interoperability is a
 * licence-permitted follow-up (Link's LICENSE.md is GPL-2.0-or-later - see
 * docs/LINK-SYNC.md section 1), so the follow-up must be able to ADD a transport
 * beside this one rather than rewrite the model.
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

#ifndef LMMS_LINK_PEER_TRANSPORT_H
#define LMMS_LINK_PEER_TRANSPORT_H

#include <functional>

#include <QByteArray>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

/*! How announcements leave and enter this process.
 *
 *  Every call is made on the UI thread. `setReceiver`'s callback is invoked on
 *  the UI thread too, from the socket notifier - a transport that has to read
 *  from another thread must marshal before calling it, because the model it
 *  feeds is not thread-safe and is not meant to be.
 */
class LMMS_EXPORT LinkPeerTransport
{
public:
	//! The payload of one announcement, plus this process's monotone clock in
	//! microseconds at the moment it was read.
	using Receiver = std::function<void(const QByteArray&, quint64)>;

	virtual ~LinkPeerTransport() = default;

	/*! Open the socket and join the group. False means announcements cannot
	 *  travel; `reason()` then says why and the model reports it verbatim
	 *  instead of pretending a silent session is a working one. */
	virtual bool start() = 0;
	virtual void stop() = 0;
	virtual bool available() const = 0;
	virtual QString reason() const = 0;
	//! "group:port", for a reader who has to know where to look.
	virtual QString endpoint() const = 0;
	//! Send one announcement. Best-effort: a datagram is not a guarantee.
	virtual void send(const QByteArray& payload) = 0;
	virtual void setReceiver(Receiver receiver) = 0;
};

} // namespace lmms

#endif // LMMS_LINK_PEER_TRANSPORT_H
