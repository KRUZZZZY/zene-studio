/*
 * LayoutMenu.cpp - the Window Layouts menu: the personal library and the layout the project remembers
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

#include "LayoutMenu.h"

#include <QApplication>
#include <QJsonArray>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "ControlWindowCommands.h"
#include "Engine.h"
#include "MainWindow.h"
#include "ProjectWorkspace.h"
#include "Shell.h"
#include "Song.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

void clearRebuiltMenu(QMenu* menu)
{
	for (QAction* action : menu->actions())
	{
		menu->removeAction(action);
		if (QMenu* sub = action->menu()) { sub->deleteLater(); }  // its own action goes with it
		else if (action->parent() == menu) { action->deleteLater(); }
	}
}

namespace LayoutMenu
{

namespace
{

std::function<bool(const QString&)> s_ask;

Shell* mainShell()
{
	const MainWindow* window = controlMainWindow();
	return window != nullptr ? window->shell() : nullptr;
}

bool projectHasLayout()
{
	return !ProjectWorkspace::instance().part(ProjectWorkspace::Part::Layout).isEmpty();
}

//! A disabled line saying what the items under it keep; it declares the list command it reads (A15).
void addNote(QMenu* menu, const QString& text, const QString& listId)
{
	QAction* note = menu->addAction(text);
	note->setEnabled(false);
	note->setProperty("controlCommand", listId);
}

CommandCall bare(const QString& id, const QJsonObject& fixed = {})
{
	return CommandCall{id, fixed, {}, QString(), false};
}

//! Theme 22: one action per saved layout in @a sub that replaces it with the layout on screen, asking first.
void addReplace(QMenu* sub, const QJsonArray& names)
{
	const QString id = QStringLiteral("window.layout_save");
	for (const QJsonValue& value : names)
	{
		const QString name = value.toString();
		QAction* action = sub->addAction(name);
		action->setProperty("controlCommand", id);
		action->setEnabled(ControlRegistry::instance()->hasCommand(id));
		QObject::connect(action, &QAction::triggered, sub, [id, name] {
			if (!confirm(QMenu::tr("Replace the layout \"%1\" with the one on screen?").arg(name))) { return; }
			runCommand(QApplication::activeWindow(), bare(id, {{QStringLiteral("name"), name}, {QStringLiteral("replace"), true}}));
		});
	}
	sub->setEnabled(!names.isEmpty());
}

//! One action per saved layout in @a sub, each running @a id on that name; disabled when there is none.
void addNamed(QMenu* sub, const QJsonArray& names, const QString& id)
{
	for (const QJsonValue& name : names)
	{
		const QJsonObject named{{QStringLiteral("name"), name}};
		addCommandAction(sub, name.toString(), id, [id, named] { return bare(id, named); });
	}
	sub->setEnabled(!names.isEmpty());
}

void rememberOnScreen(bool replacing)
{
	if (replacing && !confirm(QMenu::tr("This project already remembers a window layout. Replace it with the one on screen?"))) { return; }
	runCommand(QApplication::activeWindow(), bare(QStringLiteral("window.layout_project_save")));
}

//! The project's own layout: remember the one on screen, put it back, forget it.
void addProjectItems(QMenu* menu, bool remembered, bool shown)
{
	addNote(menu, shown ? QMenu::tr("This project's layout is on screen; yours comes back in a project without one")
		: QMenu::tr("This project's layout - saved with it and put back when it opens"), QStringLiteral("window.layout_list"));
	QAction* remember = menu->addAction(QMenu::tr("Remember This Layout with the Project"));
	remember->setProperty("controlCommand", QStringLiteral("window.layout_project_save"));
	remember->setEnabled(ControlRegistry::instance()->hasCommand(QStringLiteral("window.layout_project_save")));
	QObject::connect(remember, &QAction::triggered, menu, [remembered] { rememberOnScreen(remembered); });
	for (QAction* action : {
			addCommandAction(menu, QMenu::tr("Apply the Project's Layout"), QStringLiteral("window.layout_project_apply"),
				[] { return bare(QStringLiteral("window.layout_project_apply")); }),
			addCommandAction(menu, QMenu::tr("Forget the Project's Layout"), QStringLiteral("window.layout_project_clear"),
				[] { return bare(QStringLiteral("window.layout_project_clear")); })})
	{
		action->setEnabled(action->isEnabled() && remembered);
	}
}

void rebuild(QMenu* menu)
{
	clearRebuiltMenu(menu);
	const QJsonObject listed = ControlRegistry::instance()->invoke(QStringLiteral("window.layout_list")).result;
	const QJsonArray names = listed.value(QStringLiteral("layouts")).toArray();
	// Theme 22: what a layout keeps, the limit, and whose it is are said where layouts are saved.
	addNote(menu, QMenu::tr("Your layouts, in every project - %1 of %2 saved").arg(names.size())
		.arg(listed.value(QStringLiteral("max_layouts")).toInt()), QStringLiteral("window.layout_list"));
	addNote(menu, QMenu::tr("A layout keeps the view, the open, pinned and floating panels and their sizes - not the zoom, "
		"the scroll or the selection"), QStringLiteral("window.layout_list"));
	addCommandAction(menu, QMenu::tr("Save Layout..."), QStringLiteral("window.layout_save"), [] {
		return CommandCall{QStringLiteral("window.layout_save"), {{QStringLiteral("replace"), false}}, {}, QMenu::tr("Save Layout"), true};
	});
	addReplace(menu->addMenu(QMenu::tr("Replace")), names);
	addNamed(menu->addMenu(QMenu::tr("Apply")), names, QStringLiteral("window.layout_apply"));
	addNamed(menu->addMenu(QMenu::tr("Remove")), names, QStringLiteral("window.layout_remove"));
	menu->addSeparator();
	addProjectItems(menu, listed.value(QStringLiteral("project_layout")).toBool(),
		listed.value(QStringLiteral("project_layout_shown")).toBool());
}

} // namespace

void populate(QMenu* menu)
{
	QObject::connect(menu, &QMenu::aboutToShow, menu, [menu] { rebuild(menu); });
	rebuild(menu);
}

void recallProjectLayout(QObject* context)
{
	QTimer::singleShot(0, context, [] {
		if (projectHasLayout())
		{
			ControlRegistry::instance()->invoke(QStringLiteral("window.layout_project_apply"));  // puts the personal one aside
			return;
		}
		if (Shell* shell = mainShell()) { shell->restorePersonal(); }  // theme 22: a project without one shows yours
	});
}

void followNewProjects(QObject* context)
{
	Song* song = Engine::getSong();
	if (song == nullptr) { return; }
	QObject::connect(song, &Song::projectFileNameChanged, context, [context] {
		QTimer::singleShot(0, context, [] {
			// Only a new, unnamed project: a Save As keeps the project, and its layout, on screen.
			const Song* now = Engine::getSong();
			if (now == nullptr || !now->projectFileName().isEmpty() || projectHasLayout()) { return; }
			if (Shell* shell = mainShell()) { shell->restorePersonal(); }
		});
	});
}

bool confirm(const QString& question)
{
	if (s_ask) { return s_ask(question); }
	if (isUnattendedRun()) { return true; }  // no one to ask; the replacement is one undo step
	return QMessageBox::question(QApplication::activeWindow(), QMenu::tr("Replace"), question,
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

void setQuestion(std::function<bool(const QString&)> askFn)
{
	s_ask = std::move(askFn);
}

} // namespace LayoutMenu

} // namespace lmms::gui
