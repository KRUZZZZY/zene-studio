/*
 * MixerBusFreezeMenu.cpp - a bus strip's "Freeze bus" / "Unfreeze bus" entry (Q050-124)
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

#include "MixerBusFreezeMenu.h"

#include <QAction>
#include <QJsonObject>
#include <QMenu>

#include "ControlVocabulary.h"
#include "Mixer.h"
#include "ShellMenuParts.h"

namespace lmms::gui
{

QAction* addBusFreezeAction(QMenu* menu, const MixerChannel* channel)
{
	if (menu == nullptr || channel == nullptr || !channel->isBus()) { return nullptr; }
	const QJsonObject self{{QStringLiteral("channel"), control::channelIdOf(channel)}};
	// Run at once: the render is the command's own (blocking, like the track menu's Unfreeze); a refusal is shown.
	return channel->m_freeze.isFrozen()
		? menuparts::add(menu, "Unfreeze bus", "freeze.unfreeze_bus", self, false)
		: menuparts::add(menu, "Freeze bus", "freeze.bus", self, false);
}

} // namespace lmms::gui
