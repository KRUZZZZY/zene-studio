/*
 * ControlCommandsMacros.cpp - macro.*: named runs of registry commands
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

/*! WF-114 (0.5.0 L13): a macro is a named list of registry commands with their arguments, run
 *  in order by macro.run - the same registry, validation and transaction record a single
 *  command has, so each step is undone as its own command records (the run is acknowledged
 *  and the steps run on the next event-loop turn, the mastering.run precedent). Kept in the config file
 *  (ui/macros, UiConfigStore); the Macros menu has an action per macro, so a macro is in the
 *  palette and can be given a shortcut. A macro cannot run a macro (no nesting, no loops), and
 *  scripts cannot run or write macros (ScriptCommandBridge's refusal list). */

#include <QJsonArray>
#include <QMenuBar>
#include <QTimer>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "MacroMenu.h"
#include "MainWindow.h"
#include "ShortcutMap.h"
#include "UiConfigStore.h"

namespace lmms
{

using namespace control;

namespace
{

const QString StoreKey = QStringLiteral("macros");
constexpr int MaxMacros = 64;
constexpr int MaxSteps = 64;
bool g_running = false;  //!< a macro is running: macro.run refuses a second one (UI thread only)
QJsonObject g_lastRun;   //!< macro.status: the last run's outcome

QJsonObject saved()
{
	return gui::uistore::read(StoreKey).value(QStringLiteral("macros")).toObject();
}

//! After a write and its undo: the Macros menu follows, and the shortcuts bound to its actions.
void refreshMenu()
{
	gui::MainWindow* window = controlMainWindow();
	if (window == nullptr) { return; }
	gui::MacroMenu::install(window->menuBar())->refresh();
	gui::ShortcutMap::apply(window->menuBar());
}

ControlResult store(const QJsonObject& macros, QJsonObject result, const gui::uistore::Inverse& inverse)
{
	return gui::uistore::write(StoreKey, {{QStringLiteral("macros"), macros}}, std::move(result), inverse, refreshMenu);
}

//! Why @a steps cannot be a macro, or empty.
QString stepsRefusal(const QJsonArray& steps)
{
	if (steps.isEmpty() || steps.size() > MaxSteps) { return QStringLiteral("a macro has 1..%1 steps").arg(MaxSteps); }
	for (const QJsonValue& value : steps)
	{
		const QString command = value.toObject().value(QStringLiteral("command")).toString();
		if (!ControlRegistry::instance()->hasCommand(command)) { return QStringLiteral("no command '%1'").arg(command); }
		if (command.startsWith(QStringLiteral("macro."))) { return QStringLiteral("a macro cannot run or edit macros"); }
	}
	return QString();
}

ControlResult saveMacro(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	const QJsonArray steps = args.value(QStringLiteral("steps")).toArray();
	if (name.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a macro needs a name")); }
	if (const QString why = stepsRefusal(steps); !why.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, why); }
	QJsonObject macros = saved();
	const bool replaced = macros.contains(name);
	if (!replaced && macros.size() >= MaxMacros)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("at most %1 macros are kept").arg(MaxMacros));
	}
	const QJsonObject previous = macros.value(name).toObject();
	macros.insert(name, QJsonObject{{QStringLiteral("steps"), steps}});
	const gui::uistore::Inverse inverse = replaced
		? gui::uistore::Inverse{QStringLiteral("macro.save"), {{QStringLiteral("name"), name}, {QStringLiteral("steps"), previous.value(QStringLiteral("steps"))}}}
		: gui::uistore::Inverse{QStringLiteral("macro.remove"), {{QStringLiteral("name"), name}}};
	return store(macros, {{QStringLiteral("name"), name}, {QStringLiteral("steps"), steps.size()}, {QStringLiteral("replaced"), replaced}}, inverse);
}

