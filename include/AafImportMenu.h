/*
 * AafImportMenu.h - File > Import AAF... (interchange.aaf_import, WF-038)
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

#ifndef LMMS_GUI_AAF_IMPORT_MENU_H
#define LMMS_GUI_AAF_IMPORT_MENU_H

#include "lmms_export.h"

class QAction;
class QMenu;

namespace lmms::gui
{

/*! The File menu's Import AAF... item, beside Import DAWproject...: a file dialog, then
 *  interchange.aaf_import with the earliest clip moved to the song's start (an AAF's timeline
 *  usually begins at timecode 01:00:00:00); the reply - what was built, the missing media - or
 *  the refusal floats over the window. In a build without LibAAF the item is there but disabled,
 *  and says why. Returns the action. */
LMMS_EXPORT QAction* addAafImportAction(QMenu* fileMenu);

} // namespace lmms::gui

#endif // LMMS_GUI_AAF_IMPORT_MENU_H
