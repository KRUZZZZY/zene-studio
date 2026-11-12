/*
 * OscBridge.h - OSC 1.0 messages to and from registered commands (Q050-205)
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

#ifndef LMMS_OSC_BRIDGE_H
#define LMMS_OSC_BRIDGE_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "lmms_export.h"

/*! The OSC half of the opt-in OSC control server (Q050-205): the wire codec and the mapping of one
 *  request datagram onto ControlRegistry::invoke(). No socket here - OscServer (Qt Network) does the I/O -
 *  so the whole mapping is testable without a network and builds where Qt Network is absent.
 *
 *  Address map (docs/050-features/Q050-205.md, the manual's "OSC control" section):
 *    request  /zene/<command-id>   e.g. /zene/transport.play, /zene/mixer.set_volume
 *      arguments: none; or ONE string holding the JSON object of the command's arguments; or
 *      key/value pairs - a string key followed by its value (i, f, s, T, F or N) - e.g.
 *      ,sisf "channel" 1 "volume" 0.5
 *    reply    /zene/reply  ,ss <command-id> <the result as compact JSON>
 *    error    /zene/error  ,sss <command-id or address> <error kind> <message>
 *  Everything is bounded: a request over MaxPacketBytes, a bundle, a blob or a malformed packet is an error
 *  reply; a reply that would exceed MaxReplyBytes is replaced by a too_large error; messages are capped. */
namespace lmms::osc
{

constexpr int MaxPacketBytes = 8192;     //!< a larger request datagram is refused unread
constexpr int MaxReplyBytes = 16384;     //!< a larger reply is replaced by a too_large error
constexpr int MaxMessageChars = 512;     //!< an error message is cut to this
constexpr int MaxArguments = 64;         //!< more OSC arguments than this is malformed

//! The address prefix every request carries.
LMMS_EXPORT QString addressPrefix();

//! What a server lets a request run.
enum class Scope
{
	Remote,  //!< the transport-and-faders allow-list (CollabPolicy::remoteAllowList) - the default
	Full,    //!< every registered command except the closed set fullScopeRefusal() names; loopback only
};

LMMS_EXPORT QString scopeName(Scope scope);
LMMS_EXPORT bool scopeFromName(const QString& name, Scope* scope);

//! One decoded OSC message: its address, its type tags (without the leading comma) and its arguments.
struct Message
{
	QString address;
	QString typeTags;
	QJsonArray args;
};

//! Decode one OSC 1.0 message. False with *error for a bundle, a blob, an unknown tag or a malformed packet.
LMMS_EXPORT bool decode(const QByteArray& packet, Message* out, QString* error);

//! Encode @a address with @a args: integral numbers as i, other numbers as f, strings s, booleans T/F, null N.
LMMS_EXPORT QByteArray encode(const QString& address, const QJsonArray& args);

//! The command arguments a request's OSC arguments stand for (the three forms above).
LMMS_EXPORT bool argumentsToJson(const QJsonArray& args, QJsonObject* out, QString* error);

//! Why @a id may not run over OSC in @a scope, or an empty string when it may.
LMMS_EXPORT QString scopeRefusal(const QString& id, Scope scope);

//! The outcome of one request: the reply datagram and whether the command ran and succeeded.
struct Exchange
{
	QByteArray reply;
	bool ok = false;
	QString command;   //!< the command id the address named, empty when it named none
	QString errorKind; //!< empty on success
};

//! Handle one request datagram on the UI thread: decode, check the scope, invoke the registered command,
//! encode its reply. Never throws and never returns an empty reply.
LMMS_EXPORT Exchange handleRequest(const QByteArray& packet, Scope scope);

} // namespace lmms::osc

#endif // LMMS_OSC_BRIDGE_H
