/*
 * SurroundPanner.h - the surround panner's window: a room to place a channel in, its LFE send and spread
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

#ifndef LMMS_GUI_SURROUND_PANNER_H
#define LMMS_GUI_SURROUND_PANNER_H

#include <QString>

#include "lmms_export.h"

class QWidget;

namespace lmms::gui
{

/*! 0.5.0 F7 (MX-060): a window for channel @a channelId's surround panner (mixer.surround_pan), read
 *  from mixer.surround_get when it opens: the room, seen from above with the front at the top, with the
 *  speakers of the first surround channel the channel sends into (5.1 when it sends into none) and the
 *  channel's point, which is dragged; an LFE slider, a spread slider and an on/off switch. Each drag, and
 *  each slider or switch change, is one mixer.surround_pan call when it ends, so one undo step. */
LMMS_EXPORT void showSurroundPanner(const QString& channelId, QWidget* parent = nullptr);

} // namespace lmms::gui

#endif // LMMS_GUI_SURROUND_PANNER_H
