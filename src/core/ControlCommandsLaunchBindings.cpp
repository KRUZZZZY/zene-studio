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

/*! Q050-217.2: the names the workflow library in use holds, sorted, for a refusal that says what
 *  there is. Read through workflow.list - the one reader of the library - so a shared or per-project
 *  library answers the same way it does to the Workflows menu. */
QStringList workflowNames()
{
	QStringList names;
	for (const QJsonValue& value : ControlRegistry::instance()->invoke(QStringLiteral("workflow.list")).result
		.value(QStringLiteral("workflows")).toArray())
	{
		names.append(value.toObject().value(QStringLiteral("name")).toString());
	}
	names.sort();
	return names;
}

/*! Q050-217.2: the workflow a binding names, trimmed, or an empty string when the library in use
 *  does not hold @a name - with @a error saying so. A workflow target is a saved NAME (not a copy of
 *  its steps), run through workflow.run when the binding fires; resolving it at BINDING time keeps
 *  the command's promise honest (a typo is refused where the user can see it), and it is resolved
 *  again from that same library when it fires, so editing, renaming or switching scope is followed
 *  rather than frozen. A core-only build registers no workflow.* group: then the check is skipped and
 *  the binding is stored, exactly as a command id this build lacks would be. */
QString workflowTarget(const QString& name, ControlResult* error)
{
	const QString trimmed = name.trimmed();
	if (trimmed.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a workflow binding needs 'workflow', its name"));
		return QString();
	}
	ControlRegistry* registry = ControlRegistry::instance();
	if (!registry->hasCommand(QStringLiteral("workflow.list"))) { return trimmed; }
	const QStringList names = workflowNames();
	if (!names.contains(trimmed))
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound, names.isEmpty()
			? QStringLiteral("no workflow '%1': the library in use holds none (workflow.save makes one)").arg(trimmed)
			: QStringLiteral("no workflow '%1' in the library in use (%2)").arg(trimmed, names.join(QStringLiteral(", "))));
		return QString();
	}
	return trimmed;
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

/*! The target @a args name, as the ONE key/value a stored binding carries, or false with @a error.
 *  A binding targets exactly one of `command` (Q050-217.1) and `workflow` (Q050-217.2); when both
 *  are given the call is ambiguous and refused rather than silently preferring one. */
bool targetOf(const QJsonObject& args, QJsonObject* out, ControlResult* error)
{
	const bool hasCommand = args.contains(QStringLiteral("command"));
	const bool hasWorkflow = args.contains(QStringLiteral("workflow"));
	if (hasCommand == hasWorkflow)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, hasCommand
			? QStringLiteral("a binding has one target: 'command' or 'workflow', not both")
			: QStringLiteral("a binding needs a target: 'command' (a command id) or 'workflow' (a saved workflow's name)"));
		return false;
	}
	if (hasWorkflow)
	{
		const QString name = workflowTarget(args.value(QStringLiteral("workflow")).toString(), error);
		if (name.isEmpty()) { return false; }
		*out = {{QStringLiteral("workflow"), name}};
		return true;
	}
	const QString command = args.value(QStringLiteral("command")).toString();
	const QString reason = unbindableReason(command);
	if (!reason.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, reason);
		return false;
	}
	*out = {{QStringLiteral("command"), command}};
	return true;
}