ControlResult removeMacro(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	QJsonObject macros = saved();
	if (!macros.contains(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no macro '%1'").arg(name)); }
	const QJsonValue steps = macros.value(name).toObject().value(QStringLiteral("steps"));
	macros.remove(name);
	return store(macros, {{QStringLiteral("name"), name}, {QStringLiteral("count"), macros.size()}},
		{QStringLiteral("macro.save"), {{QStringLiteral("name"), name}, {QStringLiteral("steps"), steps}}});
}

ControlResult listMacros(const QJsonObject&)
{
	QJsonArray out;
	const QJsonObject macros = saved();
	for (auto it = macros.begin(); it != macros.end(); ++it)
	{
		const QJsonArray steps = it.value().toObject().value(QStringLiteral("steps")).toArray();
		out.append(QJsonObject{{QStringLiteral("name"), it.key()}, {QStringLiteral("step_count"), steps.size()},
			{QStringLiteral("steps"), steps}});
	}
	return ControlResult::success({{QStringLiteral("macros"), out}, {QStringLiteral("count"), out.size()},
		{QStringLiteral("max_macros"), MaxMacros}, {QStringLiteral("max_steps"), MaxSteps}});
}

//! The steps, each a top-level registry call of its own; the outcome lands in g_lastRun.
void runSteps(const QString& name, const QJsonArray& steps)
{
	QJsonArray results;
	int failedAt = -1;
	for (int i = 0; i < steps.size() && failedAt < 0; ++i)
	{
		const QJsonObject step = steps.at(i).toObject();
		const QString command = step.value(QStringLiteral("command")).toString();
		const ControlResult ran = ControlRegistry::instance()->invoke(command, step.value(QStringLiteral("args")).toObject());
		QJsonObject entry{{QStringLiteral("command"), command}, {QStringLiteral("ok"), ran.ok}};
		if (!ran.ok) { entry.insert(QStringLiteral("error"), ran.errorMessage); failedAt = i; }
		results.append(entry);
	}
	g_lastRun = {{QStringLiteral("name"), name}, {QStringLiteral("state"), QStringLiteral("done")},
		{QStringLiteral("ran"), results.size()}, {QStringLiteral("stopped_at"), failedAt}, {QStringLiteral("results"), results}};
	g_running = false;
}

ControlResult runMacro(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	const QJsonObject macros = saved();
	if (!macros.contains(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no macro '%1'").arg(name)); }
	if (g_running) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a macro is already running")); }
	// The steps run on the next turn of the event loop, each as a top-level command: run inside
	// this handler, the registry would fold every step's undo step into this command's while each
	// step kept a transaction record of its own, and control.undo would then disagree with Ctrl+Z.
	g_running = true;
	const QJsonArray steps = macros.value(name).toObject().value(QStringLiteral("steps")).toArray();
	g_lastRun = {{QStringLiteral("name"), name}, {QStringLiteral("state"), QStringLiteral("running")}};
	QTimer::singleShot(0, ControlRegistry::instance(), [name, steps] { runSteps(name, steps); });
	return ControlResult::success({{QStringLiteral("name"), name}, {QStringLiteral("queued"), steps.size()},
		{QStringLiteral("poll"), QStringLiteral("macro.status")}});
}

//! ED-056: the macro being recorded - the commands the user (or an agent) runs until macro.record_stop.
struct Recording
{
	bool on = false;
	QString name;
	QJsonArray steps;
};

Recording& recording()
{
	static Recording value;
	return value;
}

ControlResult recordStart(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	if (name.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a macro needs a name")); }
	recording() = Recording{true, name, {}};
	ControlRegistry::instance()->setCommandObserver([](const QString& command, const QJsonObject& stepArgs) {
		// Undo, redo and the macro commands themselves are not steps; the step count is the macro's own bound.
		if (command.startsWith(QStringLiteral("macro.")) || command.startsWith(QStringLiteral("control.")) || recording().steps.size() >= MaxSteps) { return; }
		recording().steps.append(QJsonObject{{QStringLiteral("command"), command}, {QStringLiteral("args"), stepArgs}});
	});
	return ControlResult::success({{QStringLiteral("name"), name}, {QStringLiteral("recording"), true}});
}

ControlResult recordStop(const QJsonObject&)
{
	ControlRegistry::instance()->setCommandObserver({});
	const Recording taken = recording();
	recording() = Recording{};
	if (!taken.on) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no macro is being recorded")); }
	if (taken.steps.isEmpty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("nothing was done while recording")); }
	return saveMacro({{QStringLiteral("name"), taken.name}, {QStringLiteral("steps"), taken.steps}});
}

ControlResult macroStatus(const QJsonObject&)
{
	QJsonObject out = g_lastRun;
	if (out.isEmpty()) { out.insert(QStringLiteral("state"), QStringLiteral("idle")); }
	return ControlResult::success(out);
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	const QJsonObject& result, bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd = guiCommand("macro", verb);
	cmd.description = description;
	cmd.argsSchema = args;
	cmd.resultSchema = result;
	cmd.mutating = mutating;
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void registerMacroCommands(ControlRegistry& registry)
{
	const QJsonObject named = objectSchema({{QStringLiteral("name"), stringProperty()}}, {QStringLiteral("name")});
	add(registry, "save", QStringLiteral("Save a macro: 'steps' is a list of {command, args} run in order by "
		"macro.run (1..64 steps, every command registered, none a macro.* one). Replaces a macro of that name; at "
		"most 64 are kept. The Macros menu gains an action for it. Kept in the config file; one undo step."),
		objectSchema({{QStringLiteral("name"), stringProperty()}, {QStringLiteral("steps"), arrayProperty()}},
			{QStringLiteral("name"), QStringLiteral("steps")}),
		objectSchema({{QStringLiteral("name"), stringProperty()}, {QStringLiteral("steps"), integerProperty()},
			{QStringLiteral("replaced"), booleanProperty()}}), true, saveMacro);
	add(registry, "run", QStringLiteral("Run macro 'name': acknowledged at once ('queued' steps), then each step runs "
		"through the registry on the next turn of the event loop, in order, stopping at the first that fails - "
		"macro.status reports the outcome ('stopped_at', -1 when every step ran). Each step records and undoes as "
		"itself; this command records nothing of its own. One macro runs at a time."), named,
		objectSchema({{QStringLiteral("name"), stringProperty()}, {QStringLiteral("queued"), integerProperty()},
			{QStringLiteral("poll"), stringProperty()}}), false, runMacro);
	add(registry, "status", QStringLiteral("The last macro run: 'state' (idle, running, done), and once done how many "
		"steps ran, where it stopped and each step's outcome."), objectSchema({}), objectProperty(), false, macroStatus);
	add(registry, "list", QStringLiteral("The saved macros with their steps."), objectSchema({}),
		objectSchema({{QStringLiteral("macros"), arrayProperty()}, {QStringLiteral("count"), integerProperty()},
			{QStringLiteral("max_macros"), integerProperty()}, {QStringLiteral("max_steps"), integerProperty()}}), false, listMacros);
	add(registry, "record_start", QStringLiteral("Start recording macro 'name': every command that changes something from now on "
		"(not undo, redo or the macro commands) becomes one of its steps, with its arguments, until macro.record_stop."),
		named, objectProperty(), false, recordStart);
	add(registry, "record_stop", QStringLiteral("Stop recording and save the macro as macro.save would (refused when nothing "
		"was done). One undo step."), objectSchema({}), objectProperty(), true, recordStop);
	add(registry, "remove", QStringLiteral("Forget macro 'name'. One undo step."), named,
		objectSchema({{QStringLiteral("name"), stringProperty()}, {QStringLiteral("count"), integerProperty()}}), true, removeMacro);
}

} // namespace lmms
