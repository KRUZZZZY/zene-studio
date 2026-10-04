/*
 * ControlCommandsCollab.cpp - collab.*: peer-to-peer collaboration and remote
 *                             control without a server (0.5.0 F6, WF-050/052)
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

/*! Off by default. Nothing listens, maps a port or connects until collab.host or collab.join; collab.host
 *  needs `consent: true`, and in a session a person can answer it also asks them. Registered in every build:
 *  without Qt Network the handlers say so ("unavailable: ...") instead of the ids disappearing. */

#include "lmmsconfig.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "CollabCommands.h"
#include "CollabPolicy.h"
#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "UnattendedRun.h"

#ifdef ZENE_HAVE_COLLAB
#include "CollabPanel.h"
#include "CollabSession.h"
#include "GuiApplication.h"
#endif

namespace lmms
{

using namespace control;

namespace
{

QJsonObject roleProperty()
{
	return enumProperty({QStringLiteral("editor"), QStringLiteral("viewer"), QStringLiteral("remote")});
}

#ifdef ZENE_HAVE_COLLAB

collab::CollabRole roleArg(const QJsonObject& args)
{
	collab::CollabRole role = collab::CollabRole::Editor;
	collab::roleFromName(args.value(QStringLiteral("role")).toString(QStringLiteral("editor")), &role);
	return role;
}

//! True when a person can answer a dialog here; such a person must say yes too.
bool personPresent()
{
	return !isUnattendedRun() && gui::getGUI() != nullptr;
}

ControlResult collabHost(const QJsonObject& args)
{
	if (!args.value(QStringLiteral("consent")).toBool(false))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("hosting opens a network port that peers with "
			"an invite can reach; pass consent: true to confirm"));
	}
	const quint16 port = static_cast<quint16>(args.value(QStringLiteral("port")).toInt(collab::CollabSession::DefaultPort));
	const bool mapPort = args.value(QStringLiteral("map_port")).toBool(true);
	const QString question = QStringLiteral("Host a collaboration session?\n\nThis opens TCP port %1 on this computer%2. "
		"Anyone you give an invite to can edit this project (or, with a remote invite, control playback and faders) until "
		"you stop hosting. An invite carries the session key: share it like a password.\n\nThe connection is encrypted with "
		"the invite's key. Peers cannot run scripts, open files or change settings on this computer.")
		.arg(port).arg(mapPort ? QStringLiteral(" and asks your router to open it to the internet") : QString());
	if (personPresent() && !gui::collabConsentPrompt(QStringLiteral("Collaborate"), question))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("hosting was declined"));
	}
	return collab::CollabSession::instance()->host(port, mapPort, args.value(QStringLiteral("name")).toString());
}

ControlResult collabJoin(const QJsonObject& args)
{
	const QString question = QStringLiteral("Join a collaboration session?\n\nThe host's project replaces the one open here "
		"(save it first if you want to keep it), and edits stay in step with the host until you leave.");
	if (personPresent() && !gui::collabConsentPrompt(QStringLiteral("Collaborate"), question))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("joining was declined"));
	}
	return collab::CollabSession::instance()->join(args.value(QStringLiteral("invite")).toString(),
		args.value(QStringLiteral("name")).toString());
}

ControlResult collabStop(const QJsonObject&) { return collab::CollabSession::instance()->stop(); }
ControlResult collabLeave(const QJsonObject&) { return collab::CollabSession::instance()->leave(); }
ControlResult collabResync(const QJsonObject&) { return collab::CollabSession::instance()->resync(); }
ControlResult collabState(const QJsonObject&) { return ControlResult::success(collab::CollabSession::instance()->state()); }
ControlResult collabPanel(const QJsonObject&) { return gui::showCollabPanel(); }

ControlResult collabInvite(const QJsonObject& args)
{
	return collab::CollabSession::instance()->invite(roleArg(args), args.value(QStringLiteral("scope")).toString(QStringLiteral("auto")));
}

ControlResult collabSetRole(const QJsonObject& args)
{
	return collab::CollabSession::instance()->setRole(args.value(QStringLiteral("peer")).toInt(), roleArg(args));
}

#else // !ZENE_HAVE_COLLAB

const QString& unavailableReason()
{
	static const QString reason = QStringLiteral("unavailable: this build was made without collaboration (Qt Network was "
		"not found, or -DWANT_COLLAB=OFF)");
	return reason;
}

