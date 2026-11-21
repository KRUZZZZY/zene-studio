/*
 * ShellToolsMenu.cpp - the footer's Tools home: every tool that opens in a window of its own
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

#include "ShellToolsMenu.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QMenu>
#include <QSet>

#include "Accessibility.h"
#include "CommandForm.h"
#include "ControlRegistry.h"
#include "ShellChrome.h"
#include "ShellIcons.h"

namespace lmms::gui::shelltools
{

namespace
{

QString tr(const char* text) { return QCoreApplication::translate("ShellTools", text); }

//! The window.toggle editors whose window shows now (none without an interface).
QSet<QString> showingEditors()
{
	QSet<QString> out;
	const QJsonArray editors = ControlRegistry::instance()->invoke(QStringLiteral("window.get_state")).result
		.value(QStringLiteral("editors")).toArray();
	for (const QJsonValue& value : editors)
	{
		const QJsonObject entry = value.toObject();
		if (entry.value(QStringLiteral("visible")).toBool()) { out.insert(entry.value(QStringLiteral("editor")).toString()); }
	}
	return out;
}

CommandCall callFor(const Tool& tool)
{
	const QJsonObject fixed = tool.editor.isEmpty() ? QJsonObject() : QJsonObject{{QStringLiteral("editor"), tool.editor}};
	return CommandCall{tool.command, fixed, {}, QString(), false};
}

} // namespace

const QList<Tool>& tools()
{
	static const QList<Tool> list{
		{tr("Controller Rack"), QStringLiteral("window.toggle"), QStringLiteral("controller_rack")},
		{tr("Project Notes"), QStringLiteral("window.toggle"), QStringLiteral("project_notes")},
		{tr("Scales and Keymaps"), QStringLiteral("window.toggle"), QStringLiteral("microtuner")},
		{tr("Classic Piano Roll"), QStringLiteral("window.toggle"), QStringLiteral("classic_piano_roll")},
		{tr("Undo History"), QStringLiteral("window.undo_history"), QString()},
		{tr("Tempo Map"), QStringLiteral("window.tempo_map"), QString()},
		{tr("Live Code"), QStringLiteral("window.livecode"), QString()},
		{tr("Modulators"), QStringLiteral("window.modulators"), QString()},
		{tr("Progression"), QStringLiteral("window.progression"), QString()},
		{tr("Large Clock"), QStringLiteral("window.large_clock"), QString()},
		{tr("Tuner"), QStringLiteral("window.tuner"), QString()},
		{tr("Video"), QStringLiteral("video.window_show"), QString()},
		{tr("Collaboration"), QStringLiteral("collab.show_panel"), QString()},
	};
	return list;
}

QJsonArray state()
{
	QJsonArray out;
	for (const Tool& tool : tools())
	{
		out.append(QJsonObject{{QStringLiteral("name"), tool.name}, {QStringLiteral("command"), tool.command},
			{QStringLiteral("editor"), tool.editor}, {QStringLiteral("home"), QStringLiteral("window")}});
	}
	return out;
}

void populate(QMenu* menu)
{
	menu->clear();
	const QSet<QString> showing = showingEditors();
	bool toggling = true;
	for (const Tool& tool : tools())
	{
		if (toggling && tool.editor.isEmpty())
		{
			menu->addSeparator();  // the toggling windows first, then the ones opened by their own command
			toggling = false;
		}
		QAction* action = addCommandAction(menu, tool.name, tool.command, [tool] { return callFor(tool); });
		action->setCheckable(!tool.editor.isEmpty());
		action->setChecked(showing.contains(tool.editor));
	}
}

ShellButton* makeButton(QWidget* parent)
{
	auto* button = new ShellButton(parent);
	button->setObjectName(QStringLiteral("footerTools"));
	button->setIcon(shellicons::icon(QStringLiteral("float")));
	button->setText(tr("Tools"));
	button->setToolTip(tr("Tools that open in a window of their own"));
	a11y::announce(button, tr("Tools"), tr("Every tool that opens in a window of its own: the controller rack, project "
		"notes, scales and keymaps, the undo history, the tempo map and more."));
	button->setPopupMode(QToolButton::InstantPopup);
	auto* menu = new QMenu(button);
	QObject::connect(menu, &QMenu::aboutToShow, menu, [menu] { populate(menu); });
	button->setMenu(menu);
	return button;
}

} // namespace lmms::gui::shelltools
