/*
 * ControlCommandsHelp.cpp - app.help, app.tours, app.tour and window.appearance
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
/*! 0.5.0 L14: help and access on the spine.
 *  - app.help (WF-136) opens the manual - at a topic - or the command reference (HelpCenter.h);
 *  - app.tours / app.tour (WF-140) list the guided tours and start one;
 *  - window.appearance (WF-130 / WF-131, W3 WF-118 / WF-134) sets the interface scale, the text size, the
 *    palette and the layout density (Appearance.h), from the next start; a change is one undo step, as
 *    settings.set's. */

#include <QJsonArray>
#include <QWhatsThis>

#include "Appearance.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "GuiApplication.h"
#include "HelpCenter.h"
#include "MainWindow.h"

namespace lmms
{

using namespace control;

namespace
{

ControlResult help(const QJsonObject& args)
{
	if (args.value(QStringLiteral("whats_this")).toBool())
	{
		QWhatsThis::enterWhatsThisMode();  // 0.5.0 L14: the next click on a control shows its full description
		return ControlResult::success({{QStringLiteral("whats_this"), true}});
	}
	gui::HelpCenter::showHelp(args.value(QStringLiteral("topic")).toString(), args.value(QStringLiteral("reference")).toBool());
	return ControlResult::success({{QStringLiteral("open"), true}});
}

ControlResult tours(const QJsonObject&) { return ControlResult::success({{QStringLiteral("tours"), gui::GuidedTour::tours()}}); }

ControlResult tour(const QJsonObject& args)
{
	QString error;
	QWidget* window = gui::getGUI() != nullptr ? gui::getGUI()->mainWindow() : nullptr;
	gui::GuidedTour* started = gui::GuidedTour::start(args.value(QStringLiteral("name")).toString(), window, &error);
	if (started == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, error); }
	return ControlResult::success({{QStringLiteral("name"), args.value(QStringLiteral("name"))}, {QStringLiteral("steps"), started->stepCount()}});
}

ControlResult appearanceCommand(const QJsonObject& args)
{
	const appearance::Options before = appearance::options();
	appearance::Options after = before;
	after.scale = args.value(QStringLiteral("scale")).toDouble(after.scale);
	after.fontScale = args.value(QStringLiteral("font_scale")).toDouble(after.fontScale);
	after.palette = args.value(QStringLiteral("palette")).toString(after.palette);
	after.density = args.value(QStringLiteral("density")).toString(after.density);
	const bool changed = after.scale != before.scale || after.fontScale != before.fontScale || after.palette != before.palette
		|| after.density != before.density;
	if (changed)
	{
		appearance::setOptions(after);
		addUndoStep([before]() { appearance::setOptions(before); }, [after]() { appearance::setOptions(after); });
	}
	return ControlResult::success(appearance::toJson(appearance::options()));
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, bool mutating, bool display,
	ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd;
	cmd.id = QLatin1String(id);
	const int dot = cmd.id.indexOf(QLatin1Char('.'));
	cmd.group = cmd.id.left(dot);
	cmd.verb = cmd.id.mid(dot + 1);
	cmd.description = description;
	cmd.argsSchema = args;
	cmd.resultSchema = objectProperty();
	cmd.mutating = mutating;
	cmd.requiresEngine = false;
	if (display) { cmd.requiresDecl = QStringList{QStringLiteral("display")}; }
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void registerHelpCommands(ControlRegistry& registry)
{
	add(registry, "app.help", QStringLiteral("Open the manual inside the application - at 'topic' (window, arrange, notes, mix, "
		"devices, record, launch, projects, agents, customise, access) - or, with 'reference', the command reference: every command with "
		"its description and arguments, built from the registry - or, with 'whats_this', What's This? mode: the next click on a "
		"control shows its full description. Interface state."),
		objectSchema({{QStringLiteral("topic"), stringProperty()}, {QStringLiteral("reference"), booleanProperty()},
			{QStringLiteral("whats_this"), booleanProperty()}}), false, true, help);
	add(registry, "app.tours", QStringLiteral("The guided tours there are: each one's name, title and step count."),
		objectSchema({}), false, false, tours);
	add(registry, "app.tour", QStringLiteral("Start a guided tour ('name', as app.tours lists it): each step points at a part of the "
		"window - showing its panel first when it is hidden - with Back, Next and Close. Interface state."),
		objectSchema({{QStringLiteral("name"), stringProperty()}}, {QStringLiteral("name")}), false, true, tour);
	add(registry, "window.appearance", QStringLiteral("How the interface looks, from the next start: 'scale' (0.5..3, the whole "
		"interface; an explicit QT_SCALE_FACTOR still wins), 'font_scale' (0.75..2, the text size), 'palette' (dark, "
		"high_contrast - text at 7:1 on every surface - or light) and 'density' (standard, comfortable - more room around "
		"every control, fewer on screen - or touch, targets large enough for a finger). A field left out keeps its value; "
		"none reads them. A preference in the config file; a change is one undo step."),
		objectSchema({{QStringLiteral("scale"), numberProperty()}, {QStringLiteral("font_scale"), numberProperty()},
			{QStringLiteral("palette"), enumProperty(appearance::paletteNames())},
			{QStringLiteral("density"), enumProperty(appearance::densityNames())}}), true, false, appearanceCommand);
}

} // namespace lmms