ControlResult unavailable(const QJsonObject&) { return ControlResult::failure(ControlErrorKind::Refused, unavailableReason()); }
ControlResult collabState(const QJsonObject&)
{
	return ControlResult::success({{QStringLiteral("available"), false}, {QStringLiteral("mode"), QStringLiteral("idle")},
		{QStringLiteral("reason"), unavailableReason()}});
}
using Handler = ControlResult (*)(const QJsonObject&);
const Handler collabHost = unavailable;
const Handler collabJoin = unavailable;
const Handler collabStop = unavailable;
const Handler collabLeave = unavailable;
const Handler collabResync = unavailable;
const Handler collabPanel = unavailable;
const Handler collabInvite = unavailable;
const Handler collabSetRole = unavailable;

#endif // ZENE_HAVE_COLLAB

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	ControlResult (*handler)(const QJsonObject&), bool requiresEngine = true)
{
	defineCommandWithSchema(registry, QStringLiteral("collab.") + QLatin1String(verb), description, args, false, handler, requiresEngine);
}

} // namespace

void registerCollabCommands(ControlRegistry& registry)
{
	// Session state, never project state: each is not_mutating (ControlReversibilityTableCollab.cpp), which also keeps
	// an invite - it carries the session key - out of the transaction log and the workflow recorder.
	add(registry, "host", QStringLiteral("Host a peer-to-peer collaboration session (no server): listen on TCP 'port' "
		"(default 41740; any free port when that one is taken) with TLS-PSK encryption, and with 'map_port' (default true) ask "
		"the router to open it (PCP/NAT-PMP, then UPnP). Needs 'consent': true; a person at the screen is asked as well. "
		"'name' is how peers see you. The router's answer arrives in collab.get_state's host.mapping."),
		objectSchema({{QStringLiteral("consent"), booleanProperty()}, {QStringLiteral("port"), integerProperty(1024, 65535)},
			{QStringLiteral("map_port"), booleanProperty()}, {QStringLiteral("name"), stringProperty()}},
			QJsonArray{QStringLiteral("consent")}), collabHost);
	add(registry, "stop", QStringLiteral("Stop hosting: every peer is disconnected and the router mapping removed."),
		objectSchema({}), collabStop);
	add(registry, "invite", QStringLiteral("The invite for a 'role' (editor - default; viewer; remote: transport, markers "
		"and faders only) as one copyable string carrying the address, port and that role's 128-bit key. 'scope': auto "
		"(the router's public address when the mapping worked, else the LAN address), public, lan, or local (127.0.0.1). "
		"The invite is a secret: whoever holds it can join as that role."),
		objectSchema({{QStringLiteral("role"), roleProperty()}, {QStringLiteral("scope"), enumProperty({QStringLiteral("auto"),
			QStringLiteral("public"), QStringLiteral("lan"), QStringLiteral("local")})}}), collabInvite);
	add(registry, "join", QStringLiteral("Join a session with an 'invite' (ZENE1-...). The host's project replaces the open "
		"one, then every edit made through a command stays in step: yours go to the host, which applies and streams them to "
		"everyone in its order. 'name' is how others see you. Asynchronous: follow collab.get_state's peer.state."),
		objectSchema({{QStringLiteral("invite"), stringProperty()}, {QStringLiteral("name"), stringProperty()}},
			QJsonArray{QStringLiteral("invite")}), collabJoin);
	add(registry, "leave", QStringLiteral("Leave the session; the project stays open here as it was."), objectSchema({}), collabLeave);
	add(registry, "get_state", QStringLiteral("The session: mode (idle, hosting, joined), the peers with their roles and "
		"connection, the port and the router mapping (method, external address, whether the internet can reach it), the "
		"last notice and the undo model. Never includes an invite or key."), objectSchema({}), collabState, false);
	add(registry, "set_role", QStringLiteral("Host only: change connected 'peer' (its id) to 'role' (editor, viewer, "
		"remote)."), objectSchema({{QStringLiteral("peer"), integerProperty(1, 1 << 30)}, {QStringLiteral("role"), roleProperty()}},
		QJsonArray{QStringLiteral("peer"), QStringLiteral("role")}), collabSetRole);
	add(registry, "resync", QStringLiteral("Send the host's project again: a host re-sends it to every peer, a peer asks the "
		"host for it."), objectSchema({}), collabResync);
	ControlCommand panel;
	panel.id = QStringLiteral("collab.show_panel");
	panel.group = QStringLiteral("collab");
	panel.verb = QStringLiteral("show_panel");
	panel.description = QStringLiteral("Show the collaboration panel (host, invites, join, who is connected).");
	panel.requiresDecl = QStringList{QStringLiteral("display")};
	panel.argsSchema = objectSchema({});
	panel.resultSchema = objectProperty();
	panel.requiresEngine = false;
	panel.handler = collabPanel;
	registry.registerCommand(panel);
}

} // namespace lmms
