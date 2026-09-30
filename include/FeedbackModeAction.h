/*
 * FeedbackModeAction.h - a mixer channel menu's Allow feedback sends: the cycle-permitted submode
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

#ifndef LMMS_GUI_FEEDBACK_MODE_ACTION_H
#define LMMS_GUI_FEEDBACK_MODE_ACTION_H

#include <functional>

#include <QString>

#include "lmms_export.h"

class QAction;
class QMenu;

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS (board card #709) "No GUI surface": a checkable "Allow feedback sends" item on
 *  @a menu. On runs feedback.enable (the mixer then offers a send that closes a loop, a one-period
 *  delayed feedback path, and plugin delay compensation is suspended - the item's tooltip says so);
 *  off runs feedback.disable after a confirmation naming how many loop-closing sends it deletes.
 *  @a refresh redraws the mixer's send buttons after either. */
LMMS_EXPORT QAction* addFeedbackModeAction(QMenu* menu, std::function<void()> refresh);

//! Tests replace the confirmation (default: a question box). An empty function restores it.
LMMS_EXPORT void setFeedbackModeQuestion(std::function<bool(const QString& question)> ask);

} // namespace lmms::gui

#endif // LMMS_GUI_FEEDBACK_MODE_ACTION_H
