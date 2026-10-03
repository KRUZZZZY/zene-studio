/*
 * ControlDeviceMoves.h - devices recreated or removed on a chain, each recorded for one undo step
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
#ifndef LMMS_CONTROL_DEVICE_MOVES_H
#define LMMS_CONTROL_DEVICE_MOVES_H

#include <functional>
#include <vector>

#include <QJsonArray>
#include <QStringList>

#include "ControlRegistry.h"
#include "lmms_export.h"

namespace lmms
{

class Effect;
class EffectChain;

namespace control
{

/*! 0.5.0 L10: the moves plugin.copy, chain.copy and rack.wrap share. Every helper records its own
 *  inverse (the registry merges a command's steps into one undo step), so a command composes them. */

//! Recreate each settings document in @a states on @a chain from slot @a position on (negative:
//! appended), recording one step that takes them out again. The fx-<n> of each; stops at the first
//! device that cannot be recreated (@a error says why).
LMMS_EXPORT QJsonArray recreateDevices(EffectChain* chain, const QStringList& states, int position, ControlResult* error);
//! The same for a chain that an undo may delete and its redo create again (a rack chain): @a chainOf
//! finds it each time, so the recorded step never holds a pointer to a chain that is gone.
LMMS_EXPORT QJsonArray recreateDevices(const std::function<EffectChain*()>& chainOf, const QStringList& states, int position,
	ControlResult* error);

//! Whether each of @a effects can be recorded for an undo (its settings document is in bounds).
LMMS_EXPORT bool devicesRecordable(const std::vector<Effect*>& effects);

//! Take @a effects off @a chain, last first, each removal recorded (its undo recreates it at its slot).
LMMS_EXPORT void removeDevicesRecorded(EffectChain* chain, const std::vector<Effect*>& effects);
//! The same for a chain an undo may delete and a redo create again, found by @a chainOf on every run.
LMMS_EXPORT void removeDevicesRecorded(const std::function<EffectChain*()>& chainOf, const std::vector<Effect*>& effects);

} // namespace control
} // namespace lmms

#endif // LMMS_CONTROL_DEVICE_MOVES_H
