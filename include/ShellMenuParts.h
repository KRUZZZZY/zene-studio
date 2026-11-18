/*
 * ShellMenuParts.h - the entries the shell's command menus are built from
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
#ifndef LMMS_GUI_SHELL_MENU_PARTS_H
#define LMMS_GUI_SHELL_MENU_PARTS_H

#include <functional>

#include <QJsonObject>
#include <QString>

#include "lmms_export.h"

class QAction;
class QMenu;

namespace lmms
{
class Clip;
class Track;
}

namespace lmms::gui
{

struct ListSpec;

//! The building blocks of the shell's command menus (ShellCommandMenus.cpp and the files that add to it).
namespace menuparts
{

LMMS_EXPORT QString tr(const char* text);

//! An entry running @a id with @a fixed - through its form when @a ask, with @a defaults.
LMMS_EXPORT QAction* add(QMenu* menu, const char* label, const char* id, const QJsonObject& fixed, bool ask,
	const QJsonObject& defaults = {}, bool showResult = false);

//! A submenu of @a id's values for @a key (enum or On/Off), each run at once with @a fixed.
LMMS_EXPORT void choices(QMenu* menu, const char* label, const char* id, const char* key, const QJsonObject& fixed);

//! An entry opening the list surface @a make builds.
LMMS_EXPORT void list(QMenu* menu, const char* label, std::function<ListSpec()> make);

//! @a base with @a key set to @a value.
LMMS_EXPORT QJsonObject with(QJsonObject base, const char* key, const QJsonValue& value);

//! 0.5.0 L04 (ED-022): a clip's placement entries - its snap point (ShellMenuEntries.cpp).
LMMS_EXPORT void clipPlacementEntries(QMenu* menu, Clip* clip, const QJsonObject& self);

//! 0.5.0 L05 (ED-152 / ED-179): an audio clip's speed - a ramp, a tape start, a tape stop (ShellMenuEntries.cpp).
LMMS_EXPORT void clipSpeedEntries(QMenu* menu, Clip* clip, const QJsonObject& self);

//! 0.5.0 L04 (ED-049): a track's versions - switch to one, keep the clips as one, forget one (ShellMenuEntries.cpp).
LMMS_EXPORT void trackVersionEntries(QMenu* menu, Track* track, const QJsonObject& self);

//! 0.5.0 L09 (MX-038 / MX-039): the control room - speakers, dim, mono check, cut (ShellMenuEntries.cpp).
LMMS_EXPORT void controlRoomEntries(QMenu* menu);

//! 0.5.0 L07 (ED-086): a MIDI clip's "Export as MusicXML..." - a file dialog, then clip.export_musicxml (ShellMenuEntries.cpp).
LMMS_EXPORT void clipScoreEntries(QMenu* menu, const QJsonObject& self);

//! 0.5.0 L08 (MX-172): a channel's crossfader side, and the crossfader itself (ShellMenuEntries.cpp).
LMMS_EXPORT void crossfaderEntries(QMenu* menu, const QJsonObject& channel);

//! 0.5.0 L09 (MX-062): an instrument track's MIDI output - a submenu of the MIDI output ports (ShellMenuEntries.cpp).
LMMS_EXPORT void trackMidiOutEntries(QMenu* menu, Track* track, const QJsonObject& self);

//! ED-029: "Loop the Content..." - a MIDI clip's notes, or (0.5.0 audio-clips) an audio clip's audio from its in-point
//! (ShellMenuEntries.cpp).
LMMS_EXPORT void clipLoopEntries(QMenu* menu, Clip* clip, const QJsonObject& self);

//! 0.5.0 ED-176: an audio clip's "Crossfade Editor..." with the clip overlapping it, disabled while none does
//! (ShellMenuEntries.cpp).
LMMS_EXPORT void crossfadeEditorEntries(QMenu* menu, Clip* clip);

//! Q050-339: an audio or instrument track's "Track Delay (N samples)..." - pdc.set_offset through its form, the field
//! starting at the stored value; nothing for other tracks (ShellMenuEntries.cpp).
LMMS_EXPORT void trackDelayEntries(QMenu* menu, Track* track, const QString& id);

//! Q050-339: a mixer channel's "Channel Delay (N samples)..." - none for the master, which has no later summing point
//! (ShellMenuEntries.cpp).
LMMS_EXPORT void channelDelayEntries(QMenu* menu, const QString& channel);

//! L08: a mixer channel's Strip submenu - polarity, mono, EQ, pan mode, solo safe, its meter mode (Q050-108), trim,
//! its delay (Q050-339), meters and clip lights (ShellMenuEntries.cpp; moved here from ShellCommandMenus.cpp).
LMMS_EXPORT void stripEntries(QMenu* strip, const QString& channel);

} // namespace menuparts

} // namespace lmms::gui

#endif // LMMS_GUI_SHELL_MENU_PARTS_H
