/*
 * MenuBarExtras.cpp - the menu bar's own Zene menus: Navigate and Macros
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

#include "MenuBarExtras.h"

#include <QMenu>
#include <QMenuBar>

#include "CommandForm.h"
#include "CommandLists.h"

namespace lmms::gui
{

QMenu* insertBeforeHelp(QMenuBar* bar, QMenu* menu)
{
	if (auto* existing = bar->window()->findChild<QMenu*>(menu->objectName()); existing != nullptr && existing != menu)
	{
		delete menu;
		return existing;
	}
	QAction* help = bar->actions().isEmpty() ? nullptr : bar->actions().last();
	bar->insertMenu(help, menu);
	return menu;
}

QMenu* installNavigateMenu(QMenuBar* bar)
{
	auto* menu = new QMenu(QObject::tr("&Navigate"), bar->window());
	menu->setObjectName(QStringLiteral("navigateMenu"));
	if (QMenu* existing = insertBeforeHelp(bar, menu); existing != menu) { return existing; }
	const auto run = [menu](const QString& label, const QString& id, const QJsonObject& args) {
		addCommandAction(menu, label, id, [id, args] { return CommandCall{id, args, {}, QString(), false}; });
	};
	run(QObject::tr("Zoom Back"), QStringLiteral("arrangement.zoom_back"), {});
	run(QObject::tr("Zoom In"), QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), 4}});
	run(QObject::tr("Zoom Out"), QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), -4}});
	menu->addSeparator();
	run(QObject::tr("Fit Song"), QStringLiteral("arrangement.zoom"), {{QStringLiteral("fit"), QStringLiteral("song")}});
	run(QObject::tr("Fit Loop"), QStringLiteral("arrangement.zoom"), {{QStringLiteral("fit"), QStringLiteral("loop")}});
	run(QObject::tr("Fit Selection"), QStringLiteral("arrangement.zoom"), {{QStringLiteral("fit"), QStringLiteral("selection")}});
	run(QObject::tr("Fit Tracks"), QStringLiteral("arrangement.fit_tracks"), {});
	menu->addSeparator();
	run(QObject::tr("Add Marker at Play Position"), QStringLiteral("marker.add"), {});
	run(QObject::tr("Next Marker"), QStringLiteral("marker.jump"), {{QStringLiteral("direction"), QStringLiteral("next")}});
	run(QObject::tr("Previous Marker"), QStringLiteral("marker.jump"), {{QStringLiteral("direction"), QStringLiteral("previous")}});
	QAction* markers = menu->addAction(QObject::tr("Markers..."));
	markers->setProperty("controlCommand", QStringLiteral("marker.list"));
	QObject::connect(markers, &QAction::triggered, menu, [menu] { showCommandList(menu->parentWidget(), CommandLists::markers()); });
	menu->addSeparator();
	run(QObject::tr("Range Tool"), QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("range")}});
	QMenu* range = menu->addMenu(QObject::tr("Range"));
	const auto onRange = [range](const QString& label, const QString& id, const QJsonObject& args) {
		addCommandAction(range, label, id, [id, args] { return CommandCall{id, args, {}, QString(), false}; });
	};
	onRange(QObject::tr("Split at Edges"), QStringLiteral("range.split"), {});
	onRange(QObject::tr("Delete"), QStringLiteral("range.delete"), {});
	onRange(QObject::tr("Delete and Close Gap"), QStringLiteral("range.delete"), {{QStringLiteral("ripple"), true}});
	onRange(QObject::tr("Crop"), QStringLiteral("range.crop"), {});
	onRange(QObject::tr("Insert Time"), QStringLiteral("range.insert_time"), {});
	onRange(QObject::tr("Copy"), QStringLiteral("range.copy"), {});
	onRange(QObject::tr("Paste"), QStringLiteral("range.paste"), {});
	onRange(QObject::tr("Duplicate"), QStringLiteral("range.duplicate"), {});
	onRange(QObject::tr("Clear Range"), QStringLiteral("range.clear"), {});
	run(QObject::tr("Tool Menu at Pointer"), QStringLiteral("window.tool_menu"), {});
	return menu;
}

} // namespace lmms::gui
