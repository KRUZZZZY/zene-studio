/*
 * ControlCommandsOsc.cpp - osc.*: the opt-in OSC control server (Q050-205)
 *                                       reversibility contract (0.5.0 F6)
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

/*! Off by default: nothing listens until osc.start (the Customise menu's OSC Control Server entry runs the
 *  same command). The server maps /zene/<command-id> onto this registry (OscBridge.h); these three commands
 *  only open, close and read it. */

#include "lmmsconfig.h"

#include <QJsonObject>
#include <QString>

#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "OscBridge.h"
#include "OscCommands.h"

#ifdef ZENE_HAVE_COLLAB
#include "OscServer.h"
#endif

namespace lmms
{

using namespace control;

namespace
{

constexpr int DefaultPort = 9000;

QJsonObject stopped()
{
	return {{QStringLiteral("running"), false}, {QStringLiteral("prefix"), osc::addressPrefix()}};
}

#ifdef ZENE_HAVE_COLLAB

ControlResult oscStart(const QJsonObject& args)
{
	osc::Scope scope = osc::Scope::Remote;
	if (!osc::scopeFromName(args.value(QStringLiteral("scope")).toString(QStringLiteral("remote")), &scope))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'scope' is remote or full"));
	}
	QString error;
	osc::OscServer* server = osc::OscServer::start(args.value(QStringLiteral("address")).toString(QStringLiteral("127.0.0.1")),
		args.value(QStringLiteral("port")).toInt(DefaultPort), scope, &error);
	if (server == nullptr) { return ControlResult::failure(ControlErrorKind::Refused, error); }
	QJsonObject out = server->state();
	out.insert(QStringLiteral("available"), true);
	return ControlResult::success(out);
}

ControlResult oscStop(const QJsonObject&)
{
	QJsonObject out = stopped();
	out.insert(QStringLiteral("was_running"), osc::OscServer::stop());
	out.insert(QStringLiteral("available"), true);
	return ControlResult::success(out);
}

ControlResult oscState(const QJsonObject&)
{
	osc::OscServer* server = osc::OscServer::instance();
	QJsonObject out = server != nullptr ? server->state() : stopped();
	out.insert(QStringLiteral("available"), true);
	return ControlResult::success(out);
}

#else // !ZENE_HAVE_COLLAB

const QString& unavailableReason()
{
	static const QString reason = QStringLiteral("unavailable: this build was made without Qt Network (or with "
		"-DWANT_COLLAB=OFF), so it has no OSC server");
	return reason;
}

ControlResult oscStart(const QJsonObject&) { return ControlResult::failure(ControlErrorKind::Refused, unavailableReason()); }
ControlResult oscStop(const QJsonObject&)
{
	QJsonObject out = stopped();
	out.insert(QStringLiteral("was_running"), false);
	out.insert(QStringLiteral("available"), false);
	return ControlResult::success(out);
}
ControlResult oscState(const QJsonObject&)
{
	QJsonObject out = stopped();
	out.insert(QStringLiteral("available"), false);
	out.insert(QStringLiteral("reason"), unavailableReason());
	return ControlResult::success(out);
}

#endif // ZENE_HAVE_COLLAB

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	ControlResult (*handler)(const QJsonObject&))
{
	defineCommandWithSchema(registry, QStringLiteral("osc.") + QLatin1String(verb), description, args, false, handler);
}

} // namespace

void registerOscCommands(ControlRegistry& registry)
{
	add(registry, "start", QStringLiteral("Start the OSC control server (off until this runs): listen for UDP on "
		"'address' (an IP literal, default 127.0.0.1 - this machine only) and 'port' (default 9000; 0 = any free port). "
		"A message to /zene/<command-id> runs that registered command - its arguments one JSON-object string or "
		"string-key/value pairs - and the sender gets /zene/reply (id, result JSON) or /zene/error (id, kind, message). "
		"'scope' remote (default) allows transport, markers, mixer faders and state reads; full allows every command "
		"except scripts, sessions and quitting, on a loopback address only. Replaces a running server. Device state: not "
		"recorded."),
		objectSchema({{QStringLiteral("address"), stringProperty()}, {QStringLiteral("port"), integerProperty(0, 65535)},
			{QStringLiteral("scope"), enumProperty({QStringLiteral("remote"), QStringLiteral("full")})}}), oscStart);
	add(registry, "stop", QStringLiteral("Stop the OSC control server and close its port."), objectSchema({}), oscStop);
	add(registry, "get_state", QStringLiteral("The OSC control server: running or not, its address, port and scope, "
		"and how many requests it received, ran and refused."), objectSchema({}), oscState);
}

} // namespace lmms
