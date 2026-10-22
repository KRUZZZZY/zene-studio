/*
 * SampleClipOverlays.h - what a sample clip draws over its waveform: gain envelope, fades and their handles, transients
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

#ifndef LMMS_GUI_SAMPLE_CLIP_OVERLAYS_H
#define LMMS_GUI_SAMPLE_CLIP_OVERLAYS_H

#include <vector>

#include <QColor>
#include <QPoint>

#include "ClipEdits.h"
#include "lmms_export.h"

class QPainter;

namespace lmms::gui
{

//! The geometry an overlay is drawn in: ticks to pixels from the clip's left edge.
struct ClipOverlayFrame
{
	float pixelsPerTick = 1.f;
	int xOrigin = 0;  //!< the pixel of the clip's tick 0
	int width = 0;    //!< the clip's width in pixels
	int top = 0;
	int height = 0;
};

//! Which fade handle a point is on.
enum class FadeHandle
{
	None,
	In,
	Out
};

/*! ED-042: the clip's gain envelope as a line over the waveform, dB mapped -60..+24 from the
 *  bottom to the top, held flat before the first point and after the last. */
LMMS_EXPORT void paintGainEnvelope(QPainter& p, const ClipEdits& edits, const ClipOverlayFrame& frame, const QColor& colour);

/*! Q050-125: the clip's gain as a dashed level line across the waveform plus the square handle at
 *  its left edge that drags it (`clip.set_gain` on release). The line is drawn at the gain's own dB
 *  on the envelope's scale; the handle is held inside the clip so a boosted clip's handle stays
 *  grabbable below the fade handles. */
LMMS_EXPORT void paintClipGain(QPainter& p, const ClipEdits& edits, const ClipOverlayFrame& frame, const QColor& colour);

//! Q050-125: where paintClipGain draws the handle, and whether @a pos takes it (a few pixels around).
LMMS_EXPORT int clipGainHandleY(const ClipEdits& edits, const ClipOverlayFrame& frame);
LMMS_EXPORT bool clipGainHandleAt(const ClipEdits& edits, const ClipOverlayFrame& frame, const QPoint& pos);

/*! Q050-125: the dB a gain-handle drag by @a dy pixels sets, from @a startDb - the whole -60..+24
 *  range over the clip's height, so the gesture is the same however tall the clip is. */
LMMS_EXPORT float clipGainDraggedDb(float startDb, int dy, const ClipOverlayFrame& frame);

/*! Q050-125: the dB of pixel @a y on the envelope's -60..+24 scale (the inverse of the mapping
 *  paintGainEnvelope draws with), and the tick of pixel @a x inside the clip. Both clamped. */
LMMS_EXPORT float gainDbAtY(int y, const ClipOverlayFrame& frame);
LMMS_EXPORT int gainTickAtX(int x, const ClipOverlayFrame& frame);

//! Q050-125: the envelope point within a few pixels of @a pos (-1 when none).
LMMS_EXPORT int gainPointAt(const ClipEdits& edits, const ClipOverlayFrame& frame, const QPoint& pos);

/*! Q050-125: @a edits with point @a index moved/added to @a tick and @a db, re-sorted into tick order
 *  (an existing point on the same tick is replaced). False - @a edits untouched - for an index outside
 *  the envelope, an add past MaxGainPoints, or a value outside the clip/range: `clip.gain_envelope_set`
 *  would refuse exactly those, so the GUI must not preview them either. */
LMMS_EXPORT bool withGainPoint(ClipEdits* edits, int index, int tick, float db, int clipTicks);

/*! Q050-125: how many points @a edits has and the tick/dB of one - the read side a drag saves. */
LMMS_EXPORT int gainPointCount(const ClipEdits& edits);

/*! ED-173 (0.5.0 L05): the fade-in and fade-out drawn as their curves (fadeShapeGain) over a
 *  shaded region, and the two handles at the top edge that drag them. */
LMMS_EXPORT void paintFades(QPainter& p, const ClipEdits& edits, int clipTicks, const ClipOverlayFrame& frame, const QColor& colour);

//! The handle under @a pos (a few pixels around each), or None.
LMMS_EXPORT FadeHandle fadeHandleAt(const ClipEdits& edits, int clipTicks, const ClipOverlayFrame& frame, const QPoint& pos);

//! The fade length, in ticks, a drag of @a handle to pixel @a x sets (0..the clip's length).
LMMS_EXPORT int fadeTicksAt(FadeHandle handle, int x, int clipTicks, const ClipOverlayFrame& frame, int offsetTicks = 0);

//! ED-150: thin marks at the top edge where the clip's audio has a transient (ticks from its start).
LMMS_EXPORT void paintTransients(QPainter& p, const std::vector<int>& ticks, const ClipOverlayFrame& frame, const QColor& colour);

//! ED-022: the clip's snap point, a dotted line with a small flag at the bottom; nothing when it has none.
LMMS_EXPORT void paintSnapPoint(QPainter& p, const ClipEdits& edits, const ClipOverlayFrame& frame, const QColor& colour);

} // namespace lmms::gui

#endif // LMMS_GUI_SAMPLE_CLIP_OVERLAYS_H
