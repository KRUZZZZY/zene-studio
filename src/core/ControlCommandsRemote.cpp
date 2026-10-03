/*
 * ControlCommandsRemote.cpp - remote.*: remote-control pages for a MIDI controller's eight knobs
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
/*! 0.5.0 L10 (MX-106): remote-control pages on the spine (RemoteControls.h). remote.connect binds a
 *  controller's eight knobs; remote.target names the device or turns following the selection on;
 *  remote.page moves through its parameters eight at a time; remote.state answers what the knobs
 *  drive now. Session state, not project state: nothing is journalled. */

#include "ControlDeviceSupport.h"
#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "RemoteControls.h"

namespace lmms
{

using namespace control;

namespace
{

ControlResult notConnected()
{
	return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no remote controls are connected (remote.connect)"));
}

ControlResult remoteConnect(const QJsonObject& args)
{
	RemoteControls* remote = RemoteControls::connect(args.value(QStringLiteral("input")).toString(),
		args.value(QStringLiteral("output")).toString(), args.value(QStringLiteral("channel")).toInt(-1),
		args.value(QStringLiteral("first_cc")).toInt(16));
	return ControlResult::success(remote->state());
}

ControlResult remoteDisconnect(const QJsonObject&)
{
	const bool was = RemoteControls::instance() != nullptr;
	RemoteControls::disconnect();
	return ControlResult::success({{QStringLiteral("was_connected"), was}});
}

ControlResult remoteTarget(const QJsonObject& args)
{
	RemoteControls* remote = RemoteControls::instance();
	if (remote == nullptr) { return notConnected(); }
	if (args.contains(QStringLiteral("follow"))) { remote->setFollow(args.value(QStringLiteral("follow")).toBool()); }
	if (args.contains(QStringLiteral("target")))
	{
		ControlTarget target;
		ControlResult error;
		if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), &target, &error)) { return error; }
		remote->setTarget(target.id, args.value(QStringLiteral("plugin")).toString(QStringLiteral("inst")));
	}
	return ControlResult::success(remote->state());
}

ControlResult remotePage(const QJsonObject& args)
{
	RemoteControls* remote = RemoteControls::instance();
	if (remote == nullptr) { return notConnected(); }
	const int page = remote->state().value(QStringLiteral("page")).toInt();
	remote->setPage(args.contains(QStringLiteral("page")) ? args.value(QStringLiteral("page")).toInt()
		: page + args.value(QStringLiteral("delta")).toInt(1));
	return ControlResult::success(remote->state());
}

ControlResult remoteState(const QJsonObject&)
{
	RemoteControls* remote = RemoteControls::instance();
	return remote != nullptr ? ControlResult::success(remote->state()) : ControlResult::success({{QStringLiteral("connected"), false}});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QStringLiteral("remote.") + QLatin1String(verb), description, args, false, handler, false);
}

} // namespace

void registerRemoteCommands(ControlRegistry& registry)
{
	add(registry, "connect", QStringLiteral("Bind a MIDI controller's eight knobs to remote-control pages: 'input' (a readable "
		"port, as midi.clients_list names it), 'output' (optional - the knobs' values are sent back on a page or device change, "
		"for motorised or LED-ring knobs), 'channel' (0-15; any when left out) and 'first_cc' (the first of eight consecutive "
		"CCs, default 16). Replaces a connected one; follows the selection until remote.target says otherwise. 'subscribed' "
		"says whether the input is a port the MIDI client has (false: nothing will arrive)."),
		objectSchema({{QStringLiteral("input"), stringProperty()}, {QStringLiteral("output"), stringProperty()},
			{QStringLiteral("channel"), integerProperty(0, 15)}, {QStringLiteral("first_cc"), integerProperty(0, 120)}}), remoteConnect);
	add(registry, "disconnect", QStringLiteral("Release the remote-control knobs."), objectSchema({}), remoteDisconnect);
	add(registry, "target", QStringLiteral("The device the knobs drive: 'target' (trk-<n> / ch-<n>) and 'plugin' (fx-<n>, or "
		"inst - the default - for a track's instrument); 'follow' true makes them follow the selected track or channel."),
		objectSchema({{QStringLiteral("target"), stringProperty()}, {QStringLiteral("plugin"), stringProperty()},
			{QStringLiteral("follow"), booleanProperty()}}), remoteTarget);
	add(registry, "page", QStringLiteral("Show another page of the device's parameters on the knobs: 'page' (0 = parameters 1-8) "
		"or 'delta' (+1 next, -1 previous; +1 when neither is given). Clamped to the pages there are."),
		objectSchema({{QStringLiteral("page"), integerProperty(0, 1024)}, {QStringLiteral("delta"), integerProperty(-64, 64)}}), remotePage);
	add(registry, "state", QStringLiteral("What the remote-control knobs drive now: the device, the page and its eight "
		"parameters with their values, the page count, the ports and whether they follow the selection."), objectSchema({}), remoteState);
}

} // namespace lmms
