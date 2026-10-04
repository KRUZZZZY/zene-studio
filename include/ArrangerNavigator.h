/*
 * ArrangerNavigator.h - the whole song in miniature under the arranger
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
#ifndef LMMS_GUI_ARRANGER_NAVIGATOR_H
#define LMMS_GUI_ARRANGER_NAVIGATOR_H

#include "lmms_export.h"

namespace lmms::gui
{

class SongEditorWindow;

/*! 0.5.0 L04 (ED-053): the navigator - every track's clips across the whole song in a thin strip under the
 *  arranger, with the part the arranger shows outlined. Click or drag in it to scroll there
 *  (arrangement.scroll), turn the wheel to zoom (arrangement.zoom). Shown with arrangement.navigator (Navigate >
 *  Show Navigator), a preference kept as config ui/navigator; hidden by default. Installs once.
 *  0.5.0 theme 9: it shows the song's landmarks (sections, markers, the loop, the play position), dims what
 *  the arranger does not show so the viewport stands out, zooms around the tick under the pointer, and
 *  takes the keyboard (arrows, Page Up/Down, Home/End scroll; +/- zoom). */
LMMS_EXPORT void installArrangerNavigator(SongEditorWindow* window);

} // namespace lmms::gui

#endif // LMMS_GUI_ARRANGER_NAVIGATOR_H
