/*
 * TaskCommands.h - the task.* command group's registration
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

#ifndef LMMS_TASK_COMMANDS_H
#define LMMS_TASK_COMMANDS_H

#include "lmms_export.h"

namespace lmms
{

class ControlRegistry;

/*! 0.5.0 UX decision (theme 24): task.list / task.cancel / task.clear_finished over TaskCenter
 *  (src/core/ControlCommandsTask.cpp), and task.show - the status bar's task list - from src/gui/TaskIndicator.cpp,
 *  registered from here. All are session or interface state: none is journalled. */
LMMS_EXPORT void registerTaskCommands(ControlRegistry& registry);
LMMS_EXPORT void registerTaskWindowCommand(ControlRegistry& registry);

} // namespace lmms

#endif // LMMS_TASK_COMMANDS_H
