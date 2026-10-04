/*
 * LayoutMenu.h - the Window Layouts menu: the personal library and the layout the project remembers
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

#ifndef LMMS_GUI_LAYOUT_MENU_H
#define LMMS_GUI_LAYOUT_MENU_H

#include <functional>

#include <QString>

#include "lmms_export.h"

class QMenu;
class QObject;

namespace lmms::gui
{

//! Take every action and submenu out of @a menu, each deleted later: a menu that rebuilds itself can be
//! rebuilt from one of its own actions (a command its action ran refreshes it), so nothing is deleted now.
LMMS_EXPORT void clearRebuiltMenu(QMenu* menu);

/*! Design theme 22: saved window layouts behave visibly. Two kinds, kept apart -
 *  - the personal library (window.layout_save / apply / remove, the config file): "Save Layout..." never
 *    replaces silently (a name in use is refused, the form says so), "Replace" names the layout it
 *    overwrites (one undo step, no question), and the menu says what a layout keeps and how many of the
 *    limit are saved;
 *  - the layout THIS project remembers (window.layout_project_*, saved with the project): remember the one
 *    on screen (replacing the project's own asks first), put it back, forget it. A project that remembers
 *    one gets it back when it opens (recallProjectLayout). */
namespace LayoutMenu
{

//! Fill @a menu (a "Window Layouts" submenu) and rebuild it each time it is shown.
LMMS_EXPORT void populate(QMenu* menu);

//! On the next turn of the event loop, put back the layout the open project remembers, when it has one.
LMMS_EXPORT void recallProjectLayout(QObject* context);

//! Tests: answer "replace the project's layout?" without a dialog (the default asks, or says yes unattended).
LMMS_EXPORT void setQuestion(std::function<bool(const QString&)> ask);

} // namespace LayoutMenu

} // namespace lmms::gui

#endif // LMMS_GUI_LAYOUT_MENU_H
