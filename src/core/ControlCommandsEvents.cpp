/*
 * ControlCommandsEvents.cpp - events.subscribe / unsubscribe / get_state (Q050-334.1).
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

#include <QJsonArray>
#include <QJsonObject>

#include "ControlEventHub.h"
#include "ControlRegistry.h"

namespace lmms
{

using namespace control;

namespace
{

QJsonObject typed(const char* type)
{
	return QJsonObject{{QStringLiteral("type"), QString::fromLatin1(type)}};
}

ControlResult eventsSubscribe(const QJsonObject& args)
{
	const int limit = args.value(QStringLiteral("queue_limit")).toInt(ControlEventQueue::DefaultEventLimit);
	const ControlEventHub::SubscribeResult subscribed = ControlEventHub::instance().subscribe(limit);
	if (subscribed.noConnection)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral(
			"events.subscribe needs a local control-socket connection to deliver to; none serves this call"));
	}
	if (!subscribed.ok)
	{
		return ControlResult::failure(ControlErrorKind::Busy, QStringLiteral("this connection already has a "
			"subscription: call events.unsubscribe first, or read on until its events_overflow notice ended it"));
	}
	return ControlResult::success(subscribed.result);
}

ControlResult eventsUnsubscribe(const QJsonObject& args)
{
	const QString subscription = args.value(QStringLiteral("subscription")).toString();
	const QJsonObject result = ControlEventHub::instance().unsubscribe(subscription);
	if (result.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("this connection has no subscription '%1' (an overflow or a closed connection ends one)")
				.arg(subscription));
	}
	return ControlResult::success(result);
}

} // namespace

void registerEventsCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("events.subscribe");
		cmd.group = QStringLiteral("events");
		cmd.verb = QStringLiteral("subscribe");
		cmd.description = QStringLiteral("Subscribe THIS connection to the instance's ordered project-change events. "
			"After the reply, every project change from any client, a script or the interface arrives on this "
			"connection as an unsolicited line {\"event\":\"project_changed\",\"subscription\",\"session\",\"seq\","
			"\"source\":\"command\"|\"journal\",\"kind\":\"edit\"|\"undo\"|\"redo\"|\"project_replaced\",\"command\","
			"\"args\"} with no 'id', seq strictly increasing. 'seq' in this reply is the current position: compare it "
			"with the last seq you saw (and 'session' with the last session) to learn whether you missed anything while "
			"away; events are not replayed, so after any gap reread the project, after subscribing so no change falls between. Delivery is bounded by 'queue_limit' "
			"events (default 256, at most 4096) and 1 MiB per subscriber: a client that falls behind gets what was "
			"queued, then one {\"event\":\"events_overflow\",...} line naming first_missed_seq, and the subscription "
			"ends - subscribe again, then reread the project. A connection holds one subscription; a second is refused "
			"'busy'. A subscribed idle connection is not retired by the idle timeout. Runtime state only: never saved "
			"in a project. Over the MCP bridge the lines are skipped, because a bridge call has no way to receive them.");
		cmd.requiresEngine = false;
		cmd.argsSchema = objectSchema({
			{QStringLiteral("queue_limit"), integerProperty(1, ControlEventQueue::MaxEventLimit)},
		});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("subscription"), stringProperty()},
			{QStringLiteral("session"), stringProperty()},
			{QStringLiteral("seq"), typed("integer")},
			{QStringLiteral("queue_limit"), typed("integer")},
			{QStringLiteral("queue_byte_limit"), typed("integer")},
		});
		cmd.handler = eventsSubscribe;
		registry.registerCommand(cmd);
	}
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("events.unsubscribe");
		cmd.group = QStringLiteral("events");
		cmd.verb = QStringLiteral("unsubscribe");
		cmd.description = QStringLiteral("End this connection's subscription: no event follows this reply. "
			"'last_delivered_seq' is the last event sent before it; 'discarded' counts queued events dropped unsent. "
			"Typed not_found for a subscription this connection does not hold.");
		cmd.requiresEngine = false;
		cmd.argsSchema = objectSchema({{QStringLiteral("subscription"), stringProperty()}},
			{QStringLiteral("subscription")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("unsubscribed"), typed("boolean")},
			{QStringLiteral("subscription"), stringProperty()},
			{QStringLiteral("last_delivered_seq"), typed("integer")},
			{QStringLiteral("discarded"), typed("integer")},
		});
		cmd.handler = eventsUnsubscribe;
		registry.registerCommand(cmd);
	}
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("events.get_state");
		cmd.group = QStringLiteral("events");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("The event stream as this instance sees it: 'session', the current 'seq', every "
			"live subscription with its queue depth against its bound, how many overflows and subscriber disconnects "
			"have happened, and the limits. Reads only.");
		cmd.requiresEngine = false;
		cmd.resultSchema = objectSchema({
			{QStringLiteral("session"), stringProperty()},
			{QStringLiteral("seq"), typed("integer")},
			{QStringLiteral("subscriptions"), typed("array")},
			{QStringLiteral("overflows"), typed("integer")},
			{QStringLiteral("disconnects"), typed("integer")},
			{QStringLiteral("default_queue_limit"), typed("integer")},
			{QStringLiteral("max_queue_limit"), typed("integer")},
			{QStringLiteral("queue_byte_limit"), typed("integer")},
		});
		cmd.handler = [](const QJsonObject&) { return ControlResult::success(ControlEventHub::instance().state()); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
