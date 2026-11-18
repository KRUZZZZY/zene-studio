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

#include <QPointer>

#include "ControlDevicePlacement.h"
#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlRackSupport.h"
#include "ControlReversibility.h"
#include "ControlStructuralSupport.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Rack.h"

namespace lmms::control
{

namespace
{

//! A copied device: keep its guarded instance while live, and its slot and plugin type across a chain rebuild.
struct RecordedDevice
{
	QPointer<Effect> effect;
	int slot;
	QString plugin;
};

using Made = std::shared_ptr<std::vector<RecordedDevice>>;

qint64 bytesOf(const QStringList& states)
{
	qint64 bytes = 0;
	for (const QString& state : states) { bytes += state.size(); }
	return bytes;
}

//! The step that takes the devices @a made out again; its redo recreates them from @a states at @a places.
//! A structural step: its settings documents count against the undo stack's byte budget (review #9).
void recordRecreation(const std::function<EffectChain*()>& chainOf, const Made& made, const QStringList& states)
{
	addStructuralUndoStep(
		[made, chainOf]() {
			EffectChain* chain = chainOf();
			for (auto it = made->rbegin(); it != made->rend(); ++it)
			{
				removeRecordedEffect(chain, it->effect, it->slot, it->plugin);
			}
		},
		[made, chainOf, states]() {
			// As recreateDevices itself: the first device that cannot be recreated stops the rest, so a redo never
			// leaves a later device without an earlier one (and no slot keeps a pointer from before the undo).
			EffectChain* chain = chainOf();
			bool stopped = chain == nullptr;
			for (std::size_t i = 0; i < made->size(); ++i)
			{
				RecordedDevice& device = (*made)[i];
				device.effect = stopped ? nullptr : recreateEffectFromState(chain, states.at(static_cast<int>(i)), device.slot);
				stopped = device.effect.isNull();
			}
		},
		bytesOf(states));
}

} // namespace

QJsonArray recreateDevices(EffectChain* chain, const QStringList& states, int position, ControlResult* error)
{
	return recreateDevices(recordedChainFinder(chain), states, position, error);
}

QJsonArray recreateDevices(const std::function<EffectChain*()>& chainOf, const QStringList& states, int position, ControlResult* error)
{
	EffectChain* chain = chainOf();
	if (chain == nullptr) { return {}; }
	auto made = std::make_shared<std::vector<RecordedDevice>>();
	QJsonArray ids;
	for (const QString& state : states)
	{
		const int at = position < 0 ? static_cast<int>(chain->effects().size()) : position + static_cast<int>(made->size());
		Effect* effect = recreateEffectFromState(chain, state, at);
		if (effect == nullptr)
		{
			if (std::optional<ControlResult> why = takeStructuralRestoreFailure()) { *error = *why; }
			break;
		}
		made->push_back({effect, placeEffect(chain, effect, at), QString::fromUtf8(effect->descriptor()->name)});
		ids.append(effectIdOf(effect));
	}
	if (!made->empty()) { recordRecreation(chainOf, made, states.mid(0, static_cast<int>(made->size()))); }
	return ids;
}

bool devicesRecordable(const std::vector<Effect*>& effects)
{
	return std::all_of(effects.begin(), effects.end(),
		[](Effect* effect) { return controlEffectStateXml(effect).size() <= StructuralSnapshotLimit; });
}

void removeDevicesRecorded(EffectChain* chain, const std::vector<Effect*>& effects)
{
	removeDevicesRecorded(recordedChainFinder(chain), effects);
}

void removeDevicesRecorded(const std::function<EffectChain*()>& chainOf, const std::vector<Effect*>& effects)
{
	EffectChain* chain = chainOf();
	for (auto it = effects.rbegin(); it != effects.rend() && chain != nullptr; ++it)
	{
		const std::vector<Effect*>& now = chain->effects();
		const auto at = std::find(now.begin(), now.end(), *it);
		if (at == now.end()) { continue; }
		const int slot = static_cast<int>(at - now.begin());
		const QString state = controlEffectStateXml(*it);
		const QString plugin = QString::fromUtf8((*it)->descriptor()->name);
		// The chain is found again on every run (review L10-L03-L02-1 #2): a rack chain an undo deleted
		// and a redo made again is a new object.
		auto holder = std::make_shared<QPointer<Effect>>(*it);
		addStructuralUndoStep(
			[chainOf, state, slot, holder]() {
				if (EffectChain* current = chainOf()) { *holder = recreateEffectFromState(current, state, slot); }
			},
			[chainOf, holder, slot, plugin]() { removeRecordedEffect(chainOf(), *holder, slot, plugin); },
			state.size());
		chain->removeEffect(*it);
		(*it)->deleteLater();
	}
}

bool journalChainRemoval(const QString& channelId, int index, const QStringList& states, int selectedBefore)
{
	ControlResult ignored;
	Rack* rack = resolveRack(channelId, &ignored);
	// Rack::addChain appends: only the LAST chain can be put back where it was. An inner chain records nothing, and the
	// caller says so in its transaction rather than claim an inverse that would come back at the wrong index.
	if (rack == nullptr || index != rack->chainCount() - 1) { return false; }
	// The writer's answer is the answer: a step the journal refused to record is no
	// inverse, and the caller (rack.remove_chain) must not claim one (new-p14-81).
	return addStructuralUndoStep(
		[channelId, index, states, selectedBefore]() {
			ControlResult unused;
			Rack* current = resolveRack(channelId, &unused);
			if (current == nullptr || current->chainCount() != index) { return; }
			current->addChain();
			// As recreateDevices: the first device that cannot be re-created stops the rest, and its failure is recorded for
			// control.undo / control.redo to answer with (takeStructuralRestoreFailure).
			for (int slot = 0; slot < states.size(); ++slot)
			{
				if (recreateEffectFromState(current->chain(index), states.at(slot), slot) == nullptr) { break; }
			}
			current->setSelectedChain(selectedBefore);
		},
		[channelId, index]() {
			ControlResult unused;
			Rack* current = resolveRack(channelId, &unused);
			if (current != nullptr && index < current->chainCount()) { current->removeChain(index); }
		},
		bytesOf(states));
}

} // namespace lmms::control
