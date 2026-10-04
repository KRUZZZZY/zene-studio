/*
 * ArrangerLandmarks.h - the song's landmarks (sections, markers, the loop) as the navigator names them
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

#ifndef LMMS_GUI_ARRANGER_LANDMARKS_H
#define LMMS_GUI_ARRANGER_LANDMARKS_H

#include <QString>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms::gui::landmarks
{

/*! 0.5.0 theme 9 (wave 3): a landmark of the song - a section, a marker or the loop - with the span a zoom to it
 *  fills the arranger with: a section's own span, a marker's span up to the next marker (or the song's end), the
 *  loop's span. Bitwig zooms to a cue marker's content on Alt-click; the navigator does it on Alt-click and on a
 *  double-click. UI thread only. */
struct Landmark
{
	enum class Kind { None, Section, Marker, Loop };
	Kind kind = Kind::None;
	QString name;
	tick_t start = 0;
	tick_t end = 0;
};

//! The landmark at @a tick: a marker within @a slack ticks of it first (it is the thinnest), else the section
//! there, else the loop when it is on and covers @a tick; Kind::None when there is none.
LMMS_EXPORT Landmark at(tick_t tick, tick_t slack);

//! What the navigator's tooltip and a screen reader say of @a landmark: "Section Chorus, 9.1.1 to 17.1.1".
LMMS_EXPORT QString describe(const Landmark& landmark);

} // namespace lmms::gui::landmarks

#endif // LMMS_GUI_ARRANGER_LANDMARKS_H
