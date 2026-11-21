/*
 * RenderRangeMenu.h - Navigate > Range: render the selection, and export the song's sections
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

#ifndef LMMS_GUI_RENDER_RANGE_MENU_H
#define LMMS_GUI_RENDER_RANGE_MENU_H

#include <functional>

#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h" // ControlResult
#include "lmms_export.h"

class QMenu;

namespace lmms::gui
{

/*! The two range renders the 0.5.0 L12 export wave owes the interface:
 *
 *  - "Render Selection..." renders the active arranger range (range.set) to one
 *    file through render.render, forwarding the range's exact start/end ticks;
 *  - "Export Sections..." writes one bounded file per named section (section.*)
 *    through render.sections.
 *
 *  Added to @a rangeMenu and returned. Both actions declare their registered
 *  command in the "controlCommand" property, so the agent-surface gate sees the
 *  menu item and the socket reach the same implementation. */
LMMS_EXPORT QMenu* addRenderRangeActions(QMenu* rangeMenu);

//! Tests replace the file picker (Render Selection), the directory picker (Export Sections) and
//! the command invoker (default: ControlRegistry::instance()->invoke). Empty functions restore the
//! defaults. A picker that returns an empty path cancels.
LMMS_EXPORT void setRenderRangeOutputPicker(std::function<QString(const QString& title)> picker);
LMMS_EXPORT void setRenderSectionsOutputPicker(std::function<QString(const QString& title)> picker);
LMMS_EXPORT void setRenderRangeInvoker(
	std::function<ControlResult(const QString& command, const QJsonObject& args)> invoker);

} // namespace lmms::gui

#endif // LMMS_GUI_RENDER_RANGE_MENU_H
