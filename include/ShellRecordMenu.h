/*
 * ShellRecordMenu.h - a track's Record and Retrospective Capture submenus (the track menu's More)
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

#ifndef LMMS_GUI_SHELL_RECORD_MENU_H
#define LMMS_GUI_SHELL_RECORD_MENU_H

#include <QJsonObject>

class QMenu;

namespace lmms::gui::ShellCommandMenus
{

//! Part of populateTrack (ShellCommandMenus.cpp): arming, the audio input, an instrument track's MIDI
//! takes (Q050-054.1) and retrospective capture, every entry a registered command. @a self is the
//! track's {"track": id}.
void recordMenu(QMenu* menu, const QJsonObject& self, bool instrument);

} // namespace lmms::gui::ShellCommandMenus

#endif // LMMS_GUI_SHELL_RECORD_MENU_H
