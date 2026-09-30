/*
 * ControllerTemplateMenu.h - Edit > Controller Templates: saved MIDI mapping sets
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

#ifndef LMMS_GUI_CONTROLLER_TEMPLATE_MENU_H
#define LMMS_GUI_CONTROLLER_TEMPLATE_MENU_H

#include <functional>
#include <optional>

#include <QString>

#include "lmms_export.h"

class QMenu;

namespace lmms::gui
{

//! Asks for a template's name; nothing when cancelled. Replaceable for tests.
using ControllerTemplatePrompt = std::function<std::optional<QString>()>;
LMMS_EXPORT void setControllerTemplatePrompt(ControllerTemplatePrompt prompt);

/*! KNOWN-LIMITATIONS "... no template menu": fills @a menu (cleared first; call it from aboutToShow)
 *  with every mapping template saved on this machine - "name (resolvable/bindings)", applying it
 *  (controller.template_apply) - then "Save current mappings as..." (controller.template_save) and a
 *  Delete submenu (controller.template_delete). */
LMMS_EXPORT void populateControllerTemplateMenu(QMenu* menu);

} // namespace lmms::gui

#endif // LMMS_GUI_CONTROLLER_TEMPLATE_MENU_H
