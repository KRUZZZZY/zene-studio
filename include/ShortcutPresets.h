/*
 * ShortcutPresets.h - the shipped keyboard-map presets (0.5.0 Q050-202)
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_GUI_SHORTCUT_PRESETS_H
#define LMMS_GUI_SHORTCUT_PRESETS_H

#include <QJsonObject>
#include <QList>
#include <QString>

#include "lmms_export.h"

namespace lmms::gui
{

/*! One named keymap. Keys are keyed by the REGISTRY COMMAND the menu action declares, not by a
 *  menu path: applying a preset resolves each command to the live menu action that dispatches
 *  it, so a menu rename cannot leave a stale id behind and an action this build does not have is
 *  simply skipped. The mappings cover menu actions only, the existing ShortcutMap model. */
struct ShortcutPreset
{
	QString id;          //!< stable name: accessibility, ableton-live, fl-studio, logic-pro
	QString name;        //!< what the menu says
	QString description; //!< what it is for, and that it maps corresponding actions only
	QJsonObject keysByCommand;  //!< registry command id -> portable key text
};

//! The shipped presets, in menu order: the accessibility map, then the three source DAWs.
LMMS_EXPORT QList<ShortcutPreset> shortcutPresets();
//! @a id's preset, or nullptr.
LMMS_EXPORT const ShortcutPreset* shortcutPreset(const QString& id);

} // namespace lmms::gui

#endif // LMMS_GUI_SHORTCUT_PRESETS_H
