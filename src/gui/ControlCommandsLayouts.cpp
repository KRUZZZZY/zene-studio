/*
 * ControlCommandsLayouts.cpp - saved layouts, panels in their own windows, the user's header
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

/*! 0.5.0 L13: WF-091 saved window layouts (window.layout_*), WF-092 a panel in a window of its
 *  own for a second monitor (window.detach / window.attach) and WF-116 the header's buttons
 *  (window.header_get / window.header_set). Layouts and the header are interface state kept
 *  in the config file (UiConfigStore: one undo step per write); applying a layout or moving a
 *  panel is interface state the shell persists itself, as window.set_view is.
 *  Design theme 22: the saved layouts are the user's personal library, and besides it each project
 *  can remember a layout of its own (window.layout_project_*, <workspace>: project state, one undo
 *  step per write), put back when the project opens (LayoutMenu::recallProjectLayout). A save says
 *  what it kept ('captures') and can refuse to replace ('replace' false: the menu's Save never
 *  overwrites silently, its Replace does it by name). */

#include <QGuiApplication>
#include <QJsonArray>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "MainWindow.h"
#include "ProjectWorkspace.h"
#include "Shell.h"
#include "ShellChrome.h"
#include "UiConfigStore.h"

namespace lmms
{

using namespace control;

namespace
{

const QString LayoutsKey = QStringLiteral("shell.layouts");
const QString HeaderKey = QStringLiteral("header");
constexpr int MaxLayouts = 32;

gui::Shell* shell()
{
	gui::MainWindow* window = controlMainWindow();
	return window != nullptr ? window->shell() : nullptr;
}

QJsonObject savedLayouts()
{
	return gui::uistore::read(LayoutsKey).value(QStringLiteral("layouts")).toObject();
}

ControlResult storeLayouts(const QJsonObject& layouts, QJsonObject result, const gui::uistore::Inverse& inverse)
{
	return gui::uistore::write(LayoutsKey, {{QStringLiteral("layouts"), layouts}}, std::move(result), inverse, {});
}

//! Theme 22: what a saved layout holds, by the snapshot's own parts (view, panels, sizes, floating...).
QJsonArray capturesOf(const QJsonObject& snapshot)
{
	return QJsonArray::fromStringList(snapshot.keys());
}

//! Why @a name cannot be saved into @a layouts now, or empty: the limit, or a name in use with 'replace' false.
QString saveRefusal(const QJsonObject& layouts, const QString& name, bool replace)
{
	if (layouts.contains(name))
	{
		return replace ? QString() : QStringLiteral("a layout named '%1' is saved already: choose another name, or replace it "
			"(Window Layouts > Replace)").arg(name);
	}
	return layouts.size() >= MaxLayouts ? QStringLiteral("at most %1 layouts are kept; remove one first").arg(MaxLayouts) : QString();
}

ControlResult saveLayout(const QJsonObject& args)
{
	gui::Shell* s = shell();
	if (s == nullptr) { return noInterface(QStringLiteral("window.layout_save")); }
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	if (name.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a layout needs a name")); }
	QJsonObject layouts = savedLayouts();
	if (const QString why = saveRefusal(layouts, name, args.value(QStringLiteral("replace")).toBool(true)); !why.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Refused, why);
	}
	const bool replaced = layouts.contains(name);
	const QJsonObject snapshot = s->snapshot();
	layouts.insert(name, snapshot);
	// A replaced layout comes back through the recorded step (control.undo): layout_save would save the
	// layout on screen then, not the one replaced.
	const gui::uistore::Inverse inverse = replaced
		? gui::uistore::Inverse{QStringLiteral("control.undo"), {}}
		: gui::uistore::Inverse{QStringLiteral("window.layout_remove"), {{QStringLiteral("name"), name}}};
	return storeLayouts(layouts, {{QStringLiteral("name"), name}, {QStringLiteral("replaced"), replaced},
		{QStringLiteral("count"), layouts.size()}, {QStringLiteral("max_layouts"), MaxLayouts},
		{QStringLiteral("captures"), capturesOf(snapshot)}}, inverse);
}

//! Put @a snapshot on screen; window.get_state's view of the result plus @a extra.
ControlResult showSnapshot(gui::Shell* s, const QJsonObject& snapshot, const QJsonObject& extra)
{
	s->restoreSnapshot(snapshot);
	QJsonObject result = s->state();
	for (auto it = extra.begin(); it != extra.end(); ++it) { result.insert(it.key(), it.value()); }
	return ControlResult::success(result);
}

ControlResult applyLayout(const QJsonObject& args)
{
	gui::Shell* s = shell();
	if (s == nullptr) { return noInterface(QStringLiteral("window.layout_apply")); }
	const QString name = args.value(QStringLiteral("name")).toString();
	const QJsonObject layouts = savedLayouts();
	if (!layouts.contains(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no saved layout '%1'").arg(name)); }
	return showSnapshot(s, layouts.value(name).toObject(), {{QStringLiteral("name"), name}});
}

QJsonObject projectLayout()
{
	return ProjectWorkspace::instance().part(ProjectWorkspace::Part::Layout);
}

ControlResult listLayouts(const QJsonObject&)
{
	return ControlResult::success({{QStringLiteral("layouts"), QJsonArray::fromStringList(savedLayouts().keys())},
		{QStringLiteral("max_layouts"), MaxLayouts}, {QStringLiteral("project_layout"), !projectLayout().isEmpty()}});
}

//! window.layout_project_save: the layout on screen, remembered with the project.
ControlResult rememberLayout(const QJsonObject&)
{
	gui::Shell* s = shell();
	if (s == nullptr) { return noInterface(QStringLiteral("window.layout_project_save")); }
	const QJsonObject snapshot = s->snapshot();
	const bool replaced = !projectLayout().isEmpty();
	QJsonObject result{{QStringLiteral("replaced"), replaced}, {QStringLiteral("captures"), capturesOf(snapshot)},
		{QStringLiteral("changed"), snapshot != projectLayout()}};
	if (snapshot == projectLayout()) { return ControlResult::success(result); }  // already remembered just so: no undo step
	return gui::uistore::writeProject(ProjectWorkspace::Part::Layout, snapshot, result, replaced
		? gui::uistore::Inverse{QStringLiteral("control.undo"), {}} : gui::uistore::Inverse{QStringLiteral("window.layout_project_clear"), {}}, {});
}

ControlResult recallLayout(const QJsonObject&)
{
	gui::Shell* s = shell();
	if (s == nullptr) { return noInterface(QStringLiteral("window.layout_project_apply")); }
	if (projectLayout().isEmpty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("this project remembers no layout")); }
	return showSnapshot(s, projectLayout(), {{QStringLiteral("project"), true}});
}

ControlResult forgetLayout(const QJsonObject&)
{
	if (projectLayout().isEmpty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("this project remembers no layout")); }
	return gui::uistore::writeProject(ProjectWorkspace::Part::Layout, {}, {{QStringLiteral("cleared"), true}},
		{QStringLiteral("control.undo"), {}}, {});  // window.layout_project_save would remember the layout on screen, not this one
}

ControlResult removeLayout(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	QJsonObject layouts = savedLayouts();
	if (!layouts.contains(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no saved layout '%1'").arg(name)); }
	layouts.remove(name);
	return storeLayouts(layouts, {{QStringLiteral("name"), name}, {QStringLiteral("count"), layouts.size()}},
		{QStringLiteral("control.undo"), {}});  // as a replace: layout_save could not bring the removed snapshot back
}

//! The refusal of a panel id that is none, naming the ids there are (window.get_state's).
ControlResult unknownPanel(const QString& panel)
{
	QStringList ids;
	for (const gui::ShellPanelSpec& spec : gui::ShellLayout::panels()) { ids << spec.id; }
	return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no panel '%1' (%2)").arg(panel, ids.join(QStringLiteral(", "))));
}

//! window.detach's 'screen' (-1 when not given); @a refusal is set when it names no connected screen.
int screenArg(const QJsonObject& args, QString& refusal)
{
	if (!args.contains(QStringLiteral("screen"))) { return -1; }
	const int screen = args.value(QStringLiteral("screen")).toInt(-1);
	const int count = static_cast<int>(QGuiApplication::screens().size());
	if (screen < 0 || screen >= count) { refusal = QStringLiteral("no screen %1: there are %2").arg(screen).arg(count); }
	return screen;
}

//! Float @a panel (a panel already floating stays so) and, with @a screen, move its window there; false with @a reason
//! when it could not float.
bool detachTo(gui::Shell* s, const QString& panel, int screen, bool* changed, QString* reason)
{
	*changed = s->detachPanel(panel, reason);
	if (!*changed && !s->isDetached(panel)) { return false; }
	if (screen >= 0) { s->moveFloatingToScreen(panel, screen); }
	return true;
}

ControlResult movePanel(const QJsonObject& args, bool detach)
{
	gui::Shell* s = shell();
	const QString id = detach ? QStringLiteral("window.detach") : QStringLiteral("window.attach");
	if (s == nullptr) { return noInterface(id); }
	const QString panel = args.value(QStringLiteral("panel")).toString();
	if (!gui::ShellLayout::isPanel(panel)) { return unknownPanel(panel); }
	QString refusal;
	const int screen = detach ? screenArg(args, refusal) : -1;
	if (!refusal.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, refusal); }
	QString reason;
	bool done = false;
	if (!detach) { done = s->attachPanel(panel); }
	else if (!detachTo(s, panel, screen, &done, &reason)) { return ControlResult::failure(ControlErrorKind::Refused, reason); }
	return ControlResult::success({{QStringLiteral("panel"), panel}, {QStringLiteral("floating"), s->isDetached(panel)},
		{QStringLiteral("changed"), done}});
}

ControlResult headerState(const QJsonObject&)
{
	gui::Shell* s = shell();
	if (s == nullptr) { return noInterface(QStringLiteral("window.header_get")); }
	QJsonObject result = gui::uistore::read(HeaderKey);
	result.insert(QStringLiteral("buttons"), s->header()->buttons());
	return ControlResult::success(result);
}

ControlResult setHeader(const QJsonObject& args)
{
	gui::Shell* s = shell();
	if (s == nullptr) { return noInterface(QStringLiteral("window.header_set")); }
	for (const QJsonValue& value : args.value(QStringLiteral("extra")).toArray())
	{
		const QString command = value.toObject().value(QStringLiteral("command")).toString();
		const ControlCommand* found = ControlRegistry::instance()->command(command);
		if (found == nullptr)
		{
			return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no command '%1' for a header button").arg(command));
		}
		// A click runs the command with no arguments (dispatchShellCommand), so one that needs some would only ever fail.
		if (!found->argsSchema.value(QStringLiteral("required")).toArray().isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1' needs arguments, and a header button "
				"runs its command with none").arg(command));
		}
	}
	const QJsonObject previous = gui::uistore::read(HeaderKey);
	QJsonObject doc = previous;
	for (const char* key : {"hidden", "extra"})
	{
		if (args.contains(QLatin1String(key))) { doc.insert(QLatin1String(key), args.value(QLatin1String(key))); }
	}
	QPointer<gui::Shell> guarded(s);
	return gui::uistore::write(HeaderKey, doc, doc,
		{QStringLiteral("window.header_set"), {{QStringLiteral("hidden"), previous.value(QStringLiteral("hidden")).toArray()},
			{QStringLiteral("extra"), previous.value(QStringLiteral("extra")).toArray()}}},
		[guarded] { if (!guarded.isNull()) { guarded->header()->customise(gui::uistore::read(HeaderKey)); } });
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	const QJsonObject& result, bool mutating, std::function<ControlResult(const QJsonObject&)> handler)
{
	ControlCommand cmd = guiCommand("window", verb);
	cmd.description = description;
	cmd.argsSchema = args;
	cmd.resultSchema = result;
	cmd.mutating = mutating;
	cmd.handler = std::move(handler);
	registry.registerCommand(cmd);
}

} // namespace

