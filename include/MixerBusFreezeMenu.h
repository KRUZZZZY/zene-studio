/*
 * MixerBusFreezeMenu.h - a bus strip's "Freeze bus" / "Unfreeze bus" entry (Q050-124)
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

#ifndef LMMS_GUI_MIXER_BUS_FREEZE_MENU_H
#define LMMS_GUI_MIXER_BUS_FREEZE_MENU_H

#include "lmms_export.h"

class QAction;
class QMenu;

namespace lmms
{
class MixerChannel;
}

namespace lmms::gui
{

/*! Adds "Freeze bus" (freeze.bus) or, on a frozen bus, "Unfreeze bus" (freeze.unfreeze_bus) to @a menu - the
 *  registered commands an agent runs, with the strip's stable channel id. Nothing for a channel that is not a bus.
 *  Returns the entry, or nullptr. */
LMMS_EXPORT QAction* addBusFreezeAction(QMenu* menu, const MixerChannel* channel);

} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_BUS_FREEZE_MENU_H
