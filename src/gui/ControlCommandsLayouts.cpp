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
 *  panel is interface state the shell persists itself, as window.set_view is. */

#include <QJsonArray>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "MainWindow.h"
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

ControlResult saveLayout(const QJsonObject& args)
{
	gui::Shell* s = shell();
	if (s == nullptr) { return noInterface(QStringLiteral("window.layout_save")); }
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	if (name.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a layout needs a name")); }
	QJsonObject layouts = savedLayouts();
	const bool replaced = layouts.contains(name);
	if (!replaced && layouts.size() >= MaxLayouts)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("at most %1 layouts are kept; remove one first").arg(MaxLayouts));
	}
	layouts.insert(name, s->snapshot());
	const gui::uistore::Inverse inverse = replaced
		? gui::uistore::Inverse{QStringLiteral("window.layout_save"), {{QStringLiteral("name"), name}}}
		: gui::uistore::Inverse{QStringLiteral("window.layout_remove"), {{QStringLiteral("name"), name}}};
	return storeLayouts(layouts, {{QStringLiteral("name"), name}, {QStringLiteral("replaced"), replaced},
		{QStringLiteral("count"), layouts.size()}}, inverse);
}

ControlResult applyLayout(const QJsonObject& args)
{
	gui::Shell* s = shell();
	if (s == nullptr) { return noInterface(QStringLiteral("window.layout_apply")); }
	const QString name = args.value(QStringLiteral("name")).toString();
	const QJsonObject layouts = savedLayouts();
	if (!layouts.contains(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no saved layout '%1'").arg(name)); }
	s->restoreSnapshot(layouts.value(name).toObject());
	QJsonObject result = s->state();
	result.insert(QStringLiteral("name"), name);
	return ControlResult::success(result);
}

ControlResult listLayouts(const QJsonObject&)
{
	return ControlResult::success({{QStringLiteral("layouts"), QJsonArray::fromStringList(savedLayouts().keys())},
		{QStringLiteral("max_layouts"), MaxLayouts}});
}

ControlResult removeLayout(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	QJsonObject layouts = savedLayouts();
	if (!layouts.contains(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no saved layout '%1'").arg(name)); }
	layouts.remove(name);
	return storeLayouts(layouts, {{QStringLiteral("name"), name}, {QStringLiteral("count"), layouts.size()}},
		{QStringLiteral("window.layout_save"), {{QStringLiteral("name"), name}}});
}

ControlResult movePanel(const QJsonObject& args, bool detach)
{
	gui::Shell* s = shell();
	const QString id = detach ? QStringLiteral("window.detach") : QStringLiteral("window.attach");
	if (s == nullptr) { return noInterface(id); }
	const QString panel = args.value(QStringLiteral("panel")).toString();
	if (!gui::ShellLayout::isPanel(panel)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no panel '%1'").arg(panel)); }
	QString reason;
	const bool done = detach ? s->detachPanel(panel, &reason) : s->attachPanel(panel);
	if (detach && !done) { return ControlResult::failure(ControlErrorKind::Refused, reason); }
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
		if (!ControlRegistry::instance()->hasCommand(command))
		{
			return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no command '%1' for a header button").arg(command));
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

void registerLayoutCommands(ControlRegistry& registry)
{
	const QJsonObject named = objectSchema({{QStringLiteral("name"), stringProperty()}}, {QStringLiteral("name")});
	const QJsonObject panel = objectSchema({{QStringLiteral("panel"), stringProperty()}}, {QStringLiteral("panel")});
	add(registry, "layout_save", QStringLiteral("Save the window layout - the view, the open panels and the "
		"splitter sizes - under 'name' (replacing one of that name). At most 32 are kept, in the config file; "
		"one undo step."), named, objectSchema({{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("replaced"), booleanProperty()}, {QStringLiteral("count"), integerProperty()}}), true, saveLayout);
	add(registry, "layout_apply", QStringLiteral("Switch to the saved layout 'name'; answers window.get_state's view "
		"of the result. Interface state, as window.set_view is: not recorded."), named, objectProperty(), false, applyLayout);
	add(registry, "layout_list", QStringLiteral("The saved layouts' names."), objectSchema({}),
		objectSchema({{QStringLiteral("layouts"), arrayProperty()}, {QStringLiteral("max_layouts"), integerProperty()}}),
		false, listLayouts);
	add(registry, "layout_remove", QStringLiteral("Forget the saved layout 'name'. One undo step."), named,
		objectSchema({{QStringLiteral("name"), stringProperty()}, {QStringLiteral("count"), integerProperty()}}), true, removeLayout);
	const QJsonObject moved = objectSchema({{QStringLiteral("panel"), stringProperty()},
		{QStringLiteral("floating"), booleanProperty()}, {QStringLiteral("changed"), booleanProperty()}});
	add(registry, "detach", QStringLiteral("Show shell panel 'panel' (window.get_state's panel ids) in a window of its "
		"own - one that can sit on a second monitor; it stays there whatever view is chosen until window.attach or "
		"its window is closed. Interface state: not recorded."), panel, moved, false,
		[](const QJsonObject& args) { return movePanel(args, true); });
	add(registry, "attach", QStringLiteral("Put a panel shown in its own window (window.detach) back in the shell."),
		panel, moved, false, [](const QJsonObject& args) { return movePanel(args, false); });
	add(registry, "header_get", QStringLiteral("The header's buttons - name, text, command, shown or not - and the "
		"user's choices (hidden names, extra command buttons)."), objectSchema({}), objectProperty(), false, headerState);
	add(registry, "header_set", QStringLiteral("Customise the header: 'hidden' lists button names to hide (the rest "
		"show), 'extra' adds buttons that run registry commands ([{command, label}]). A field not given is kept. "
		"Kept in the config file; one undo step."), objectSchema({{QStringLiteral("hidden"), arrayProperty()},
		{QStringLiteral("extra"), arrayProperty()}}), objectProperty(), true, setHeader);
}

} // namespace lmms