//! Theme 22: the layout each project remembers, beside the personal library.
void registerProjectLayoutCommands(ControlRegistry& registry)
{
	ProjectWorkspace::instance();  // registers the section before any project loads
	add(registry, "layout_project_save", QStringLiteral("Remember the window layout on screen with the project (the same parts "
		"window.layout_save keeps: 'captures' lists them), replacing the one it remembered ('replaced'). Saved with the project "
		"and put back when it opens; one undo step ('changed' false, and no step, when it remembered just this one)."),
		objectSchema({}), objectSchema({{QStringLiteral("replaced"), booleanProperty()}, {QStringLiteral("captures"), arrayProperty()},
		{QStringLiteral("changed"), booleanProperty()}}), true, rememberLayout);
	add(registry, "layout_project_apply", QStringLiteral("Put back the layout the project remembers; answers window.get_state's "
		"view of the result. Interface state: not recorded. Refused NotFound when it remembers none."), objectSchema({}),
		objectProperty(), false, recallLayout);
	add(registry, "layout_project_clear", QStringLiteral("Forget the layout the project remembers (the personal layouts are "
		"kept). One undo step; refused NotFound when it remembers none."), objectSchema({}),
		objectSchema({{QStringLiteral("cleared"), booleanProperty()}}), true, forgetLayout);
}

