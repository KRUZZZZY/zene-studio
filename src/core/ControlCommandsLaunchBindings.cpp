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
 *  undo step.
 *
 *  Q050-217.1 adds the same surface for built-in commands: session.bind_command binds a note OR a
 *  control-change to a command that needs no arguments, session.unbind_command removes it, and
 *  session.command_bindings lists them. Same controller, same undo rule; an overlapping note binding
 *  is resolved by channel specificity, with commands winning ties. */

#include "ControlCommandDef.h"
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

//! The command binding names' triggers (Q050-217.1).
const QStringList& triggerNames()
{
	static const QStringList names{QStringLiteral("note"), QStringLiteral("cc")};
	return names;
}

/*! Q050-217.1: why @a id cannot be bound to a MIDI control, or an empty string when it can. A
 *  control activation carries no arguments - the CC value is NOT one - so a command that requires
 *  any would only ever fail; it is refused when the binding is made, with the reason, rather than
 *  firing into a refusal the user cannot see. */
QString unbindableReason(const QString& id)
{
	const ControlCommand* command = ControlRegistry::instance()->command(id);
	if (command == nullptr) { return QStringLiteral("no command '%1'").arg(id); }
	if (!command->argsSchema.value(QStringLiteral("required")).toArray().isEmpty())
	{
		return QStringLiteral("'%1' needs arguments, and a MIDI control supplies none").arg(id);
	}
	return QString();
}

//! The stored command bindings without the one a trigger names: the same note (on a channel) or the same CC.
QJsonArray commandBindingsWithout(const QJsonObject& trigger)
{
	QJsonArray out;
	const bool control = trigger.value(QStringLiteral("trigger")).toString() == QLatin1String("cc");
	const int channel = trigger.value(QStringLiteral("channel")).toInt(-1);
	for (const QJsonValue& value : LaunchBindings::commandBindings())
	{
		const QJsonObject binding = value.toObject();
		const bool sameKind = (binding.value(QStringLiteral("trigger")).toString() == QLatin1String("cc")) == control;
		const bool sameNumber = control ? binding.value(QStringLiteral("controller")).toInt(-1) == trigger.value(QStringLiteral("controller")).toInt()
										: binding.value(QStringLiteral("key")).toInt(-1) == trigger.value(QStringLiteral("key")).toInt();
		const bool sameChannel = binding.value(QStringLiteral("channel")).toInt(-1) == channel;
		if (!(sameKind && sameNumber && sameChannel)) { out.append(binding); }
	}
	return out;
}

//! The trigger @a args name: {trigger: note|cc, key|controller, channel}. The kind's own number is
//! checked here (the schema cannot say "key is required when trigger is note"); false with a reason
//! when it is missing.
bool triggerOf(const QJsonObject& args, QJsonObject* out, ControlResult* error)
{
	const QString trigger = args.value(QStringLiteral("trigger")).toString();
	const QString number = trigger == QLatin1String("cc") ? QStringLiteral("controller") : QStringLiteral("key");
	if (!args.contains(number))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a %1 binding needs '%2'").arg(trigger, number));
		return false;
	}
	*out = {{QStringLiteral("trigger"), trigger}, {number, args.value(number).toInt()},
		{QStringLiteral("channel"), args.value(QStringLiteral("channel")).toInt(-1)}};
	return true;
}

//! Replace the command bindings with @a next as one undo step; answers them.
ControlResult storeCommandBindings(const QJsonArray& next)
{
	const QJsonArray before = LaunchBindings::commandBindings();
	LaunchBindings::setCommandBindings(next);
	addUndoStep([before]() { LaunchBindings::setCommandBindings(before); }, [next]() { LaunchBindings::setCommandBindings(next); });
	return ControlResult::success({{QStringLiteral("bindings"), next}});
}

