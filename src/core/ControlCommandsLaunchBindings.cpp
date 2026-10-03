/*
 * ControlCommandsLaunchBindings.cpp - session.bind*: launch the launcher from a MIDI controller
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
/*! 0.5.0 L02 (WF-069): the launcher's controller bindings on the spine (LaunchBindings.h).
 *  session.bindings_connect opens the controller; session.bind binds a note to a cell, a scene or
 *  stop-all, session.bind_learn binds the next note played, session.unbind removes one, and
 *  session.bindings lists them. The bindings are a preference in the config file; a change is one
 *  undo step. */

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "LaunchBindings.h"

namespace lmms
{

using namespace control;

namespace
{

const QStringList& actionNames()
{
	static const QStringList names{QStringLiteral("slot"), QStringLiteral("scene"), QStringLiteral("stop_all")};
	return names;
}

//! The action part of a binding from @a args, or false (with @a error) when it is incomplete.
bool actionOf(const QJsonObject& args, QJsonObject* out, ControlResult* error)
{
	const QString action = args.value(QStringLiteral("action")).toString();
	const bool needsScene = action != QLatin1String("stop_all");
	const bool needsTrack = action == QLatin1String("slot");
	if ((needsScene && !args.contains(QStringLiteral("scene"))) || (needsTrack && !args.contains(QStringLiteral("track"))))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a slot needs 'track' and 'scene', a scene needs 'scene'"));
		return false;
	}
	*out = {{QStringLiteral("action"), action}};
	if (needsScene) { out->insert(QStringLiteral("scene"), args.value(QStringLiteral("scene")).toInt()); }
	if (needsTrack) { out->insert(QStringLiteral("track"), args.value(QStringLiteral("track")).toInt()); }
	return true;
}

//! Replace the stored bindings with @a next as one undo step; answers them.
ControlResult storeBindings(const QJsonArray& next)
{
	const QJsonArray before = LaunchBindings::bindings();
	LaunchBindings::setBindings(next);
	addUndoStep([before]() { LaunchBindings::setBindings(before); }, [next]() { LaunchBindings::setBindings(next); });
	return ControlResult::success({{QStringLiteral("bindings"), next}});
}

//! The stored bindings without the one on (@a key, @a channel).
QJsonArray without(int key, int channel)
{
	QJsonArray out;
	for (const QJsonValue& value : LaunchBindings::bindings())
	{
		const QJsonObject binding = value.toObject();
		if (binding.value(QStringLiteral("key")).toInt() != key || binding.value(QStringLiteral("channel")).toInt(-1) != channel) { out.append(binding); }
	}
	return out;
}

ControlResult bind(const QJsonObject& args)
{
	QJsonObject binding;
	ControlResult error;
	if (!actionOf(args, &binding, &error)) { return error; }
	const int key = args.value(QStringLiteral("key")).toInt();
	const int channel = args.value(QStringLiteral("channel")).toInt(-1);
	binding.insert(QStringLiteral("key"), key);
	binding.insert(QStringLiteral("channel"), channel);
	QJsonArray next = without(key, channel);
	next.append(binding);
	return storeBindings(next);
}

ControlResult unbind(const QJsonObject& args)
{
	return storeBindings(without(args.value(QStringLiteral("key")).toInt(), args.value(QStringLiteral("channel")).toInt(-1)));
}

ControlResult bindLearn(const QJsonObject& args)
{
	LaunchBindings* bindings = LaunchBindings::instance();
	if (bindings == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no controller is connected (session.bindings_connect)"));
	}
	QJsonObject action;
	ControlResult error;
	if (!actionOf(args, &action, &error)) { return error; }
	bindings->learn(action);
	return ControlResult::success({{QStringLiteral("learning"), action}});
}

ControlResult connectBindings(const QJsonObject& args)
{
	LaunchBindings* bindings = LaunchBindings::connect(args.value(QStringLiteral("input")).toString(), args.value(QStringLiteral("output")).toString());
	return ControlResult::success({{QStringLiteral("connected"), bindings->isOpen()}, {QStringLiteral("input"), bindings->input()},
		{QStringLiteral("bindings"), LaunchBindings::bindings()}});
}

ControlResult listBindings(const QJsonObject&)
{
	const LaunchBindings* bindings = LaunchBindings::instance();
	return ControlResult::success({{QStringLiteral("connected"), bindings != nullptr}, {QStringLiteral("bindings"), LaunchBindings::bindings()},
		{QStringLiteral("learning"), bindings != nullptr ? bindings->learning() : QJsonObject{}}});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	ControlResult (*handler)(const QJsonObject&), bool mutating)
{
	ControlCommand cmd;
	cmd.group = QStringLiteral("session");
	cmd.verb = QLatin1String(verb);
	cmd.id = cmd.group + QLatin1Char('.') + cmd.verb;
	cmd.description = description;
	cmd.argsSchema = args;
	cmd.resultSchema = objectProperty();
	cmd.mutating = mutating;
	cmd.requiresEngine = false;
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void registerLaunchBindingCommands(ControlRegistry& registry)
{
	const QJsonObject action{{QStringLiteral("action"), enumProperty(actionNames())}, {QStringLiteral("track"), integerProperty(0, 255)},
		{QStringLiteral("scene"), integerProperty(0, 511)}};
	QJsonObject withNote = action;
	withNote.insert(QStringLiteral("key"), integerProperty(0, 127));
	withNote.insert(QStringLiteral("channel"), integerProperty(-1, 15));
	add(registry, "bindings_connect", QStringLiteral("Open a MIDI controller for the launcher's bindings: 'input' (a readable "
		"port, as midi.clients_list names it), 'output' optional. Its notes then launch what they are bound to."),
		objectSchema({{QStringLiteral("input"), stringProperty()}, {QStringLiteral("output"), stringProperty()}}), connectBindings, false);
	add(registry, "bind", QStringLiteral("Bind a note ('key' 0..127, 'channel' 0..15 or -1 for any) to a launcher action: 'action' "
		"slot (with 'track' and 'scene' - the note's release releases a Gate or Repeat cell), scene (with 'scene') or stop_all. "
		"Replaces a binding on the same note. Kept in the config file; one undo step."),
		objectSchema(withNote, {QStringLiteral("key"), QStringLiteral("action")}), bind, true);
	add(registry, "bind_learn", QStringLiteral("Bind the next note the connected controller plays to a launcher action ('action', "
		"'track', 'scene' as session.bind)."), objectSchema(action, {QStringLiteral("action")}), bindLearn, false);
	add(registry, "unbind", QStringLiteral("Remove the binding on a note ('key', 'channel' - -1 for an any-channel binding). One "
		"undo step."), objectSchema({{QStringLiteral("key"), integerProperty(0, 127)}, {QStringLiteral("channel"), integerProperty(-1, 15)}},
		{QStringLiteral("key")}), unbind, true);
	add(registry, "bindings", QStringLiteral("The launcher's controller bindings, whether a controller is connected and what the "
		"next note will be bound to (session.bind_learn)."), objectSchema({}), listBindings, false);
}

} // namespace lmms
