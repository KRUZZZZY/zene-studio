/*
 * ChainPresetMenu.h - a track menu's Effect chain presets: save the chain, apply or delete a preset
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

#ifndef LMMS_GUI_CHAIN_PRESET_MENU_H
#define LMMS_GUI_CHAIN_PRESET_MENU_H

#include <functional>

#include <QString>

#include "lmms_export.h"

class QMenu;

namespace lmms
{
class Track;
}

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "Plugin-chain presets have no interface": on an instrument or sample track's
 *  menu, an "Effect chain presets" submenu rebuilt from chain.list each time it opens - Save
 *  chain as preset... (a name, then chain.save; an existing name asks before it is replaced), one
 *  Apply item per stored preset (chain.apply - one control.undo puts the previous chain back) and
 *  a Delete submenu (chain.remove, after a confirmation). Other tracks get nothing. */
LMMS_EXPORT void addChainPresetMenu(QMenu* menu, Track* track);

//! Tests replace the name prompt (default: a text input; an empty result cancels) and the yes/no
//! question (default: a question box). Passing an empty function restores the default.
LMMS_EXPORT void setChainPresetNamePrompt(std::function<QString()> prompt);
LMMS_EXPORT void setChainPresetQuestion(std::function<bool(const QString& question)> ask);

} // namespace lmms::gui

#endif // LMMS_GUI_CHAIN_PRESET_MENU_H
