/*
 * ToolWindowMemory.h - tool windows open where the user last left them
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

#ifndef LMMS_GUI_TOOL_WINDOW_MEMORY_H
#define LMMS_GUI_TOOL_WINDOW_MEMORY_H

#include <QJsonObject>
#include <QRect>
#include <QString>

#include "lmms_export.h"

class QWidget;

namespace lmms::gui
{

/*! Design theme 22: saved window geometry is honoured. The tool windows that float beside the shell - the controller
 *  rack, project notes, the microtuner and the classic piano roll (window.toggle's floating editors) - opened
 *  centred over the main window at every start, so a tool moved to a second monitor had to be moved there again
 *  each session; and the placement a project stores for them is the MDI era's, relative to a workspace that is no
 *  longer shown, so it cannot be honoured (it is still written, for older builds).
 *
 *  Now each one's place is the user's: where it was last left - its screen, position and size - is kept in the
 *  personal config (ui/tools.geometry, the industry default the research names: window size and position are the
 *  most portable remembered properties, kept per user as Bitwig and Studio One do), and it opens there next time,
 *  fitted inside that screen's available area - or the nearest connected screen's when that one is gone - so its
 *  title bar can always be grabbed. A tool never placed still opens centred over the main window. Other floating
 *  windows (instrument and effect windows, one per track) are untouched. Interface state: nothing is recorded. */
namespace toolwindows
{

//! The tool @a content is, by its window.toggle editor name (controller_rack, project_notes, microtuner,
//! classic_piano_roll), or empty for any other window.
LMMS_EXPORT QString keyOf(const QWidget* content);

//! Put tool window @a content where it was last left, fitted to a connected screen; false when it is no tool or
//! has no remembered place (the caller centres it then).
LMMS_EXPORT bool placeRemembered(QWidget* content);

//! From now on keep where tool window @a content is: each move or resize of the shown window is remembered. Once
//! per window; nothing for a window that is no tool.
LMMS_EXPORT void remember(QWidget* content);

//! Every remembered place: {editor: {screen, x, y, width, height}}.
LMMS_EXPORT QJsonObject remembered();

//! @a saved moved and shrunk as little as it takes to lie inside @a area; @a saved itself for an empty area.
LMMS_EXPORT QRect fitted(const QRect& saved, const QRect& area);

} // namespace toolwindows

} // namespace lmms::gui

#endif // LMMS_GUI_TOOL_WINDOW_MEMORY_H
