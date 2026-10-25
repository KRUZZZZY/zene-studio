/*
 * TouchInput.h - a single finger acts as the left mouse button
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
#ifndef LMMS_GUI_TOUCH_INPUT_H
#define LMMS_GUI_TOUCH_INPUT_H

#include "lmms_export.h"

class QWidget;

namespace lmms::gui::touch
{

/*! 0.5.0 Q050-141 (touch-first layout): a one-finger tap or drag on @a target is the left mouse
 *  button. Some platforms deliver QTouchEvent to the widget that accepted touch - the arranger
 *  accepts it for pinch to zoom - and then the widget under the finger never sees a mouse event,
 *  so a tap could not select a clip or a note. This filter turns one finger into the ordinary
 *  press, move and release a widget already handles, at the widget under the finger (the deepest
 *  visible child, so a tap on a clip reaches the clip). Two or more fingers are left untouched for
 *  Qt's gesture recognisers, so a pinch still zooms. UI thread only: the filter runs with the
 *  widget events, never on the audio thread. */
LMMS_EXPORT void installSingleTouch(QWidget* target);
//! Whether @a target already has a single-touch filter (the install is idempotent).
LMMS_EXPORT bool handlesSingleTouch(const QWidget* target);

} // namespace lmms::gui::touch

#endif // LMMS_GUI_TOUCH_INPUT_H
