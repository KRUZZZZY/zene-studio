/*
 * NoteTransformActions.h - the piano roll's randomise, humanise and velocity-scale tools
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

#ifndef LMMS_GUI_NOTE_TRANSFORM_ACTIONS_H
#define LMMS_GUI_NOTE_TRANSFORM_ACTIONS_H

#include <functional>
#include <optional>

#include <QList>
#include <QString>

#include "lmms_export.h"

class QAction;
class QObject;

namespace lmms
{
class MidiClip;
}

namespace lmms::gui
{

//! Asks for one number: (title, label, initial, min, max, decimals) -> the value, or nothing.
using NoteTransformPrompt = std::function<std::optional<double>(const QString&, const QString&, double, double, double, int)>;

/*! Hands the notes selected in the piano roll (Note::selected) to the command surface (note.select), whose own
 *  selection a command's 'selection' scope reads; false, sending nothing, when none is selected. */
LMMS_EXPORT bool handOverNoteSelection(const MidiClip* clip);

//! Replaces the prompt (a QInputDialog by default) - for tests; an empty function restores it.
LMMS_EXPORT void setNoteTransformPrompt(NoteTransformPrompt prompt);

/*! KNOWN-LIMITATIONS "note randomisation, transforms ... drivable through the socket, not from the
 *  interface": three items for the piano roll's note-tools menu - Randomize velocities...,
 *  Humanize timing... and Scale velocities... - each asking for its amount and running
 *  note.randomize / note.velocity_scale on the notes selected in @a clip, or on the whole clip
 *  when none is selected (the commands' own 'selection' / 'clip' scopes). Seeded like the command:
 *  the project's seed, so the same notes roll the same way. One undo step each.
 *
 *  Q050-016 adds the streams' GUI path beside them: MPE pitch ramp... (asks for the bend at the
 *  note's end in cents and writes a {0:0, len:value} pitch stream, keeping the other axes) and
 *  Clear MPE expression streams (empties every axis of the notes that carry a stream, skipping
 *  the rest so no empty undo step is pushed). Both run the shared note.expression_stream_set
 *  command - the same id the agent surface uses - with one MidiClip-checkpoint undo step per
 *  note. */
LMMS_EXPORT QList<QAction*> makeNoteTransformActions(std::function<const MidiClip*()> clip, QObject* parent);

} // namespace lmms::gui

#endif // LMMS_GUI_NOTE_TRANSFORM_ACTIONS_H
