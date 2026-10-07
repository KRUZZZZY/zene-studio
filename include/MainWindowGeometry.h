/*
 * MainWindowGeometry.h - the main window opens where, and as large as, the user last left it
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

#ifndef LMMS_GUI_MAIN_WINDOW_GEOMETRY_H
#define LMMS_GUI_MAIN_WINDOW_GEOMETRY_H

#include <QJsonObject>
#include <QRect>

#include "lmms_export.h"

class QWidget;

namespace lmms::gui
{

/*! 0.5.0 theme 22, the main window's own geometry (docs/KNOWN-LIMITATIONS.md: "The main window's own geometry is
 *  not remembered; it opens maximised"). Where the main window was left - its screen, its normal (un-maximised)
 *  position and size, and whether it was maximised - is kept in the personal config (ui/main.geometry), beside the
 *  tool windows' places (ToolWindowMemory.h, ui/tools.geometry), and the next start opens it there: fitted inside
 *  that screen's available area, or the nearest connected screen's when that one is gone, so its title bar can
 *  always be grabbed. With nothing remembered it opens maximised, as before. Interface state: nothing is recorded,
 *  and nothing goes into a project. */
namespace mainwindowgeometry
{

//! Keep where @a window is now in the config (and write the config file).
LMMS_EXPORT void save(const QWidget* window);

//! The remembered place: {screen, x, y, width, height, maximized}, or an empty object.
LMMS_EXPORT QJsonObject remembered();

//! Where a window remembered as @a saved goes now: its rectangle fitted to its screen's available area (or the
//! nearest connected screen's), room kept above it for the title bar. An empty rectangle when @a saved has no size.
LMMS_EXPORT QRect placement(const QJsonObject& saved);

//! Show @a window where it was left - maximised when it was, or when nothing is remembered. True when a
//! remembered place was used.
LMMS_EXPORT bool showRestored(QWidget* window);

} // namespace mainwindowgeometry

} // namespace lmms::gui

#endif // LMMS_GUI_MAIN_WINDOW_GEOMETRY_H
