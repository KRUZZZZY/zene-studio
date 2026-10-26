/*
 * NoteLaneKeys.h - the expression and controller lanes from the keyboard
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

#ifndef LMMS_GUI_NOTE_LANE_KEYS_H
#define LMMS_GUI_NOTE_LANE_KEYS_H

#include "lmms_export.h"

class QWidget;

/*! 0.5.0 UX decision, theme 10: the lanes under the notes, operated without a mouse. Each lane widget gets a key
 *  handler (an event filter it owns, so the lane's painting and pointer code are untouched), Tab focus, a screen-reader
 *  role and value (AccessibleCanvas.h) and the keyboard mark.
 *
 *  The expression lane (velocity, pan, chance, ...) - a slider over the selected notes:
 *  Left / Right the previous / next note (Shift adds it to the selection), Home / End the first / last note;
 *  Up / Down the value 1 up / down, Page Up / Page Down 10; Ctrl+Shift+L the editable note list. Every lane - the
 *  MPE lanes (timbre, pressure, micro-pitch) included - writes every selected note in ONE note.edit, so each key
 *  press is one undo step and a selected note the cursor is not on moves with the rest.
 *
 *  The controller lane (a CC, pitch bend, aftertouch or program curve) - a cursor on the time axis:
 *  Left / Right a grid step, Ctrl+Left / Ctrl+Right the previous / next point, Home / End the clip's start / end;
 *  Up / Down the value at the cursor 1 up / down (Page Up / Page Down an eighth of the range), adding a point there
 *  when there is none; Insert adds a point with the curve's value; Delete removes the point at the cursor;
 *  Ctrl+Shift+L the editable point list. Each is ONE clip.controllers_set. */
namespace lmms::gui
{

class NoteEditor;

namespace lanekeys
{

LMMS_EXPORT void attachExpressionLane(QWidget* lane, NoteEditor* editor);
LMMS_EXPORT void attachControllerLane(QWidget* lane, NoteEditor* editor);

} // namespace lanekeys

} // namespace lmms::gui

#endif // LMMS_GUI_NOTE_LANE_KEYS_H
