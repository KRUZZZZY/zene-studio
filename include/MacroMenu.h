/*
 * MacroMenu.h - the menu bar's Workflows menu: one action per saved workflow, and the recording state
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

#ifndef LMMS_GUI_MACRO_MENU_H
#define LMMS_GUI_MACRO_MENU_H

#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QPointer>

#include "lmms_export.h"

class QMainWindow;
class QMenuBar;

namespace lmms::gui
{

/*! WF-114 / design theme 19: a workflow is a named run of registry commands (workflow.save); this
 *  menu ("Workflows" - the class keeps its first name) has one action per workflow in the library in
 *  use, so each is in the command palette and can be given a shortcut (ShortcutMap), then Record /
 *  Stop Recording, where workflows are kept (shared or with the project) and the list to edit them.
 *  While a workflow is being recorded the menu's first line says so, and so does an indicator in the
 *  header with a Stop button (WorkflowRecordingIndicator, through showRecording). A run that stopped at a
 *  failing step is said here too, with Retry Step N and Skip Step N and Continue (workflow.resume; theme 19's
 *  step recovery). The menu also follows the project:
 *  a project opened is a library of its own when workflows are kept with projects, and a layout the
 *  project remembers is put back (LayoutMenu::recallProjectLayout). */
class LMMS_EXPORT MacroMenu : public QMenu
{
	Q_OBJECT
public:
	explicit MacroMenu(QWidget* parent = nullptr);

	//! Re-read the workflows in use (workflow.list) and the recording state, and rebuild the actions.
	void refresh();

	//! The main window's Workflows menu, added to @a bar before Help the first time.
	static MacroMenu* install(QMenuBar* bar);
	//! Rebuild @a window's Workflows menu (made when missing) and put the shortcuts bound to its actions back.
	static void refreshIn(QMainWindow* window);

	/*! Theme 19's visible recording state in @a window: the header's indicator (shown while @a on,
	 *  naming the workflow and its @a steps so far, with Stop - workflow.record_stop) and the
	 *  menu's status line. */
	static void showRecording(QWidget* window, bool on, const QString& name, int steps);

private:
	void addRuns(const QJsonArray& workflows);
	//! The stopped run's line and its Retry / Skip entries, when workflow.status @a status says one stopped.
	void addRecovery(const QJsonObject& status);
	void addRecording();
	void addScopeMenu(const QString& scope);
	//! A project was opened (@a loaded) or renamed: its workflows and its remembered layout.
	void followProject(bool loaded);

	QPointer<QAction> m_status;  //!< "Recording ..." while a workflow is being recorded
};

} // namespace lmms::gui

#endif // LMMS_GUI_MACRO_MENU_H
