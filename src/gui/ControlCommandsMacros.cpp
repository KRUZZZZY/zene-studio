/*
 * ControlCommandsMacros.cpp - workflow.*: named runs of registry commands
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

/*! WF-114 (0.5.0 L13), renamed for design theme 19: a workflow (once "macro" - a word the racks' macro
 *  knobs already use) is a named list of registry commands with their arguments, run in order by
 *  workflow.run - the same registry, validation and transaction record a single command has, so each
 *  step is undone as its own command records (the run is acknowledged and the steps run on the next
 *  event-loop turn, the mastering.run precedent). The library is kept in the config file (ui/workflows,
 *  UiConfigStore) - shared by every project - or, when the user chose so (workflow.set_scope, the
 *  ui/workflows.scope setting), with each project (<workspace>, ProjectWorkspace). The Workflows menu has
 *  an action per workflow, so a workflow is in the palette and can be given a shortcut. A workflow cannot
 *  run a workflow (no nesting, no loops), and scripts cannot run or write workflows (ScriptCommandBridge's
 *  refusal list). Recording is visible while it lasts: the header's indicator and the menu's status line
 *  (MacroMenu::showRecording), and workflow.status's `recording`. */

#include <QJsonArray>
#include <QMenuBar>
#include <QTimer>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "MacroMenu.h"
#include "MainWindow.h"
#include "ProjectWorkspace.h"
#include "ShortcutMap.h"
#include "UiConfigStore.h"

namespace lmms
{

using namespace control;

namespace
{

const QString StoreKey = QStringLiteral("workflows");
const QString ScopeKey = QStringLiteral("workflows.scope");
const QString SharedScope = QStringLiteral("shared");
const QString ProjectScope = QStringLiteral("project");
constexpr int MaxWorkflows = 64;
constexpr int MaxSteps = 64;
bool g_running = false;  //!< a workflow is running: workflow.run refuses a second one (UI thread only)
QJsonObject g_lastRun;   //!< workflow.status: the last run's outcome

//! Whether the user keeps workflows with each project (ui/workflows.scope) rather than sharing them.
bool keptWithProject()
{
	return gui::uistore::read(ScopeKey).value(QStringLiteral("scope")).toString() == ProjectScope;
}

QString scopeName()
{
	return keptWithProject() ? ProjectScope : SharedScope;
}

//! The library in use: the project's own, or the shared one in the config file.
QJsonObject saved()
{
	if (keptWithProject()) { return ProjectWorkspace::instance().part(ProjectWorkspace::Part::Workflows); }
	return gui::uistore::read(StoreKey).value(QStringLiteral("workflows")).toObject();
}

//! After a write and its undo: the Workflows menu follows, and the shortcuts bound to its actions.
void refreshMenu()
{
	gui::MainWindow* window = controlMainWindow();
	if (window == nullptr) { return; }
	gui::MacroMenu::install(window->menuBar())->refresh();
	gui::ShortcutMap::apply(window->menuBar());
}

//! Write @a workflows to the library in use, as ONE undo step.
ControlResult store(const QJsonObject& workflows, QJsonObject result, const gui::uistore::Inverse& inverse)
{
	result.insert(QStringLiteral("scope"), scopeName());
	if (keptWithProject())
	{
		return gui::uistore::writeProject(ProjectWorkspace::Part::Workflows, workflows, std::move(result), inverse, refreshMenu);
	}
	return gui::uistore::write(StoreKey, {{QStringLiteral("workflows"), workflows}}, std::move(result), inverse, refreshMenu);
}

//! Why @a steps cannot be a workflow, or empty.
QString stepsRefusal(const QJsonArray& steps)
{
	if (steps.isEmpty() || steps.size() > MaxSteps) { return QStringLiteral("a workflow has 1..%1 steps").arg(MaxSteps); }
	for (const QJsonValue& value : steps)
	{
		const QString command = value.toObject().value(QStringLiteral("command")).toString();
		if (!ControlRegistry::instance()->hasCommand(command)) { return QStringLiteral("no command '%1'").arg(command); }
		if (command.startsWith(QStringLiteral("workflow."))) { return QStringLiteral("a workflow cannot run or edit workflows"); }
	}
	return QString();
}

ControlResult saveWorkflow(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	const QJsonArray steps = args.value(QStringLiteral("steps")).toArray();
	if (name.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a workflow needs a name")); }
	if (const QString why = stepsRefusal(steps); !why.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, why); }
	QJsonObject workflows = saved();
	const bool replaced = workflows.contains(name);
	if (!replaced && workflows.size() >= MaxWorkflows)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("at most %1 workflows are kept").arg(MaxWorkflows));
	}
	const QJsonObject previous = workflows.value(name).toObject();
	workflows.insert(name, QJsonObject{{QStringLiteral("steps"), steps}});
	const gui::uistore::Inverse inverse = replaced
		? gui::uistore::Inverse{QStringLiteral("workflow.save"), {{QStringLiteral("name"), name}, {QStringLiteral("steps"), previous.value(QStringLiteral("steps"))}}}
		: gui::uistore::Inverse{QStringLiteral("workflow.remove"), {{QStringLiteral("name"), name}}};
	return store(workflows, {{QStringLiteral("name"), name}, {QStringLiteral("steps"), steps.size()}, {QStringLiteral("replaced"), replaced}}, inverse);
}

