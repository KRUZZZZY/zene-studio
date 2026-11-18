/*
 * AutomationPointEdit.h - one automation point moved from the editor's keys or its item list
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


#ifndef LMMS_GUI_AUTOMATION_POINT_EDIT_H
#define LMMS_GUI_AUTOMATION_POINT_EDIT_H

#include <QString>

#include "lmms_export.h"

namespace lmms
{
class AutomationClip;
}

/*! ED-194: what the automation editor's arrow keys and its item list send. A point of a clip whose control the command
 *  surface can name (an instrument's or an effect's parameter, the clip being that control's first) moves through
 *  automation.move_point - the agent's own command, one undo step. A control the surface cannot name (a track's
 *  volume, the tempo) is moved the same way in place, behind one journal checkpoint - still one undo step. Either way
 *  a move onto a tick another point holds is refused (the time map is keyed by tick: it would delete that point), and
 *  the point keeps its curve and bend. */
namespace lmms::gui::automationedit
{

//! The control @a clip automates: its range bounds the values, its step the keys' value step.
struct Range
{
	float minimum = 0.f;
	float maximum = 1.f;
	float step = 0.f;
};
LMMS_EXPORT bool rangeOf(const AutomationClip* clip, Range* range);

/*! Move the point of @a clip at clip-local @a tick to @a toTick with the stored value @a toValue (clamped to the
 *  control's range). False, with @a error saying why, when there is no such point or the move was refused. */
LMMS_EXPORT bool movePoint(AutomationClip* clip, int tick, int toTick, float toValue, QString* error);

//! What one Up/Down key changes a point's value by: a hundredth of the range (a tenth with @a coarse), at least one
//! step of the control.
LMMS_EXPORT float valueStep(const AutomationClip* clip, bool coarse);

} // namespace lmms::gui::automationedit

#endif // LMMS_GUI_AUTOMATION_POINT_EDIT_H
