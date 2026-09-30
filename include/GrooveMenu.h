/*
 * GrooveMenu.h - the piano roll's Groove menu: extract, apply and quantise with strength
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

#ifndef LMMS_GUI_GROOVE_MENU_H
#define LMMS_GUI_GROOVE_MENU_H

#include <functional>
#include <optional>

#include <QString>

#include "lmms_export.h"

class QMenu;
class QWidget;

namespace lmms
{
class MidiClip;
}

namespace lmms::gui
{

//! The two questions the menu asks, replaceable for tests: a groove's name, and a 0..100 %.
struct GroovePrompts
{
	std::function<std::optional<QString>()> name;
	std::function<std::optional<double>(const QString& title)> percent;
};
LMMS_EXPORT void setGroovePrompts(GroovePrompts prompts);

/*! KNOWN-LIMITATIONS "the groove pool and quantise have no interface": a Groove menu for the piano
 *  roll, rebuilt each time it opens. "Extract groove from this clip..." names a groove taken from
 *  the clip at the roll's grid (groove.extract); "Apply groove" lists the pool and applies one at a
 *  chosen strength (groove.apply); "Quantize with strength..." pulls the notes toward the grid by
 *  that much (groove.quantize). @a clip is the roll's clip, @a grid its quantisation in ticks. */
LMMS_EXPORT QMenu* makeGrooveMenu(std::function<const MidiClip*()> clip, std::function<int()> grid, QWidget* parent);

} // namespace lmms::gui

#endif // LMMS_GUI_GROOVE_MENU_H
