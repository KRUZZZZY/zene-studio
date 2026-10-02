/*
 * ControlDevicePlacement.cpp - where plugin.load puts a device: which rack chain, which slot
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

#include "ControlDevicePlacement.h"

#include <algorithm>

#include "ControlRackSupport.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Mixer.h"
#include "Rack.h"

namespace lmms::control
{

EffectChain* resolveDeviceChain(const ControlTarget& target, const QJsonObject& args, ControlResult* error)
{
	const int index = args.value(QStringLiteral("chain")).toInt(0);
	if (index == 0) { return target.chain; }
	if (target.kind != QLatin1String("channel"))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is a track: only a mixer channel's rack has parallel chains (chain %2)")
				.arg(target.id).arg(index));
		return nullptr;
	}
	Rack* rack = resolveRack(target.id, error);
	if (rack == nullptr) { return nullptr; }
	if (index < 0 || index >= rack->chainCount())
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1's rack has no chain %2 (it has %3; rack.add_chain adds one)")
				.arg(target.id).arg(index).arg(rack->chainCount()));
		return nullptr;
	}
	return rack->chain(index);
}

bool resolveChainTarget(const QJsonObject& args, ControlTarget* target, ControlResult* error)
{
	if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), target, error)) { return false; }
	EffectChain* chain = resolveDeviceChain(*target, args, error);
	if (chain == nullptr) { return false; }
	target->chain = chain;
	return true;
}

int placeEffect(EffectChain* chain, Effect* effect, int position)
{
	const std::vector<Effect*>& effects = chain->effects();
	const auto at = std::find(effects.begin(), effects.end(), effect);
	if (at == effects.end()) { return -1; }
	int slot = static_cast<int>(at - effects.begin());
	if (position < 0) { return slot; }
	const int target = std::min(position, static_cast<int>(effects.size()) - 1);
	for (; slot > target; --slot) { chain->moveUp(effect); }
	return slot;
}

} // namespace lmms::control
