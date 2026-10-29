/*
 * ControlCommandsShortcutsShared.h - the shortcuts.* handlers the registration table names
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


#ifndef LMMS_CONTROL_COMMANDS_SHORTCUTS_SHARED_H
#define LMMS_CONTROL_COMMANDS_SHORTCUTS_SHARED_H

#include <QJsonObject>

#include "ControlRegistry.h"

namespace lmms::shortcutcommands
{

// --- DEFINED in ControlCommandsShortcuts.cpp; registered by ControlCommandsShortcutsRegister.cpp ---
ControlResult list(const QJsonObject& args);
ControlResult set(const QJsonObject& args);
ControlResult reset(const QJsonObject& args);
ControlResult exportSet(const QJsonObject& args);
ControlResult importSet(const QJsonObject& args);
ControlResult presetList(const QJsonObject& args);
ControlResult presetApply(const QJsonObject& args);

} // namespace lmms::shortcutcommands

#endif // LMMS_CONTROL_COMMANDS_SHORTCUTS_SHARED_H
