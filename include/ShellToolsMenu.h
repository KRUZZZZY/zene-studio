/*
 * ShellToolsMenu.h - the footer's Tools home: every tool that opens in a window of its own
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

#ifndef LMMS_GUI_SHELL_TOOLS_MENU_H
#define LMMS_GUI_SHELL_TOOLS_MENU_H

#include <QJsonArray>
#include <QList>
#include <QString>

#include "lmms_export.h"

class QMenu;
class QWidget;

namespace lmms::gui
{

class ShellButton;

/*! Design theme 8 (a clear home for each tool). The docked tools live in the shell's regions, each with its tab and
 *  its footer switch; the tools that open in a window of their own (the controller rack, project notes, the
 *  microtuner, the classic piano roll, the undo history, the tempo map...) had no home in the shell's chrome: some
 *  were only in the View, Edit or Window menus. The footer's "Tools" button is their home: one labelled place that
 *  lists every one, each entry running the registered command that opens it (window.toggle for the four that toggle,
 *  checked while their window shows). Docked panels plus hiding stay the model (the owner's decision); these tools
 *  stay windows, as the research's hybrid model keeps selected editors floating. */
namespace shelltools
{

struct Tool
{
	QString name;     //!< what the menu says
	QString command;  //!< the registered command that opens it
	QString editor;   //!< its window.toggle editor name, or empty when it is opened by a command of its own
};

//! Every tool window, in the order the menu lists them.
LMMS_EXPORT const QList<Tool>& tools();

//! window.get_state's `tools`: one {name, command, editor, home: "window"} per tool.
LMMS_EXPORT QJsonArray state();

//! Fill @a menu with the tools, the toggling ones checked while their window shows (window.get_state's `editors`).
LMMS_EXPORT void populate(QMenu* menu);

//! The footer's labelled "Tools" button, its menu rebuilt each time it opens.
LMMS_EXPORT ShellButton* makeButton(QWidget* parent);

} // namespace shelltools

} // namespace lmms::gui

#endif // LMMS_GUI_SHELL_TOOLS_MENU_H
