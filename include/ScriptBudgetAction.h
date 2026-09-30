/*
 * ScriptBudgetAction.h - File > Script Memory Budget...: the cap a Lua run may hold
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

#ifndef LMMS_GUI_SCRIPT_BUDGET_ACTION_H
#define LMMS_GUI_SCRIPT_BUDGET_ACTION_H

#include <functional>
#include <optional>

#include <QString>

#include "lmms_export.h"

class QAction;
class QMenu;

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "The Lua memory budget has no interface": an item on @a menu that names the
 *  current cap and, when chosen, asks for a new one in MiB and runs script.set_memory_budget (one
 *  control.undo takes it back; a value outside the surface's range is refused and said so). */
LMMS_EXPORT QAction* addScriptBudgetAction(QMenu* menu);

//! The item's text for a budget of @a bytes.
LMMS_EXPORT QString scriptBudgetText(quint64 bytes);

//! Tests replace the MiB prompt (default: a number input seeded with @a currentMiB; nullopt
//! cancels). Passing an empty function restores the default.
LMMS_EXPORT void setScriptBudgetPrompt(std::function<std::optional<double>(double currentMiB)> prompt);

} // namespace lmms::gui

#endif // LMMS_GUI_SCRIPT_BUDGET_ACTION_H
