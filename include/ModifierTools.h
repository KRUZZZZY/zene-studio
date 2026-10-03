/*
 * ModifierTools.h - a second arranger tool while a modifier key is held
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
#ifndef LMMS_GUI_MODIFIER_TOOLS_H
#define LMMS_GUI_MODIFIER_TOOLS_H

#include <QMap>
#include <QString>

#include "lmms_export.h"

namespace lmms::gui
{

/*! 0.5.0 L04 (ED-014): hold a modifier key (Alt, Ctrl or Shift) over the arranger and its tool becomes the one bound
 *  to that key until the key is let go - the knife on Alt, say. The bindings are a preference (config
 *  ui/modifiertools, "alt=knife;shift=select"), set with arrangement.modifier_tools; none by default, so the
 *  arranger's own Ctrl/Alt drag behaviour is untouched until the user binds a key. */
namespace modifiertools
{

//! The bindings, modifier name ("alt", "ctrl", "shift") to tool name.
LMMS_EXPORT QMap<QString, QString> bindings();
LMMS_EXPORT void setBindings(const QMap<QString, QString>& bindings);
//! Watch the keyboard for the bound modifiers (once, at start-up).
LMMS_EXPORT void install();

} // namespace modifiertools

} // namespace lmms::gui

#endif // LMMS_GUI_MODIFIER_TOOLS_H
