/*
 * ControlDevicePlacement.h - where plugin.load puts a device: which rack chain, which slot
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

#ifndef LMMS_CONTROL_DEVICE_PLACEMENT_H
#define LMMS_CONTROL_DEVICE_PLACEMENT_H

#include <QJsonObject>

#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "lmms_export.h"

namespace lmms
{

class Effect;
class EffectChain;

namespace control
{

/*! The chain a device command addresses: the target's own chain (chain 0) or, with `chain` >= 1,
 *  parallel chain n of a mixer channel's rack (rack.add_chain makes them). nullptr with @a error
 *  for a chain that does not exist or a track target asked for a rack chain (a track has none).
 *  Engine audit E16: before this, plugin.load could never fill a rack chain. */
LMMS_EXPORT EffectChain* resolveDeviceChain(const ControlTarget& target, const QJsonObject& args,
	ControlResult* error);

//! resolveControlTarget for `target`, then the chain `chain` names (resolveDeviceChain) in place
//! of the target's own: what chain.save and chain.apply address (engine audit E17).
LMMS_EXPORT bool resolveChainTarget(const QJsonObject& args, ControlTarget* target, ControlResult* error);

//! Move @a effect (just appended to @a chain) to slot @a position (0 = first), clamped to the
//! chain; a negative position leaves it at the end. Returns the slot it ends in.
LMMS_EXPORT int placeEffect(EffectChain* chain, Effect* effect, int position);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_DEVICE_PLACEMENT_H
