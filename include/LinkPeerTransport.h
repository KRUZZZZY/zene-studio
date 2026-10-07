/*
 * LinkPeerTransport.h - the seam between the session-sync model and the way its
 *                      announcements travel (docs/LINK-SYNC.md section 3).
 *
 * The zene-link-style model (include/LinkSync.h) knows packets, revisions and a
 * shared timeline; this interface is how its announcements travel (UDP multicast
 * today). It was drawn as the place a real Ableton Link transport would plug in.
 * When Link was vendored (WF-051) it went in one level up instead, as a whole
 * backend (include/AbletonLinkBridge.h): Link owns the timeline, the peer set and
 * the clock synchronisation, not just the wire, so it replaces the model rather
 * than this seam - docs/LINK-SYNC.md section 3.
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

	/*! What this host MEASURED about the half a socket option cannot promise:
	 *  whether a datagram sent to the session's group is actually RECEIVED by
	 *  another socket on this host that joined it.
	 *
	 *  Why a measurement and not a flag. A socket that binds the port and joins
	 *  the group can still be on a host that never delivers a multicast datagram
	 *  anywhere - in which case two instances on one box can never see each
	 *  other, and `available()` returning true is a claim the host does not
	 *  honour. That is a question about the PLATFORM, so the transport answers it
	 *  by measuring (docs/LINK-SYNC.md section 3) and reports the measurement
	 *  here, so a client can check the claim rather than believe it.
	 *
	 *  `attempted == false` is the answer for a transport that does not probe
	 *  (the unavailable Windows stub), never a silent "it worked".
	 */
	struct LoopbackProbe
	{
		bool attempted = false;
		bool delivered = false;
		int elapsedMs = -1;   //!< -1 when nothing arrived inside the bound
		int boundMs = 0;
	};

	virtual ~LinkPeerTransport() = default;

	/*! Open the socket and join the group, then MEASURE that announcements can
	 *  be received on this host. False means announcements cannot travel - to a
	 *  peer, or in the case of a failed loopback probe, not even to a second
	 *  socket in this host - and `reason()` then says which of the two it was
	 *  and what was measured, so the model reports it verbatim instead of
	 *  pretending a silent session is a working one. */
	virtual bool start() = 0;
	virtual void stop() = 0;
	virtual bool available() const = 0;
	virtual QString reason() const = 0;
	//! "group:port", for a reader who has to know where to look.
	virtual QString endpoint() const = 0;
	//! Send one announcement. Best-effort: a datagram is not a guarantee.
	virtual void send(const QByteArray& payload) = 0;
	virtual void setReceiver(Receiver receiver) = 0;
	//! The loopback measurement `available()` is derived from. Not pure: a
	//! transport that cannot answer says so with `attempted == false` rather
	//! than having to invent a probe.
	virtual LoopbackProbe loopbackProbe() const { return LoopbackProbe{}; }
};

} // namespace lmms

#endif // LMMS_LINK_PEER_TRANSPORT_H
