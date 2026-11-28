/*
 * ControlStepInput.h - the per-clip step-entry cursor: where the next step-entered note starts
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

#ifndef LMMS_CONTROL_STEP_INPUT_H
#define LMMS_CONTROL_STEP_INPUT_H

#include <QString>

#include "lmms_export.h"

namespace lmms::control
{

/*! A clip's step-entry cursor, in ticks from the clip's start: where the next note a step
 *  entry enters begins.
 *
 *  THIS IS EDIT STATE, NOT PROJECT STATE. It is not saved, not journalled and not part of any
 *  project file, so a project made before it existed opens and renders exactly as it did, and
 *  `control.undo` does not move it (it reverses the notes a step added, which is the edit).
 *  A clip names its own cursor by its stable `clip-<n>` id, which is never reused within a
 *  session, so a stale entry cannot be read by a clip made later.
 *
 *  It exists beside the notes rather than inside them because two entry paths have to agree
 *  on it - a MIDI keyboard's live notes and the editor's computer keys both go through
 *  `note.step_input`, which reads and advances this one cursor - and because a command that
 *  is driven over the control socket must leave the editor's cursor exactly where the editor's
 *  own entry leaves it.
 *
 *  THREAD: read and written only on the thread that runs control commands (the GUI thread,
 *  where the control server, the command handlers and the note editor all live). The audio
 *  thread never reads or writes it, so there is no lock, no atomic and no allocation on that
 *  path.
 */

//! @a clipId's step cursor in ticks; 0 for a clip that has never been step-entered.
LMMS_EXPORT int stepInputCursor(const QString& clipId);

//! Put @a clipId's cursor at @a ticks, clamped to 0..MaxSongLength (include/Song.h). An empty
//! id (no clip open) changes nothing.
LMMS_EXPORT void setStepInputCursor(const QString& clipId, int ticks);

} // namespace lmms::control

#endif // LMMS_CONTROL_STEP_INPUT_H
