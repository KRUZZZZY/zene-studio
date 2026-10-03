/*
 * LibraryCommandMenus.h - the project and library entries the 0.5.0 L11/L12 work adds to the shell's menus
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

#ifndef LMMS_GUI_LIBRARY_COMMAND_MENUS_H
#define LMMS_GUI_LIBRARY_COMMAND_MENUS_H

#include <QString>

#include "lmms_export.h"

class QMenu;

namespace lmms
{
class Clip;
class Track;
}

namespace lmms::gui
{

/*! 0.5.0 L11 / L12 (W3): the library and project entries, beside ShellCommandMenus (which holds the
 *  W1/W2 ones and calls these). Every entry runs a registered command through CommandForm or a
 *  CommandListDialog (SPEC A11), so the menus, the socket and Lua do the same thing. */
namespace LibraryCommandMenus
{

//! The Project Tools menu's project entries: settings, metadata, document type, mixer snapshots, locks.
LMMS_EXPORT void populateProject(QMenu* menu);
//! A track's lock.
LMMS_EXPORT void populateTrack(QMenu* menu, Track* track);
//! A clip's lock.
LMMS_EXPORT void populateClip(QMenu* menu, Clip* clip);
//! The view a document type opens in (WF-048): production the arranger, mastering the mixer, live the
//! arranger with the launcher showing.
LMMS_EXPORT void applyDocumentType(const QString& type);
//! Start-up wiring, once: a loaded project opens in its document type's view.
LMMS_EXPORT void install();

} // namespace LibraryCommandMenus

} // namespace lmms::gui

#endif // LMMS_GUI_LIBRARY_COMMAND_MENUS_H
