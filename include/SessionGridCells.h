/*
 * SessionGridCells.h - what the clip launcher's headers, cells and scene buttons say
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

#ifndef LMMS_GUI_SESSION_GRID_CELLS_H
#define LMMS_GUI_SESSION_GRID_CELLS_H

#include <vector>

#include <QString>

#include "lmms_export.h"

namespace lmms::gui::sessiongrid
{

/*! 0.5.0 theme 14 (DESIGN-DECISIONS.md "make the session grid easier to read and control"; Bitwig's launcher is
 *  the model): every column is headed by its track's name (numbered when the track has none), every filled cell
 *  and every scene carries a launch affordance - a play mark - and the mark's SHAPE says the state, so colour is
 *  never the only cue:
 *    ▷ stopped (press to launch)   ▷… queued (launches on the next quantum)   ▶ playing   ■ stopping
 *  A scene's state is its row's: queued while any of its cells waits to launch, else playing while any plays, else
 *  empty when it holds no clip, else stopped. Pure functions of session.get_state's reading, so a test can check
 *  them without a window. UI thread only. */
enum class SceneState
{
	Empty,
	Stopped,
	Queued,
	Playing
};

//! The scene state of row @a scene. @a names holds each cell's name in (track * @a scenes + scene) order, empty for
//! an empty cell; @a playingScene and @a phases hold each column's scene (-1 when idle) and phase
//! ("launching", "playing", "stopping" or empty), as SessionGridView reads them.
LMMS_EXPORT SceneState sceneState(int scene, int scenes, const std::vector<QString>& names,
	const std::vector<int>& playingScene, const std::vector<QString>& phases);

//! "empty", "stopped", "queued", "playing" - what a screen reader hears after the scene's name.
LMMS_EXPORT QString stateText(SceneState state);
//! A scene button's play mark for @a state, with the space after it.
LMMS_EXPORT QString sceneMark(SceneState state);
//! A cell's play mark for its column's @a phase on it (empty when its column is not on it), with the space after
//! it; nothing for an empty cell.
LMMS_EXPORT QString cellMark(const QString& phase, bool filled);

//! WF-055: the mark of a cell recording audio, for session.record_slot_status's @a state - "● Rec " while it records
//! (or runs on to its length), "○ Rec " while it waits for its launch, nothing otherwise. The shape, not the colour,
//! says which; the accessible name says it in words (recordingWords).
LMMS_EXPORT QString recordingMark(const QString& state);
//! "recording", "armed to record" or empty, for the same @a state.
LMMS_EXPORT QString recordingWords(const QString& state);

//! Column @a track's header: the song track's name, or "Track N" (1-based) when it has none or there is no such
//! track.
LMMS_EXPORT QString columnTitle(int track);

//! What launching the cell named @a name will do to its column, for its tooltip: it replaces @a playing (the clip
//! the column plays now, in another scene) - or empty when the column is idle or already on this cell.
LMMS_EXPORT QString launchReplaces(const QString& name, const QString& playing);

} // namespace lmms::gui::sessiongrid

#endif // LMMS_GUI_SESSION_GRID_CELLS_H
