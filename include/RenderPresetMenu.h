/*
 * RenderPresetMenu.h - File > Render Presets: save, apply and delete a render preset, render with it
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

#ifndef LMMS_GUI_RENDER_PRESET_MENU_H
#define LMMS_GUI_RENDER_PRESET_MENU_H

#include <functional>
#include <optional>

#include <QString>

#include "lmms_export.h"

class QMenu;

namespace lmms::gui
{

//! What Save Preset... asks for: the three settings a render preset carries.
struct RenderPresetRequest
{
	QString name;
	int sampleRate = 44100;
	QString bitDepth = QStringLiteral("16");       //!< "16", "24" or "32"
	QString stereoMode = QStringLiteral("jointstereo"); //!< "mono", "stereo" or "jointstereo"
};

/*! KNOWN-LIMITATIONS "Render/export presets have no interface": a "Render Presets" submenu of
 *  @a menu, rebuilt from export.preset_list each time it opens - Default Settings and one item per
 *  stored preset, the applied one checked (export.preset_apply; one control.undo restores the
 *  previous selection), Save Preset... (export.preset_add; an existing name asks before it is
 *  replaced), a Delete submenu (export.preset_remove, after a confirmation) and Render Song with
 *  Preset... (render.render to a picked file). The export dialog keeps its own settings: an applied
 *  preset governs render.render only. */
LMMS_EXPORT QMenu* addRenderPresetMenu(QMenu* menu);

//! Tests replace the Save Preset... form (default: a small dialog; nullopt cancels), the yes/no
//! question and the output picker (an empty path cancels). Empty functions restore the defaults.
LMMS_EXPORT void setRenderPresetForm(std::function<std::optional<RenderPresetRequest>()> form);
LMMS_EXPORT void setRenderPresetQuestion(std::function<bool(const QString& question)> ask);
LMMS_EXPORT void setRenderPresetOutputPicker(std::function<QString()> picker);

} // namespace lmms::gui

#endif // LMMS_GUI_RENDER_PRESET_MENU_H
