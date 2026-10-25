/*
 * ControlCues.h - the shared controls' interaction cues: hover, hit targets and the help that says how they work
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

#ifndef LMMS_GUI_CONTROL_CUES_H
#define LMMS_GUI_CONTROL_CUES_H

#include <QString>

#include "lmms_export.h"

class QWidget;

/*! 0.5.0 UX decision (theme 23): the shared parameter controls (src/gui/widgets - knobs, faders, sliders, LCD
 *  numbers, combo boxes, LED check boxes, model buttons) behave and look alike under the pointer and the keyboard:
 *  - every one shows a hover state (WA_Hover; painted by the control from hovered()), a disabled state (the
 *    textDisabled role) and the shared keyboard focus ring (AccessibleWidgets.h);
 *  - how it works - drag, the wheel and its modifiers, double-click, the keys, the right-click menu - is its What's
 *    This text (Shift+F1, or Help's What's This mode), added after any What's This of its own - not its accessible
 *    description, which a screen reader would repeat at every focus; a fader, slider, number or combo box with a tooltip adds a one-line reminder
 *    under it (a knob shows its value in its own floating label; a button's click needs no reminder);
 *  - sizing: the one global zoom scales every control, as Bitwig's does. In accessible mode a control a layout
 *    places is given at least tokens::targetSize() (44 px in accessible mode) on each side; one a panel places by coordinates - an
 *    instrument's or an effect's fixed artwork - keeps its size, so nothing there overlaps.
 *  An application event filter does it as each control is first shown (it runs by itself when the application object
 *  is made, as the accessibility helpers do). It looks only at polish and tooltip events, and answers only the tooltip
 *  event of such a control that has a tooltip, showing it with the reminder; every other event passes untouched. */
namespace lmms::gui::cues
{

enum class Kind
{
	None,   //!< not a shared parameter control
	Knob,   //!< FloatModelEditorBase: knobs and bar editors
	Fader,
	Slider, //!< AutomatableSlider
	Number, //!< LcdSpinBox, LcdFloatSpinBox
	Choice, //!< ComboBox
	Toggle, //!< LedCheckBox, a checkable model button
	Button  //!< any other model button
};

LMMS_EXPORT Kind kindOf(const QWidget* widget);
//! How a control of @a kind works, in full: What's This's text.
LMMS_EXPORT QString interactionHelp(Kind kind);
//! The one line a tooltip adds.
LMMS_EXPORT QString interactionHint(Kind kind);
//! Whether @a widget draws its hover state now: under the pointer and enabled.
LMMS_EXPORT bool hovered(const QWidget* widget);
//! Accessible mode's hit target for @a widget, when a layout places it (see above); nothing otherwise.
LMMS_EXPORT void applyTarget(QWidget* widget);
//! What the filter does to @a widget when it is first shown (exposed for the test).
LMMS_EXPORT void prepare(QWidget* widget);
//! Install the filter (runs by itself at startup; calling it again does nothing).
LMMS_EXPORT void install();

} // namespace lmms::gui::cues

#endif // LMMS_GUI_CONTROL_CUES_H
