/*
 * WorkspaceMenus.cpp - the 0.5.0 W3 customisation and access entries in the menus
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

#include "WorkspaceMenus.h"

#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>

#include "CommandForm.h"
#include "CommandLists.h"
#include "ControlRegistry.h"
#include "MenuBarExtras.h"
#include "TrackIcons.h"
#include "TrackLooks.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

//! An entry running @a id with @a args - through its form when @a ask, showing the reply when @a show.
QAction* run(QMenu* menu, const QString& label, const QString& id, const QJsonObject& args = {}, bool ask = false, bool show = false)
{
	return addCommandAction(menu, label, id, [id, args, ask, show] { return CommandCall{id, args, {}, QString(), ask, show}; });
}

//! An entry opening the list surface @a make builds (CommandListDialog), declaring the list command it reads.
void list(QMenu* menu, const QString& label, ListSpec (*make)())
{
	QAction* action = menu->addAction(label);
	action->setProperty("controlCommand", make().listId);
	QObject::connect(action, &QAction::triggered, menu, [menu, make] {
		QWidget* host = menu;
		while (qobject_cast<QMenu*>(host) != nullptr && host->parentWidget() != nullptr) { host = host->parentWidget(); }
		showCommandList(host, make());
	});
}

void choices(QMenu* menu, const QString& label, const QString& id, const QString& key)
{
	addCommandChoices(menu, label, id, key, [id] { return CommandCall{id, {}, {}, QString(), false}; });
}

void fillWindowMenu(QMenu* menu)
{
	list(menu, QObject::tr("Track Manager..."), CommandLists::trackManager);
	run(menu, QObject::tr("Channel Load"), QStringLiteral("mixer.cpu_load"), {}, false, true);
	run(menu, QObject::tr("Large Clock"), QStringLiteral("window.large_clock"));
	run(menu, QObject::tr("Undo History..."), QStringLiteral("window.undo_history"));
	menu->addSeparator();
	list(menu, QObject::tr("Memory Locations..."), CommandLists::memoryLocations);
	run(menu, QObject::tr("Save Memory Location..."), QStringLiteral("location.save"), {}, true);
	list(menu, QObject::tr("Saved Selections..."), CommandLists::savedSelections);
	run(menu, QObject::tr("Save the Selection..."), QStringLiteral("selection.save"), {}, true);
	menu->addSeparator();
	run(menu, QObject::tr("Focus Next Panel"), QStringLiteral("window.focus_panel"), {{QStringLiteral("panel"), QStringLiteral("next")}})
		->setShortcut(QKeySequence(Qt::Key_F6));
	run(menu, QObject::tr("Focus Previous Panel"), QStringLiteral("window.focus_panel"), {{QStringLiteral("panel"), QStringLiteral("previous")}})
		->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F6));
	menu->addSeparator();
	choices(menu, QObject::tr("Keep on Top"), QStringLiteral("window.stay_on_top"), QStringLiteral("enabled"));
	run(menu, QObject::tr("Cascade Windows"), QStringLiteral("window.tidy"), {{QStringLiteral("mode"), QStringLiteral("cascade")}});
	run(menu, QObject::tr("Tile Windows"), QStringLiteral("window.tidy"), {{QStringLiteral("mode"), QStringLiteral("tile")}});
}

void fillHelpMenu(QMenu* help)
{
	help->addSeparator();
	run(help, QObject::tr("Ask..."), QStringLiteral("window.ask"));
	run(help, QObject::tr("What's New"), QStringLiteral("window.whats_new"));
}

//! WF-141: this version's release notes, once, when a person starts it (never for an agent or a test).
void showWhatsNewOnce(QWidget* window)
{
	if (isUnattendedRun()) { return; }
	const ControlResult notes = ControlRegistry::instance()->invoke(QStringLiteral("app.whats_new"));
	if (!notes.ok || notes.result.value(QStringLiteral("seen")).toBool()) { return; }
	QTimer::singleShot(1500, window, [] { ControlRegistry::instance()->invoke(QStringLiteral("window.whats_new")); });
}

} // namespace

void populateCustomiseExtras(QMenu* menu)
{
	menu->addSeparator();
	QMenu* theme = menu->addMenu(QObject::tr("Theme"));
	choices(theme, QObject::tr("Palette"), QStringLiteral("window.appearance"), QStringLiteral("palette"));
	choices(theme, QObject::tr("Layout Density"), QStringLiteral("window.appearance"), QStringLiteral("density"));
	list(theme, QObject::tr("Colours..."), CommandLists::themeColors);
	run(theme, QObject::tr("Export Theme..."), QStringLiteral("theme.export"), {}, true);
	run(theme, QObject::tr("Import Theme..."), QStringLiteral("theme.import"), {}, true);
	run(theme, QObject::tr("Reset Colours"), QStringLiteral("theme.reset"));
	QMenu* settings = menu->addMenu(QObject::tr("Settings"));
	list(settings, QObject::tr("All Settings..."), CommandLists::settingsCatalogue);
	run(settings, QObject::tr("Find a Setting..."), QStringLiteral("settings.search"), {}, true, true);
	list(settings, QObject::tr("Presets..."), CommandLists::settingsPresets);
	run(settings, QObject::tr("Back Up to a File..."), QStringLiteral("settings.export"), {}, true);
	run(settings, QObject::tr("Restore from a File..."), QStringLiteral("settings.import"), {}, true);
}

void installWorkspaceMenus(QMenuBar* bar)
{
	if (bar == nullptr || bar->actions().isEmpty()) { return; }
	TrackLooks::instance().setOnChange(repaintTrackHeaders);  // WF-120: a chosen icon shows at once
	auto* menu = new QMenu(QObject::tr("&Window"), bar->window());
	menu->setObjectName(QStringLiteral("workspaceMenu"));
	if (insertBeforeHelp(bar, menu) != menu) { return; }
	fillWindowMenu(menu);
	if (QMenu* help = bar->actions().last()->menu()) { fillHelpMenu(help); }
	showWhatsNewOnce(bar->window());
}

} // namespace lmms::gui