void registerLayoutCommands(ControlRegistry& registry)
{
	const QJsonObject named = objectSchema({{QStringLiteral("name"), stringProperty()}}, {QStringLiteral("name")});
	const QJsonObject panel = objectSchema({{QStringLiteral("panel"), stringProperty()}}, {QStringLiteral("panel")});
	add(registry, "layout_save", QStringLiteral("Save the window layout - the view, the open, pinned and floating panels and "
		"the splitter sizes ('captures' lists the parts kept) - under 'name' in the personal library, replacing one of that "
		"name ('replaced') unless 'replace' is false, when a name in use is refused. At most 32 are kept ('max_layouts'), in "
		"the config file; one undo step."), objectSchema({{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("replace"), booleanProperty()}}, {QStringLiteral("name")}), objectSchema({{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("replaced"), booleanProperty()}, {QStringLiteral("count"), integerProperty()},
		{QStringLiteral("max_layouts"), integerProperty()}, {QStringLiteral("captures"), arrayProperty()}}), true, saveLayout);
	add(registry, "layout_apply", QStringLiteral("Switch to the saved layout 'name'; answers window.get_state's view "
		"of the result. Interface state, as window.set_view is: not recorded."), named, objectProperty(), false, applyLayout);
	add(registry, "layout_list", QStringLiteral("The personal library's layout names, its limit, and whether the project "
		"remembers a layout of its own ('project_layout', window.layout_project_*)."), objectSchema({}),
		objectSchema({{QStringLiteral("layouts"), arrayProperty()}, {QStringLiteral("max_layouts"), integerProperty()},
		{QStringLiteral("project_layout"), booleanProperty()}}), false, listLayouts);
	add(registry, "layout_remove", QStringLiteral("Forget the saved layout 'name'. One undo step."), named,
		objectSchema({{QStringLiteral("name"), stringProperty()}, {QStringLiteral("count"), integerProperty()}}), true, removeLayout);
	registerProjectLayoutCommands(registry);
	const QJsonObject moved = objectSchema({{QStringLiteral("panel"), stringProperty()},
		{QStringLiteral("floating"), booleanProperty()}, {QStringLiteral("changed"), booleanProperty()}});
	add(registry, "detach", QStringLiteral("Show shell panel 'panel' (window.get_state's panel ids) in a window of its "
		"own - one that can sit on a second monitor; it stays there whatever view is chosen until window.attach or "
		"its window is closed. With 'screen' (an index into the connected screens, 0 = the first) its window moves "
		"onto that screen, also when it already floats. Interface state: not recorded."),
		objectSchema({{QStringLiteral("panel"), stringProperty()}, {QStringLiteral("screen"), integerProperty()}}, {QStringLiteral("panel")}),
		moved, false,
		[](const QJsonObject& args) { return movePanel(args, true); });
	add(registry, "attach", QStringLiteral("Put a panel shown in its own window (window.detach) back in the shell."),
		panel, moved, false, [](const QJsonObject& args) { return movePanel(args, false); });
	add(registry, "header_get", QStringLiteral("The header's buttons - name, text, command, shown or not - and the "
		"user's choices (hidden names, extra command buttons)."), objectSchema({}), objectProperty(), false, headerState);
	add(registry, "header_set", QStringLiteral("Customise the header: 'hidden' lists button names to hide (the rest "
		"show), 'extra' adds buttons that run registry commands ([{command, label}], commands that need no arguments). A field not given is kept. "
		"Kept in the config file; one undo step."), objectSchema({{QStringLiteral("hidden"), arrayProperty()},
		{QStringLiteral("extra"), arrayProperty()}}), objectProperty(), true, setHeader);
}

} // namespace lmms
