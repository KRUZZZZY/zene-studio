/*
 * ControlDeviceMoves.cpp - devices recreated or removed on a chain, each recorded for one undo step
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
#include "ControlDeviceMoves.h"

#include <algorithm>
#include <memory>

#include "ControlDevicePlacement.h"
#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlStructuralSupport.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"

namespace lmms::control
{

namespace
{

using Made = std::shared_ptr<std::vector<Effect*>>;

//! The step that takes the devices @a made out again; its redo recreates them from @a states at @a places.
void recordRecreation(const std::function<EffectChain*()>& chainOf, const Made& made, const QStringList& states,
	const std::vector<int>& places)
{
	addUndoStep(
		[made, chainOf]() {
			EffectChain* chain = chainOf();
			for (auto it = made->rbegin(); it != made->rend() && chain != nullptr; ++it)
			{
				if (*it == nullptr) { continue; }
				chain->removeEffect(*it);
				(*it)->deleteLater();
				*it = nullptr;
			}
		},
		[made, chainOf, states, places]() {
			EffectChain* chain = chainOf();
			for (std::size_t i = 0; i < made->size() && chain != nullptr; ++i)
			{
				(*made)[i] = recreateEffectFromState(chain, states.at(static_cast<int>(i)), places[i]);
			}
		});
}

} // namespace

QJsonArray recreateDevices(EffectChain* chain, const QStringList& states, int position, ControlResult* error)
{
	return recreateDevices([chain] { return chain; }, states, position, error);
}

QJsonArray recreateDevices(const std::function<EffectChain*()>& chainOf, const QStringList& states, int position, ControlResult* error)
{
	EffectChain* chain = chainOf();
	auto made = std::make_shared<std::vector<Effect*>>();
	std::vector<int> places;
	QJsonArray ids;
	for (const QString& state : states)
	{
		const int at = position < 0 ? static_cast<int>(chain->effects().size()) : position + static_cast<int>(places.size());
		Effect* effect = recreateEffectFromState(chain, state, at);
		if (effect == nullptr)
		{
			if (std::optional<ControlResult> why = takeStructuralRestoreFailure()) { *error = *why; }
			break;
		}
		made->push_back(effect);
		places.push_back(placeEffect(chain, effect, at));
		ids.append(effectIdOf(effect));
	}
	if (!made->empty()) { recordRecreation(chainOf, made, states.mid(0, static_cast<int>(made->size())), places); }
	return ids;
}

bool devicesRecordable(const std::vector<Effect*>& effects)
{
	return std::all_of(effects.begin(), effects.end(),
		[](Effect* effect) { return controlEffectStateXml(effect).size() <= StructuralSnapshotLimit; });
}

void removeDevicesRecorded(EffectChain* chain, const std::vector<Effect*>& effects)
{
	for (auto it = effects.rbegin(); it != effects.rend(); ++it)
	{
		const std::vector<Effect*>& now = chain->effects();
		const auto at = std::find(now.begin(), now.end(), *it);
		if (at == now.end()) { continue; }
		journalEffectRemoval(chain, *it, static_cast<int>(at - now.begin()));
		chain->removeEffect(*it);
		(*it)->deleteLater();
	}
}

} // namespace lmms::control
