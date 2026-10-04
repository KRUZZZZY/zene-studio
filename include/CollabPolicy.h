/*
 * CollabPolicy.h - which commands a collaboration peer may send, which run
 *                  locally, and which the host streams (0.5.0 F6, WF-050/052)
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

#ifndef LMMS_COLLAB_POLICY_H
#define LMMS_COLLAB_POLICY_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms
{
struct ControlCommand;
}

namespace lmms::collab
{

/*! What a connected peer is allowed to do. The role is bound to the pre-shared key: each role has its own
 *  key and the invite carries one, so a peer cannot claim a role its invite does not grant. */
enum class CollabRole
{
	Editor,  //!< receives the project and the command stream, sends edits
	Viewer,  //!< receives the project and the stream, sends nothing that changes it
	Remote,  //!< no project; the transport-and-faders allow-list only (WF-052)
};

LMMS_EXPORT QString roleName(CollabRole role);
//! False (and \a role untouched) for a name that is not a role.
LMMS_EXPORT bool roleFromName(const QString& name, CollabRole* role);

//! Where a command invoked on a JOINED peer goes.
enum class PeerRoute
{
	Local,    //!< runs here: a read, a machine preference, the session's own commands
	Forward,  //!< sent to the host, which applies it and streams it back to everyone
	Refuse,   //!< refused here, typed, with the reason
};

struct RouteDecision
{
	PeerRoute route = PeerRoute::Local;
	QString reason;  //!< why, for a refusal
};

LMMS_EXPORT RouteDecision peerRoute(const QString& id, bool mutating, const QJsonObject& args, CollabRole role);

//! What the host does after a top-level command finished.
enum class HostBroadcast
{
	None,    //!< nothing to share (a read, a preference, a no-op)
	Stream,  //!< send the command and its arguments to every project-holding peer
	Resync,  //!< the change cannot be replayed (undo, a load, a file) - send a fresh snapshot
};

LMMS_EXPORT HostBroadcast hostBroadcast(const QString& id, bool mutating, bool changed, const QJsonObject& args);

/*! Empty when the host lets a peer of \a role run \a id, else the refusal. \a command is the registered
 *  command (nullptr: unknown, which the dispatcher answers not_found itself). */
LMMS_EXPORT QString hostRefusal(const QString& id, const ControlCommand* command, const QJsonObject& args, CollabRole role);

//! True when an argument names a file, folder or URL: such a command reads or writes one machine's disk.
LMMS_EXPORT bool hasPathLikeArgument(const QJsonObject& args);

//! The commands a Remote-role peer may run (WF-052): transport, markers, mixer faders and state polling.
LMMS_EXPORT QStringList remoteAllowList();

} // namespace lmms::collab

#endif // LMMS_COLLAB_POLICY_H
