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

#include <QApplication>
#include <QJsonArray>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>

#include "ArrangerAids.h"
#include "CommandForm.h"
#include "CommandLists.h"
#include "ControlRegistry.h"
#include "DeprecationHelper.h"
#include "GuiApplication.h"
#include "LayoutMenu.h"
#include "MenuBarExtras.h"
#include "SongEditor.h"
#include "TrackIcons.h"
#include "ShellIcons.h"
#include "TrackLooks.h"
#include "UiConfigStore.h"
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

//! A disabled line saying what the entries below keep; it declares the list command it reads (A15).
void note(QMenu* menu, const QString& text, const QString& listId)
{
	QAction* line = menu->addAction(text);
	line->setEnabled(false);
	line->setProperty("controlCommand", listId);
}

//! Theme 22: a submenu naming each saved entry (@a listId's @a key), each replacing that entry with what is there now
//! (@a saveId, 'replace' true) once the person has said yes to @a question; rebuilt each time it opens.
void replaceMenu(QMenu* menu, const QString& label, const QString& listId, const QString& key, const QString& saveId,
	const QString& question)
{
	QMenu* sub = menu->addMenu(label);
	QObject::connect(sub, &QMenu::aboutToShow, sub, [sub, listId, key, saveId, question] {
		clearRebuiltMenu(sub);
		const QJsonArray entries = ControlRegistry::instance()->invoke(listId).result.value(key).toArray();
		for (const QJsonValue& entry : entries)
		{
			const QString name = entry.toObject().value(QStringLiteral("name")).toString();
			QAction* action = sub->addAction(name);
			action->setProperty("controlCommand", saveId);
			QObject::connect(action, &QAction::triggered, sub, [saveId, question, name] {
				if (!LayoutMenu::confirm(question.arg(name))) { return; }
				runCommand(QApplication::activeWindow(), CommandCall{saveId, {{QStringLiteral("name"), name},
					{QStringLiteral("replace"), true}}, {}, QString(), false});
			});
		}
		if (entries.isEmpty()) { note(sub, QObject::tr("Nothing is saved yet"), listId); }
	});
}

//! Memory locations and saved selections: what each keeps, saving under a new name (a name in use is refused,
//! the form says so), and replacing one by name, asking first.
void fillViewMemories(QMenu* menu)
{
	note(menu, QObject::tr("A memory location keeps the play position, the range and the zoom - not the scroll, track "
		"heights or the layout"), QStringLiteral("location.list"));
	list(menu, QObject::tr("Memory Locations..."), CommandLists::memoryLocations);
	run(menu, QObject::tr("Save Memory Location..."), QStringLiteral("location.save"), {{QStringLiteral("replace"), false}}, true);
	replaceMenu(menu, QObject::tr("Replace Memory Location"), QStringLiteral("location.list"), QStringLiteral("locations"),
		QStringLiteral("location.save"), QObject::tr("Replace the memory location \"%1\" with the play position, range and "
		"zoom now?"));
	menu->addSeparator();
	note(menu, QObject::tr("A saved selection keeps the range and the selected clips - not notes or automation points"),
		QStringLiteral("selection.list"));
	list(menu, QObject::tr("Saved Selections..."), CommandLists::savedSelections);
	run(menu, QObject::tr("Save the Selection..."), QStringLiteral("selection.save"), {{QStringLiteral("replace"), false}}, true);
	replaceMenu(menu, QObject::tr("Replace Saved Selection"), QStringLiteral("selection.list"), QStringLiteral("selections"),
		QStringLiteral("selection.save"), QObject::tr("Replace the saved selection \"%1\" with what is selected now?"));
}

