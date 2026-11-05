/*
 * ArrangerClipKeys.h - choosing and editing arranger clips from the keyboard
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

#ifndef LMMS_GUI_ARRANGER_CLIP_KEYS_H
#define LMMS_GUI_ARRANGER_CLIP_KEYS_H

#include <QJsonArray>
#include <QString>

#include "lmms_export.h"

class QKeyEvent;

namespace lmms
{
class Clip;
}

/*! 0.5.0 UX decision, theme 10: clip selection without a mouse. The Edit menu already binds Alt+Left / Alt+Right
 *  (arrangement.nudge) and Alt+Up / Alt+Down (arrangement.select_clip, along the selected clip's track). The arranger
 *  adds, while it has the focus:
 *  - Shift+Alt+Up / Shift+Alt+Down: the clip on the track above / below nearest in time to the selected one (with
 *    none selected, nearest the play position) - window.select, so there is always a way in;
 *  - Ctrl+Alt+Left / Ctrl+Alt+Right: the selected clip a snap step shorter / longer - clip.resize, one undo step;
 *  - Ctrl+Shift+L: the editable clip list (window.item_list).
 *  A clip chosen this way is selected in the arranger too (what Delete acts on) and read out: its name, track,
 *  start and length.
 *
 *  Q050-255: the same keys over a folder's summary clips, which the same commands edit as groups:
 *  - Ctrl+Alt+G: select the summary clip holding the selected clip (with none, the first at or after the play position);
 *    with Shift, add it to the selection;
 *  and while one is selected, one undo step each, over the selected summary clips and the arranger's selected clips:
 *  - Ctrl+Alt+Left / Ctrl+Alt+Right: their end edge a snap step earlier / later, Shift for the start edge
 *    (track.folder_span_resize);
 *  - Ctrl+Alt+K: split them at the play position (track.folder_span_split);
 *  - Ctrl+Alt+Page Up / Page Down: move them a snap step earlier / later (track.folder_span_move);
 *  - Delete: delete them (track.folder_span_delete); Escape: select nothing. */
namespace lmms::gui
{

class SongEditor;

namespace arrangerkeys
{

//! Whether @a event is one of the keys above (the arranger claims them over a window shortcut).
LMMS_EXPORT bool claims(const QKeyEvent* event);
//! Act on @a event in @a editor; false when it is not one of the keys above.
LMMS_EXPORT bool handle(SongEditor* editor, const QKeyEvent* event);
//! Select @a clip in the shell (window.select) and in @a editor's own selection (null: the shell only); false, with
//! the refusal announced, when window.select refused it.
LMMS_EXPORT bool selectClip(SongEditor* editor, Clip* clip);
//! The ids of the clips selected in the arranger: what a summary clip's group edit takes along.
LMMS_EXPORT QJsonArray selectedClipIds();
//! "Bass, on Drums, 5.1.1, 2.0.0 long" - what a clip is read out as.
LMMS_EXPORT QString describeClip(const Clip* clip);

} // namespace arrangerkeys

} // namespace lmms::gui

#endif // LMMS_GUI_ARRANGER_CLIP_KEYS_H
