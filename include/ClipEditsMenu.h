/*
 * ClipEditsMenu.h - a sample clip's Gain and fades: clip gain, fade lengths and the fade shape
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

#ifndef LMMS_GUI_CLIP_EDITS_MENU_H
#define LMMS_GUI_CLIP_EDITS_MENU_H

#include <functional>
#include <optional>

#include "lmms_export.h"

class QMenu;

namespace lmms
{
class Clip;
}

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "No clip fade, crossfade or clip-gain gestures": a "Gain and fades" submenu of
 *  @a menu for @a clip - Clip gain... (a dB value, clip.set_gain) and Reset gain; Fade in and Fade
 *  out, each None / 1 beat / 2 beats / 1 bar with the clip's current length checked when it is one
 *  of them (clip.set_fade; a fade the clip cannot hold is refused and said so); and Fade shape,
 *  applied to both ramps. Every change is one control.undo step (the Clip checkpoint). */
LMMS_EXPORT void addClipEditsMenu(QMenu* menu, Clip* clip);

/*! The record step of the plan's five-user test: a checkable "Record into this clip" item on an
 *  audio clip's menu (clip.set_record). A clip it arms takes the input the next time the song
 *  plays with Record while playing, and the clip shows "Rec" until then. Nothing for a MIDI clip. */
LMMS_EXPORT void addClipRecordAction(QMenu* menu, Clip* clip);

/*! A clip's source-window edits by the playhead and the beat: Trim start to playhead / Trim end to
 *  playhead (clip.trim - the same edit the left- and right-edge drags make, offered only when the
 *  playhead is inside the clip) and Slip content -1 beat / +1 beat (clip.slip, the content moves inside
 *  a clip that stays put). Each is one control.undo step. */
LMMS_EXPORT void addClipTrimActions(QMenu* menu, Clip* clip);

//! Tests replace the gain prompt (default: a number input seeded with @a currentDb; nullopt
//! cancels). Passing an empty function restores the default.
LMMS_EXPORT void setClipGainPrompt(std::function<std::optional<double>(double currentDb)> prompt);

} // namespace lmms::gui

#endif // LMMS_GUI_CLIP_EDITS_MENU_H