ControlResult removeWorkflow(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	QJsonObject workflows = saved();
	if (!workflows.contains(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no workflow '%1'").arg(name)); }
	const QJsonValue steps = workflows.value(name).toObject().value(QStringLiteral("steps"));
	workflows.remove(name);
	return store(workflows, {{QStringLiteral("name"), name}, {QStringLiteral("count"), workflows.size()}},
		{QStringLiteral("workflow.save"), {{QStringLiteral("name"), name}, {QStringLiteral("steps"), steps}}});
}

ControlResult listWorkflows(const QJsonObject&)
{
	QJsonArray out;
	const QJsonObject workflows = saved();
	for (auto it = workflows.begin(); it != workflows.end(); ++it)
	{
		const QJsonArray steps = it.value().toObject().value(QStringLiteral("steps")).toArray();
		out.append(QJsonObject{{QStringLiteral("name"), it.key()}, {QStringLiteral("step_count"), steps.size()},
			{QStringLiteral("steps"), steps}});
	}
	return ControlResult::success({{QStringLiteral("workflows"), out}, {QStringLiteral("count"), out.size()},
		{QStringLiteral("max_workflows"), MaxWorkflows}, {QStringLiteral("max_steps"), MaxSteps}, {QStringLiteral("scope"), scopeName()}});
}

ControlResult setScope(const QJsonObject& args)
{
	const QString scope = args.value(QStringLiteral("scope")).toString();
	if (scope != SharedScope && scope != ProjectScope)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'scope' is shared or project"));
	}
	const QString previous = scopeName();
	QJsonObject result{{QStringLiteral("scope"), scope}, {QStringLiteral("previous"), previous}, {QStringLiteral("changed"), scope != previous}};
	if (scope == previous) { return ControlResult::success(result); }  // nothing changes: no undo step
	return gui::uistore::write(ScopeKey, {{QStringLiteral("scope"), scope}}, result,
		{QStringLiteral("workflow.set_scope"), {{QStringLiteral("scope"), previous}}}, refreshMenu);
}

//! A run that stopped at a failing step is said where the user can see it, not only in workflow.status.
void announceStop(const QString& name, const QJsonObject& failed, int at)
{
	gui::MainWindow* window = controlMainWindow();
	if (window == nullptr) { return; }
	gui::announce(window, gui::CommandCall{QStringLiteral("workflow.run"), {}, {}, name, false},
		ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("Workflow '%1' stopped at step %2 (%3): %4").arg(name)
			.arg(at + 1).arg(failed.value(QStringLiteral("command")).toString(), failed.value(QStringLiteral("error")).toString())));
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
	if (failedAt >= 0) { announceStop(name, results.last().toObject(), failedAt); }
}

