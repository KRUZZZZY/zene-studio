/*
 * ArrangerAids.h - the arranger's cross-hair cursor and pinch to zoom
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

#ifndef LMMS_GUI_ARRANGER_AIDS_H
#define LMMS_GUI_ARRANGER_AIDS_H

#include "lmms_export.h"

class QWidget;

namespace lmms::gui
{

/*! 0.5.0 W3 L13 / L14, two aids on the arranger, installed once with the menus:
 *  - the cross-hair cursor (WF-109): a vertical and a horizontal guide through the pointer across the whole
 *    arranger, to line a clip up with a bar and a track at a glance; shown while view.crosshair is on;
 *  - pinch to zoom (WF-133): a two-finger pinch on a touch screen or a trackpad zooms the arranger -
 *    0.5.0 theme 9: continuously, around the tick under the pinch (ArrangerZoom), one zoom history entry
 *    per pinch.
 *  Both are interface only: they paint, zoom the view or run a registry command, and touch nothing else. */
LMMS_EXPORT void installArrangerAids(QWidget* arranger);

//! Show or hide the cross-hair (the arranger's aids must be installed for it to show).
LMMS_EXPORT void showCrosshair(bool shown);
LMMS_EXPORT bool crosshairShown();

} // namespace lmms::gui

#endif // LMMS_GUI_ARRANGER_AIDS_H