ControlResult bindCommand(const QJsonObject& args)
{
	const QString command = args.value(QStringLiteral("command")).toString();
	const QString reason = unbindableReason(command);
	if (!reason.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, reason); }
	QJsonObject trigger;
	ControlResult error;
	if (!triggerOf(args, &trigger, &error)) { return error; }
	QJsonObject binding = trigger;
	binding.insert(QStringLiteral("command"), command);
	// A NOTE bound to a command takes the same-channel launcher binding over. A binding on any channel
	// (-1) replaces every launcher binding for that note; a channel-specific one leaves a wildcard
	// launcher available on the other channels. A control change never launched anything, so a CC binding
	// leaves the launcher store alone. Undo puts both stores back: this is ONE edit, so it is one step.
	const bool note = trigger.value(QStringLiteral("trigger")).toString() == QLatin1String("note");
	const int channel = trigger.value(QStringLiteral("channel")).toInt(-1);
	QJsonArray launcher;
	for (const QJsonValue& value : LaunchBindings::bindings())
	{
		const QJsonObject old = value.toObject();
		const bool same = note && old.value(QStringLiteral("key")).toInt() == trigger.value(QStringLiteral("key")).toInt()
			&& (channel < 0 || old.value(QStringLiteral("channel")).toInt(-1) == channel);
		if (!same) { launcher.append(old); }
	}
	QJsonArray next = commandBindingsWithout(trigger);
	next.append(binding);
	const QJsonArray launcherBefore = LaunchBindings::bindings();
	const QJsonArray before = LaunchBindings::commandBindings();
	LaunchBindings::setBindings(launcher);
	LaunchBindings::setCommandBindings(next);
	addUndoStep([launcherBefore, before]() { LaunchBindings::setBindings(launcherBefore); LaunchBindings::setCommandBindings(before); },
		[launcher, next]() { LaunchBindings::setBindings(launcher); LaunchBindings::setCommandBindings(next); });
	return ControlResult::success({{QStringLiteral("bindings"), next}});
}

ControlResult unbindCommand(const QJsonObject& args)
{
	QJsonObject trigger;
	ControlResult error;
	if (!triggerOf(args, &trigger, &error)) { return error; }
	const QJsonArray remaining = commandBindingsWithout(trigger);
	if (remaining.size() == LaunchBindings::commandBindings().size())
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no command binding on that %1")
			.arg(trigger.value(QStringLiteral("trigger")).toString()));
	}
	return storeCommandBindings(remaining);
}

ControlResult listCommandBindings(const QJsonObject&)
{
	const LaunchBindings* bindings = LaunchBindings::instance();
	return ControlResult::success({{QStringLiteral("connected"), bindings != nullptr},
		{QStringLiteral("bindings"), LaunchBindings::commandBindings()}});
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
	const int key = args.value(QStringLiteral("key")).toInt();
	const int channel = args.value(QStringLiteral("channel")).toInt(-1);
	const QJsonArray remaining = without(key, channel);
	if (remaining.size() == LaunchBindings::bindings().size())
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no binding on key %1, channel %2").arg(key).arg(channel));
	}
	return storeBindings(remaining);
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
	return ControlResult::success({{QStringLiteral("connected"), bindings->isOpen()}, {QStringLiteral("subscribed"), bindings->subscribed()},
		{QStringLiteral("input"), bindings->input()},
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
	control::defineCommandWithSchema(registry, QStringLiteral("session.") + QLatin1String(verb), description, args, mutating, handler, false);
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
		"port, as midi.clients_list names it), 'output' optional. Its notes then launch what they are bound to. 'subscribed' "
		"says whether the input is a port the MIDI client has (false: nothing will arrive)."),
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

	// Q050-217.1: the same controller, bound to built-in commands that need no arguments.
	const QJsonObject commandTrigger{{QStringLiteral("trigger"), enumProperty(triggerNames())},
		{QStringLiteral("key"), integerProperty(0, 127)}, {QStringLiteral("controller"), integerProperty(0, 127)},
		{QStringLiteral("channel"), integerProperty(-1, 15)}, {QStringLiteral("command"), stringProperty()}};
	add(registry, "bind_command", QStringLiteral("Bind a MIDI NOTE or CONTROL CHANGE to a built-in command that needs no "
		"arguments, and run it through the registry once per activation. 'trigger' is note (with 'key' 0..127) or cc (with "
		"'controller' 0..127); 'channel' is 0..15 or -1 for any; 'command' is a registered command id whose schema requires no "
		"arguments (the control change's value is NOT passed on as one). A note fires on its press and rearms on its release; a "
		"control change fires when it leaves zero and rearms at zero, so a sweep is one activation. Replaces a binding on the "
		"same control, and a note still bound to the launcher is taken away from it by the same edit (one undo step). Kept in the "
		"config file."), objectSchema(commandTrigger, {QStringLiteral("trigger"), QStringLiteral("command")}), bindCommand, true);
	add(registry, "unbind_command", QStringLiteral("Remove the command binding on a note ('trigger' note, 'key', 'channel') or a "
		"control change ('trigger' cc, 'controller', 'channel'). One undo step."),
		objectSchema(commandTrigger, {QStringLiteral("trigger")}), unbindCommand, true);
	add(registry, "command_bindings", QStringLiteral("The controller's command bindings (what session.bind_command made), and "
		"whether a controller is connected."), objectSchema({}), listCommandBindings, false);
}

} // namespace lmms