ControlResult bindCommand(const QJsonObject& args)
{
	QJsonObject target;
	ControlResult error;
	if (!targetOf(args, &target, &error)) { return error; }
	QJsonObject trigger;
	if (!triggerOf(args, &trigger, &error)) { return error; }
	QJsonObject binding = trigger;
	for (auto it = target.begin(); it != target.end(); ++it) { binding.insert(it.key(), it.value()); }
	// A NOTE bound to a target takes the same-channel launcher binding over. A binding on any channel
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

/*! Q050-217.2: what @a binding names, as the readback states it - its `target` ("command" or
 *  "workflow"), the target's name, and whether it still RESOLVES now. A workflow resolves against the
 *  library in use (a shared or per-project library, by the same reader the Workflows menu uses), so
 *  the answer follows the scope setting and an edit; a command resolves against the registry. A
 *  binding that does not resolve is kept, deliberately: the config is the user's, a workflow may come
 *  back (a removed one restored by undo, a project reopened), and silently deleting their binding
 *  would be data loss. It is reported as `resolved: false` with the reason instead. */
QJsonObject describeBinding(const QJsonObject& binding, const QStringList& workflows)
{
	const QString key = LaunchBindings::targetKey(binding);
	const QString target = binding.value(key).toString();
	QJsonObject out = binding;
	out.insert(QStringLiteral("target"), key);
	out.insert(QStringLiteral("name"), target);
	const bool resolved = key == QLatin1String("workflow") ? workflows.contains(target)
		: ControlRegistry::instance()->hasCommand(target);
	out.insert(QStringLiteral("resolved"), resolved);
	if (!resolved)
	{
		out.insert(QStringLiteral("reason"), key == QLatin1String("workflow")
			? QStringLiteral("the library in use holds no workflow '%1'").arg(target)
			: QStringLiteral("no command '%1' in this build").arg(target));
	}
	return out;
}

ControlResult listCommandBindings(const QJsonObject&)
{
	const LaunchBindings* bindings = LaunchBindings::instance();
	const QStringList workflows = ControlRegistry::instance()->hasCommand(QStringLiteral("workflow.list"))
		? workflowNames() : QStringList();
	QJsonArray described;
	for (const QJsonValue& value : LaunchBindings::commandBindings()) { described.append(describeBinding(value.toObject(), workflows)); }
	return ControlResult::success({{QStringLiteral("connected"), bindings != nullptr},
		{QStringLiteral("bindings"), LaunchBindings::commandBindings()}, {QStringLiteral("targets"), described},
		{QStringLiteral("workflows"), QJsonArray::fromStringList(workflows)},
		{QStringLiteral("workflow_scope"), ControlRegistry::instance()->hasCommand(QStringLiteral("workflow.list"))
			? ControlRegistry::instance()->invoke(QStringLiteral("workflow.list")).result.value(QStringLiteral("scope")) : QJsonValue()}});
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

	// Q050-217.1 and Q050-217.2: the same controller, bound to a command or a saved workflow.
	const QJsonObject commandTrigger{{QStringLiteral("trigger"), enumProperty(triggerNames())},
		{QStringLiteral("key"), integerProperty(0, 127)}, {QStringLiteral("controller"), integerProperty(0, 127)},
		{QStringLiteral("channel"), integerProperty(-1, 15)}, {QStringLiteral("command"), stringProperty()},
		{QStringLiteral("workflow"), stringProperty()}};
	add(registry, "bind_command", QStringLiteral("Bind a MIDI NOTE or CONTROL CHANGE to a target that needs no arguments, and "
		"run it once per activation: 'command' is a registered command id whose schema requires none, or 'workflow' is the name "
		"of a workflow saved in the library in use (shared or with the project - workflow.save), run through workflow.run. "
		"'trigger' is note (with 'key' 0..127) or cc (with 'controller' 0..127); 'channel' is 0..15 or -1 for any. The control "
		"change's value is NOT passed on as an argument. A note fires on its press and rearms on its release; a control change "
		"fires when it leaves zero and rearms at zero, so a sweep is one activation. A workflow is resolved from the library in "
		"use when it fires, so editing it, or keeping workflows with the project, is followed; a name that library no longer "
		"holds does nothing. Replaces a binding on the same control, and a note still bound to the launcher is taken away from it "
		"by the same edit (one undo step). Kept in the config file."),
		objectSchema(commandTrigger, {QStringLiteral("trigger")}), bindCommand, true);
	add(registry, "unbind_command", QStringLiteral("Remove the binding on a note ('trigger' note, 'key', 'channel') or a "
		"control change ('trigger' cc, 'controller', 'channel'), whichever target it names - a command or a workflow. One undo "
		"step."), objectSchema(commandTrigger, {QStringLiteral("trigger")}), unbindCommand, true);
	add(registry, "command_bindings", QStringLiteral("The controller's bindings to commands and workflows (what "
		"session.bind_command made), each with its target ('command' or 'workflow'), and whether a controller is connected."),
		objectSchema({}), listCommandBindings, false);
}

} // namespace lmms
