/*
 * ArrangerZoom.h - the arranger's zoom, kept around what the user is looking at
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

#ifndef LMMS_GUI_ARRANGER_ZOOM_H
#define LMMS_GUI_ARRANGER_ZOOM_H

#include <QPoint>
#include <QtCore/qnamespace.h>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms::gui
{

class SongEditor;

/*! 0.5.0 theme 9 (DESIGN-DECISIONS.md "a readable timeline"): a zoom keeps the user's focus in place.
 *  A pointer gesture (the wheel, a pinch, the navigator's wheel) keeps the tick under the pointer where
 *  it is; a keyboard or menu zoom keeps the SELECTION there (the time range, else the selected clips),
 *  else the PLAYHEAD when it is on screen, else the middle of the view - Pro Tools' and Live's order. An
 *  anchor that was off screen ends up in the middle. arrangement.zoom, the song editor's Ctrl+0, the
 *  pinch and the navigator all go through here, and so does the zoom history arrangement.zoom_back walks.
 *  Interface state only. */
namespace arrangerzoom
{

//! The pixels the arranger shows clips across (the ruler's width less the track heads).
LMMS_EXPORT int visibleWidth(SongEditor* editor);
//! The ticks that width shows at the current zoom.
LMMS_EXPORT tick_t visibleTicks(SongEditor* editor);
//! The tick under @a global (a screen point), for a pointer gesture's anchor.
LMMS_EXPORT tick_t tickAtGlobal(SongEditor* editor, const QPoint& global);
//! The span of the selected clips; false when none is selected.
LMMS_EXPORT bool selectedClipsSpan(SongEditor* editor, tick_t* begin, tick_t* end);
//! The selected span: the arranger's time range, else the selected clips; false when nothing is selected.
LMMS_EXPORT bool selectionSpan(SongEditor* editor, tick_t* begin, tick_t* end);
//! The playhead's tick, and whether it is on screen.
LMMS_EXPORT tick_t playhead(SongEditor* editor, bool* shown = nullptr);
//! What a keyboard zoom keeps in place: the selection's middle, else the playhead on screen, else the view's middle.
LMMS_EXPORT tick_t keyboardAnchor(SongEditor* editor);

//! After a zoom from @a ppbBefore with the view at @a startBefore: scroll so @a anchor is where it was.
LMMS_EXPORT void keepInPlace(SongEditor* editor, tick_t anchor, double ppbBefore, tick_t startBefore);
//! Set the zoom model to @a value and keep @a anchor in place.
LMMS_EXPORT void zoomAround(SongEditor* editor, int value, tick_t anchor);

/*! 0.5.0 theme 9: a wheel turn that zooms (Ctrl+wheel over the arranger, the wheel over the navigator), around
 *  @a anchor. The zoom follows the turn's size, not the number of events: a mouse notch (120) is WheelNotchSteps
 *  zoom steps (one with @a fine), and a touchpad's small deltas add up to the same, so the zoom is continuous. Turns
 *  less than WheelBurstMs apart are one gesture: it joins the history arrangement.zoom_back walks once, as a pinch
 *  does. The model's bounds are the limits (4 to 400 pixels a bar). */
LMMS_EXPORT void wheelZoom(SongEditor* editor, int delta, bool fine, tick_t anchor);
constexpr int WheelNotchSteps = 5;
constexpr int WheelBurstMs = 400;

//! The zoom history arrangement.zoom_back walks (zoom model values, newest last; UI thread).
LMMS_EXPORT void remember(int value);
//! Record the starting zoom when a pinch has ended (finished or canceled), if it changed.
LMMS_EXPORT void finishPinch(int startValue, int currentValue, bool ended);
#ifndef QT_NO_GESTURES
inline void finishPinch(int startValue, int currentValue, Qt::GestureState state)
{
	finishPinch(startValue, currentValue, state == Qt::GestureFinished || state == Qt::GestureCanceled);
}
#endif
//! Step one back: take the newest remembered extent into @a earlier and queue @a current so
//! arrangement.zoom_forward can return to it; false when there is nothing earlier (UI thread).
LMMS_EXPORT bool takeEarlier(int current, int* earlier);
//! Step one forward: take the newest queued extent into @a next and leave @a current to go back to; false when
//! there is nothing forward (UI thread).
LMMS_EXPORT bool takeNext(int current, int* next);
//! The zoom toggle: return the extent the current one last left (a zoom or a back / forward step) into
//! @a extent, with @a current the extent to return to next time; false after the very first zoom yet (UI thread).
//! The swap reconciles the walk: the extent swapped to comes off the stack that holds it and the extent left
//! behind queues in its place, so a back or forward step after the toggle never succeeds without a change.
LMMS_EXPORT bool takeToggle(int current, int* extent);
LMMS_EXPORT int historySize();
//! The extents queued forward (a forward step can take); UI thread.
LMMS_EXPORT int forwardSize();

} // namespace arrangerzoom

} // namespace lmms::gui

#endif // LMMS_GUI_ARRANGER_ZOOM_H
