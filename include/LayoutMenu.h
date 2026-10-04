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
 *    overwrites and asks first (one undo step), and the menu says what a layout keeps, what it does not,
 *    and how many of the limit are saved;
 *  - the layout THIS project remembers (window.layout_project_*, saved with the project): remember the one
 *    on screen (replacing the project's own asks first), put it back, forget it. A project that remembers
 *    one gets it back when it opens (recallProjectLayout), and the personal layout it replaced is put aside
 *    and comes back in the next project without one, or a new one (Shell::setPersonalAside).
 *  The questions are asked only of a person: an agent's command, or an unattended run, never waits on one. */
namespace LayoutMenu
{

//! Fill @a menu (Customise's "Window Layouts" submenu, or the Window menu's "Layouts") and rebuild it each time it
//! is shown.
LMMS_EXPORT void populate(QMenu* menu);

//! On the next turn of the event loop, put back the layout the open project remembers, when it has one; for
//! one that remembers none, put back the personal layout a previous project's had put aside.
LMMS_EXPORT void recallProjectLayout(QObject* context);

//! From now on, a new (unnamed) project without a layout of its own puts back the personal layout set aside.
LMMS_EXPORT void followNewProjects(QObject* context);

//! Ask a person whether to replace something saved under a name (a layout, a memory location, a saved
//! selection): yes or no. An unattended run, which has no one to ask, says yes; the replacement is one
//! undo step. setQuestion's answer stands in for the dialog.
LMMS_EXPORT bool confirm(const QString& question);

//! Tests: answer confirm()'s question without a dialog (the default asks, or says yes unattended).
LMMS_EXPORT void setQuestion(std::function<bool(const QString&)> ask);

} // namespace LayoutMenu

} // namespace lmms::gui

#endif // LMMS_GUI_LAYOUT_MENU_H