ControlResult runWorkflow(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	const QJsonObject workflows = saved();
	if (!workflows.contains(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no workflow '%1'").arg(name)); }
	if (g_running) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a workflow is already running")); }
	// The steps run on the next turn of the event loop, each as a top-level command: run inside
	// this handler, the registry would fold every step's undo step into this command's while each
	// step kept a transaction record of its own, and control.undo would then disagree with Ctrl+Z.
	g_running = true;
	const QJsonArray steps = workflows.value(name).toObject().value(QStringLiteral("steps")).toArray();
	g_lastRun = {{QStringLiteral("name"), name}, {QStringLiteral("state"), QStringLiteral("running")}};
	QTimer::singleShot(0, ControlRegistry::instance(), [name, steps] { runSteps(name, steps); });
	return ControlResult::success({{QStringLiteral("name"), name}, {QStringLiteral("queued"), steps.size()},
		{QStringLiteral("poll"), QStringLiteral("workflow.status")}});
}

//! ED-056: the workflow being recorded - the commands the user (or an agent) runs until workflow.record_stop.
struct Recording
{
	bool on = false;
	QString name;
	QJsonArray steps;
	int dropped = 0;  //!< commands run after the steps were full: record_stop says how many
};

Recording& recording()
{
	static Recording value;
	return value;
}

//! workflow.status's `recording`: whether one is being recorded, and its name and steps so far.
QJsonObject recordingState()
{
	const Recording& now = recording();
	if (!now.on) { return {{QStringLiteral("on"), false}}; }
	return {{QStringLiteral("on"), true}, {QStringLiteral("name"), now.name}, {QStringLiteral("steps"), now.steps.size()},
		{QStringLiteral("dropped"), now.dropped}, {QStringLiteral("replaces"), saved().contains(now.name)}};
}

//! Theme 19: the recording state where it can be seen - the header's indicator and the menu's status line.
void showRecording()
{
	gui::MainWindow* window = controlMainWindow();
	if (window == nullptr) { return; }
	const Recording& now = recording();
	gui::MacroMenu::showRecording(window, now.on, now.name, static_cast<int>(now.steps.size()));
}

void recordStep(const QString& command, const QJsonObject& stepArgs)
{
	// Undo, redo and the workflow commands themselves are not steps; the step count is the workflow's own bound.
	if (command.startsWith(QStringLiteral("workflow.")) || command.startsWith(QStringLiteral("control."))) { return; }
	if (recording().steps.size() >= MaxSteps) { ++recording().dropped; return; }
	recording().steps.append(QJsonObject{{QStringLiteral("command"), command}, {QStringLiteral("args"), stepArgs}});
	showRecording();
}

ControlResult recordStart(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	if (name.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a workflow needs a name")); }
	if (recording().on)
	{
		// Starting over would drop the steps recorded so far.
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("workflow '%1' is being recorded; workflow.record_stop it first")
			.arg(recording().name));
	}
	recording() = Recording{true, name, {}, 0};
	ControlRegistry::instance()->setCommandObserver(recordStep);
	refreshMenu();
	showRecording();
	return ControlResult::success({{QStringLiteral("name"), name}, {QStringLiteral("recording"), true},
		{QStringLiteral("replaces"), saved().contains(name)}, {QStringLiteral("scope"), scopeName()}});
}

void stopRecording()
{
	ControlRegistry::instance()->setCommandObserver({});
	recording() = Recording{};
	refreshMenu();
	showRecording();
}

ControlResult recordStop(const QJsonObject&)
{
	const Recording taken = recording();
	if (!taken.on) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no workflow is being recorded")); }
	if (taken.steps.isEmpty())
	{
		stopRecording();
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("nothing was done while recording, so no workflow was saved"));
	}
	ControlResult kept = saveWorkflow({{QStringLiteral("name"), taken.name}, {QStringLiteral("steps"), taken.steps}});
	if (!kept.ok)
	{
		// A refused save (the workflow limit) keeps the recording, so the steps are not lost: make room and stop again.
		kept.errorMessage += QStringLiteral(" - still recording '%1', its %2 steps kept").arg(taken.name).arg(taken.steps.size());
		return kept;
	}
	stopRecording();
	if (taken.dropped > 0) { kept.result.insert(QStringLiteral("dropped"), taken.dropped); }
	return kept;
}

ControlResult workflowStatus(const QJsonObject&)
{
	QJsonObject out = g_lastRun;
	if (out.isEmpty()) { out.insert(QStringLiteral("state"), QStringLiteral("idle")); }
	out.insert(QStringLiteral("recording"), recordingState());
	return ControlResult::success(out);
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	const QJsonObject& result, bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd = guiCommand("workflow", verb);
	cmd.description = description;
	cmd.argsSchema = args;
	cmd.resultSchema = result;
	cmd.mutating = mutating;
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

void registerRecording(ControlRegistry& registry, const QJsonObject& named)
{
	add(registry, "record_start", QStringLiteral("Start recording workflow 'name': every command that changes something from now on "
		"(not undo, redo or the workflow commands) becomes one of its steps, with its arguments, until workflow.record_stop. "
		"Refused while another is being recorded. The recording shows in the header and the Workflows menu, and in "
		"workflow.status's 'recording'; 'replaces' says a saved workflow of that name will be replaced when it stops."),
		named, objectProperty(), false, recordStart);
	add(registry, "record_stop", QStringLiteral("Stop recording and save the workflow as workflow.save would (refused when nothing "
		"was done; a refused save keeps recording, nothing lost). Commands past the 64th step are not kept: 'dropped' counts them. "
		"One undo step."), objectSchema({}), objectProperty(), true, recordStop);
}

} // namespace

void registerMacroCommands(ControlRegistry& registry)
{
	ProjectWorkspace::instance();  // registers the section before any project loads
	const QJsonObject named = objectSchema({{QStringLiteral("name"), stringProperty()}}, {QStringLiteral("name")});
	add(registry, "save", QStringLiteral("Save a workflow: 'steps' is a list of {command, args} run in order by "
		"workflow.run (1..64 steps, every command registered, none a workflow.* one). Replaces a workflow of that name "
		"('replaced' says so); at most 64 are kept. The Workflows menu gains an action for it. Kept in the library in use "
		"('scope': shared, in the config file, or project, saved with the project - workflow.set_scope); one undo step."),
		objectSchema({{QStringLiteral("name"), stringProperty()}, {QStringLiteral("steps"), arrayProperty()}},
			{QStringLiteral("name"), QStringLiteral("steps")}),
		objectSchema({{QStringLiteral("name"), stringProperty()}, {QStringLiteral("steps"), integerProperty()},
			{QStringLiteral("replaced"), booleanProperty()}, {QStringLiteral("scope"), stringProperty()}}), true, saveWorkflow);
	add(registry, "run", QStringLiteral("Run workflow 'name': acknowledged at once ('queued' steps), then each step runs "
		"through the registry on the next turn of the event loop, in order, stopping at the first that fails - "
		"workflow.status reports the outcome ('stopped_at', -1 when every step ran) and the interface says where it stopped. "
		"Each step records and undoes as itself; this command records nothing of its own. One workflow runs at a time."), named,
		objectSchema({{QStringLiteral("name"), stringProperty()}, {QStringLiteral("queued"), integerProperty()},
			{QStringLiteral("poll"), stringProperty()}}), false, runWorkflow);
	add(registry, "status", QStringLiteral("The last workflow run: 'state' (idle, running, done), and once done how many "
		"steps ran, where it stopped and each step's outcome; 'recording' says whether a workflow is being recorded "
		"(its name and steps so far)."), objectSchema({}), objectProperty(), false, workflowStatus);
	add(registry, "list", QStringLiteral("The workflows in the library in use, with their steps, and which library that is "
		"('scope': shared or project)."), objectSchema({}),
		objectSchema({{QStringLiteral("workflows"), arrayProperty()}, {QStringLiteral("count"), integerProperty()},
			{QStringLiteral("max_workflows"), integerProperty()}, {QStringLiteral("max_steps"), integerProperty()},
			{QStringLiteral("scope"), stringProperty()}}), false, listWorkflows);
	registerRecording(registry, named);
	add(registry, "remove", QStringLiteral("Forget workflow 'name' from the library in use. One undo step."), named,
		objectSchema({{QStringLiteral("name"), stringProperty()}, {QStringLiteral("count"), integerProperty()},
			{QStringLiteral("scope"), stringProperty()}}), true, removeWorkflow);
	add(registry, "set_scope", QStringLiteral("Choose where workflows are kept: 'shared' (the default - one library in the "
		"config file, for every project) or 'project' (each project's own, saved with it). The other library is kept as it "
		"is, and comes back when it is chosen again. The ui/workflows.scope setting; one undo step ('changed' false, and no "
		"step, when it was already so)."), objectSchema({{QStringLiteral("scope"), enumProperty({SharedScope, ProjectScope})}},
		{QStringLiteral("scope")}), objectSchema({{QStringLiteral("scope"), stringProperty()}, {QStringLiteral("previous"), stringProperty()},
		{QStringLiteral("changed"), booleanProperty()}}), true, setScope);
}

} // namespace lmms
