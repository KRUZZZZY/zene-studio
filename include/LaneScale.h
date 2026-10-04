/*
 * LaneScale.h - what a note or controller lane's values mean: its scale, units and readouts
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

#ifndef LMMS_GUI_LANE_SCALE_H
#define LMMS_GUI_LANE_SCALE_H

#include <functional>
#include <vector>

#include <QString>

#include "NoteLanes.h"
#include "lmms_export.h"

class QColor;
class QPainter;
class QPoint;
class QPointF;
class QRect;

namespace lmms::gui
{

/*! 0.5.0 theme 11: what a lane's vertical axis means - its name, the values at its edges, the values that
 *  get a labelled tick, and how one value reads with its unit. Raw MIDI controllers keep their canonical
 *  0..127 (pitch bend: signed around its centre), per-note values the units the engine stores them in. */
struct LMMS_EXPORT LaneScale
{
	QString title;                    //!< "Velocity", "Mod wheel (CC 1)"
	int low = 0;                      //!< the value at the lane's bottom edge
	int high = 127;                   //!< ... and at its top edge
	std::vector<int> marks;           //!< values with a labelled tick: the ends and the neutral or centre
	std::function<QString(int)> text; //!< one value as the lane reads it, with its unit ("83 %", "L 12", "+25 ct")

	//! "0 % to 200 %": the scale and its unit, as the lane's header shows them.
	QString rangeText() const;
};

//! How a stem or point is drawn, so its state shows in its shape and outline as well as its colour.
enum class LaneHead
{
	Plain,    //!< a filled circle
	Selected, //!< a filled square, outlined
	Hidden    //!< a hollow circle: the note's key is folded away in the note area
};

namespace lanescale
{

using YOf = std::function<int(int)>;

LMMS_EXPORT LaneScale forNoteLane(NoteLane lane);
//! The scale of controller lane @a id ("cc:<n>", "pitchbend", "aftertouch", "program").
LMMS_EXPORT LaneScale forController(const QString& id);

//! A lane's height: the design system's, enlarged with the click targets in accessible mode.
LMMS_EXPORT int laneHeight();
//! How near a stem the pointer must be to take it (the minimum target of a short note's stem).
LMMS_EXPORT int stemTolerance();

//! Faint lines across @a area at the scale's marks (under the data).
LMMS_EXPORT void paintMarkLines(QPainter& p, const QRect& area, const LaneScale& scale, const YOf& yOf);
//! The header - title, range and @a status (the selection's values, say) - top left, and each mark's value
//! at the right edge, all on a backing so the data under them never hides them (over the data).
LMMS_EXPORT void paintLabels(QPainter& p, const QRect& area, const LaneScale& scale, const YOf& yOf,
	const QString& status = QString());
//! A readout badge - @a text on a backed, outlined box - beside @a anchor, kept inside @a area.
LMMS_EXPORT void paintReadout(QPainter& p, const QRect& area, const QPoint& anchor, const QString& text);
//! A stem's head or a curve's point at @a centre.
LMMS_EXPORT void paintHead(QPainter& p, const QPointF& centre, const QColor& colour, LaneHead head);

//! "Selected: 83 %", "3 selected: 64 % to 100 %" - or empty for @a count 0.
LMMS_EXPORT QString selectionText(const LaneScale& scale, int count, int lowest, int highest);

} // namespace lanescale

} // namespace lmms::gui

#endif // LMMS_GUI_LANE_SCALE_H