void fillWindowMenu(QMenu* menu)
{
	list(menu, QObject::tr("Track Manager..."), CommandLists::trackManager);
	run(menu, QObject::tr("Channel Load"), QStringLiteral("mixer.cpu_load"), {}, false, true);
	run(menu, QObject::tr("Large Clock"), QStringLiteral("window.large_clock"));
	run(menu, QObject::tr("Tuner"), QStringLiteral("window.tuner"));
	run(menu, QObject::tr("Undo History..."), QStringLiteral("window.undo_history"));
	menu->addSeparator();
	fillViewMemories(menu);
	menu->addSeparator();
	// Theme 8 / 22: the layouts have a home where windows are, besides Project tools > Customise > Window Layouts.
	// Titled apart from that one, so each of their entries has a name of its own (the agent-surface gate's key).
	LayoutMenu::populate(menu->addMenu(QObject::tr("Layouts")));
	menu->addSeparator();
	run(menu, QObject::tr("Focus Next Panel"), QStringLiteral("window.focus_panel"), {{QStringLiteral("panel"), QStringLiteral("next")}})
		->setShortcut(QKeySequence(Qt::Key_F6));
	run(menu, QObject::tr("Focus Previous Panel"), QStringLiteral("window.focus_panel"), {{QStringLiteral("panel"), QStringLiteral("previous")}})
		->setShortcut(keySequence(Qt::SHIFT, Qt::Key_F6));
	menu->addSeparator();
	choices(menu, QObject::tr("Cross-Hair Cursor"), QStringLiteral("view.crosshair"), QStringLiteral("enabled"));
	choices(menu, QObject::tr("Keep on Top"), QStringLiteral("window.stay_on_top"), QStringLiteral("enabled"));
	run(menu, QObject::tr("Cascade Windows"), QStringLiteral("window.tidy"), {{QStringLiteral("mode"), QStringLiteral("cascade")}});
	run(menu, QObject::tr("Tile Windows"), QStringLiteral("window.tidy"), {{QStringLiteral("mode"), QStringLiteral("tile")}});
}

void fillHelpMenu(QMenu* help)
{
	help->addSeparator();
	run(help, QObject::tr("Ask..."), QStringLiteral("window.ask"));
	run(help, QObject::tr("What's New"), QStringLiteral("window.whats_new"));
	run(help, QObject::tr("Accessibility Check"), QStringLiteral("window.accessibility_report"), {}, false, true);
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
	// Theme 20: a section heading, not an entry - it runs nothing, so it declares no command.
	theme->addSection(QObject::tr("Changes apply at the next start"));
	choices(theme, QObject::tr("Palette"), QStringLiteral("window.appearance"), QStringLiteral("palette"));
	// 0.5.0 L14 (Q050-235): the touch workspace - larger launcher targets - chosen beside the palette; one command.
	choices(theme, QObject::tr("Workspace"), QStringLiteral("window.appearance"), QStringLiteral("workspace"));
	// 0.5.0 UX decision (theme 23): one global zoom sizes everything; accessible mode replaces the density presets.
	QMenu* zoom = theme->addMenu(QObject::tr("Interface Zoom"));
	for (const int percent : {75, 100, 125, 150, 175, 200})
	{
		run(zoom, QObject::tr("%1%").arg(percent), QStringLiteral("window.appearance"), {{QStringLiteral("scale"), percent / 100.0}});
	}
	choices(theme, QObject::tr("Accessible Mode"), QStringLiteral("window.appearance"), QStringLiteral("accessible"));
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
	TrackLooks::instance().setIconCheck([](const QString& name) { return !shellicons::svg(name).isEmpty(); });
	auto* menu = new QMenu(QObject::tr("&Window"), bar->window());
	menu->setObjectName(QStringLiteral("workspaceMenu"));
	if (insertBeforeHelp(bar, menu) != menu) { return; }
	fillWindowMenu(menu);
	if (QMenu* help = bar->actions().last()->menu()) { fillHelpMenu(help); }
	if (getGUI() != nullptr && getGUI()->songEditor() != nullptr) { installArrangerAids(getGUI()->songEditor()->m_editor); }
	showCrosshair(uistore::read(QStringLiteral("crosshair")).value(QStringLiteral("enabled")).toBool());
	// WF-129: once the window is built, every nameless control takes the name it already carries.
	QTimer::singleShot(0, bar->window(), [] {
		ControlRegistry::instance()->invoke(QStringLiteral("window.accessibility_report"), {{QStringLiteral("fill"), true}});
	});
	showWhatsNewOnce(bar->window());
}

} // namespace lmms::gui
