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

#include <QKeySequence>
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
		return addCommandAction(menu, label, id, [id, args] { return CommandCall{id, args, {}, QString(), false}; });
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
	// ED-024: bound to keys by default (rebindable with shortcuts.set, like every menu action).
	run(QObject::tr("Nudge Clip Left"), QStringLiteral("arrangement.nudge"), {{QStringLiteral("direction"), QStringLiteral("left")}})
		->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
	run(QObject::tr("Nudge Clip Right"), QStringLiteral("arrangement.nudge"), {{QStringLiteral("direction"), QStringLiteral("right")}})
		->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
	run(QObject::tr("Next Clip"), QStringLiteral("arrangement.select_clip"), {{QStringLiteral("direction"), QStringLiteral("next")}})
		->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Down));
	run(QObject::tr("Previous Clip"), QStringLiteral("arrangement.select_clip"), {{QStringLiteral("direction"), QStringLiteral("previous")}})
		->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
	menu->addSeparator();
	run(QObject::tr("Add Marker at Play Position"), QStringLiteral("marker.add"), {});
	run(QObject::tr("Next Marker"), QStringLiteral("marker.jump"), {{QStringLiteral("direction"), QStringLiteral("next")}});
	run(QObject::tr("Previous Marker"), QStringLiteral("marker.jump"), {{QStringLiteral("direction"), QStringLiteral("previous")}});
	QAction* markers = menu->addAction(QObject::tr("Markers..."));
	markers->setProperty("controlCommand", QStringLiteral("marker.list"));
	QObject::connect(markers, &QAction::triggered, menu, [menu] { showCommandList(menu->parentWidget(), CommandLists::markers()); });
	menu->addSeparator();
	const auto onTools = [menu](const QString& label, const QString& id, const QString& key, const QJsonObject& fixed) {
		addCommandChoices(menu, label, id, key, [id, fixed] { return CommandCall{id, fixed, {}, QString(), false}; });
	};
	onTools(QObject::tr("Arranger Tool"), QStringLiteral("arrangement.set_tool"), QStringLiteral("tool"), {});
	onTools(QObject::tr("Ripple"), QStringLiteral("arrangement.modes"), QStringLiteral("ripple"), {});
	onTools(QObject::tr("Overlap"), QStringLiteral("arrangement.modes"), QStringLiteral("overlap"), {});
	onTools(QObject::tr("Snap to Clip Edges and Markers"), QStringLiteral("arrangement.modes"), QStringLiteral("snap_to_edges"), {});
	onTools(QObject::tr("Show Transients"), QStringLiteral("arrangement.modes"), QStringLiteral("show_transients"), {});
	onTools(QObject::tr("Show Navigator"), QStringLiteral("arrangement.navigator"), QStringLiteral("visible"), {});  // ED-053
	addCommandAction(menu, QObject::tr("Second Tool on a Modifier Key..."), QStringLiteral("arrangement.modifier_tools"), [] {  // ED-014
		return CommandCall{QStringLiteral("arrangement.modifier_tools"), {}, {}, QObject::tr("Second Tool on a Modifier Key"), true};
	});
	QMenu* autoFade = menu->addMenu(QObject::tr("Automatic Fades"));
	for (const int ms : {0, 2, 5, 10, 20})
	{
		addCommandAction(autoFade, ms == 0 ? QObject::tr("Off") : QObject::tr("%1 ms").arg(ms), QStringLiteral("arrangement.modes"),
			[ms] { return CommandCall{QStringLiteral("arrangement.modes"), {{QStringLiteral("auto_fade_ms"), ms}}, {}, QString(), false}; });
	}
	QMenu* presets = menu->addMenu(QObject::tr("Zoom Presets"));
	for (const int ppb : {16, 32, 64, 128, 256})
	{
		addCommandAction(presets, QObject::tr("%1 px per bar").arg(ppb), QStringLiteral("arrangement.zoom"),
			[ppb] { return CommandCall{QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), ppb}}, {}, QString(), false}; });
	}
	QMenu* range = menu->addMenu(QObject::tr("Range"));
	const auto onRange = [range](const QString& label, const QString& id, const QJsonObject& args) {
		addCommandAction(range, label, id, [id, args] { return CommandCall{id, args, {}, QString(), false}; });
	};
	onRange(QObject::tr("Split at Edges"), QStringLiteral("range.split"), {});
	onRange(QObject::tr("Delete"), QStringLiteral("range.delete"), {});
	onRange(QObject::tr("Fade From Range"), QStringLiteral("range.fade"), {});
	onRange(QObject::tr("Delete and Close Gap"), QStringLiteral("range.delete"), {{QStringLiteral("ripple"), true}});
	onRange(QObject::tr("Crop"), QStringLiteral("range.crop"), {});
	onRange(QObject::tr("Insert Time"), QStringLiteral("range.insert_time"), {});
	onRange(QObject::tr("Copy"), QStringLiteral("range.copy"), {});
	onRange(QObject::tr("Paste"), QStringLiteral("range.paste"), {});
	onRange(QObject::tr("Duplicate"), QStringLiteral("range.duplicate"), {});
	onRange(QObject::tr("Consolidate"), QStringLiteral("range.consolidate"), {});
	onRange(QObject::tr("Clear Range"), QStringLiteral("range.clear"), {});
	run(QObject::tr("Tool Menu at Pointer"), QStringLiteral("window.tool_menu"), {});
	return menu;
}

} // namespace lmms::gui
