/*
 * ControlRackSupport.cpp - the helpers the rack.* command group shares between
 *                         its rack half (ControlCommandsRack.cpp) and its macro
 *                         and zone halves.
 *
 * They live in their own translation unit rather than in the file that
 * registers the group, for the same reason include/ControlDeviceSupport.h has
 * one: a helper the two halves would otherwise re-derive is exactly the drift
 * this split prevents (four merge repairs in this tree were two lanes deriving
 * the same helper).
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
 */

#include "ControlRackSupport.h"

#include <cstddef>

#include <QJsonArray>

#include "AutomatableModel.h"
#include "ControlVocabulary.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "Rack.h"
#include "RackMacros.h"
#include "RackZones.h"

namespace lmms
{

using namespace control;

MixerChannel* resolveRackChannel(const QString& id, ControlResult* error)
{
	const int wanted = idToIndex(id, QStringLiteral("ch-"));
	if (wanted < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a channel id of the form ch-<n>").arg(id));
		return nullptr;
	}
	Mixer* mixer = Engine::mixer();
	// By id, not by position (SPEC-stable-ids.md slice 2): the number names the
	// channel OBJECT (MixerChannel::id()), so the "ch-<n>" strings this group
	// records in its undo steps (rack.add_chain, rack.remove_chain,
	// rack.set_selected, the macro and zone halves) still name the same channel
	// when the step runs after a sibling channel was deleted or the mixer was
	// reordered. No positional fallback: every channel carries an id from
	// construction, so a fallback could only ever resolve a stale position.
	MixerChannel* channel = nullptr;
	if (mixer != nullptr)
	{
		for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
		{
			MixerChannel* candidate = mixer->mixerChannel(i);
			if (candidate != nullptr && candidate->id() == wanted)
			{
				channel = candidate;
				break;
			}
		}
	}
	if (channel == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no mixer channel %1").arg(id));
		return nullptr;
	}
	return channel;
}

Rack* resolveRack(const QString& id, ControlResult* error)
{
	MixerChannel* channel = resolveRackChannel(id, error);
	return channel != nullptr ? &channel->m_rack : nullptr;
}

QString macroId(int index)
{
	return QStringLiteral("macro-%1").arg(index);
}

QString zoneId(int index)
{
	return QStringLiteral("zone-%1").arg(index);
}

QJsonObject macroTargetJson(const RackMacroTarget& target, int index)
{
	QJsonObject out;
	out.insert(QStringLiteral("index"), index);
	out.insert(QStringLiteral("chain"), target.chain);
	out.insert(QStringLiteral("effect"), target.effect);
	out.insert(QStringLiteral("parameter"), target.parameter);
	out.insert(QStringLiteral("low"), static_cast<double>(target.low));
	out.insert(QStringLiteral("high"), static_cast<double>(target.high));
	return out;
}

QJsonObject macroJson(const RackMacro& macro, int index)
{
	QJsonObject out;
	out.insert(QStringLiteral("macro"), macroId(index));
	out.insert(QStringLiteral("name"), macro.name);
	out.insert(QStringLiteral("value"), static_cast<double>(macro.value));

	QJsonArray targets;
	for (std::size_t i = 0; i < macro.targets.size(); ++i)
	{
		targets.append(macroTargetJson(macro.targets[i], static_cast<int>(i)));
	}
	out.insert(QStringLiteral("targets"), targets);
	out.insert(QStringLiteral("target_count"), targets.size());
	return out;
}

QJsonObject zoneJson(const RackZone& zone, int index)
{
	QJsonObject out;
	out.insert(QStringLiteral("zone"), zoneId(index));
	out.insert(QStringLiteral("low_key"), zone.lowKey);
	out.insert(QStringLiteral("high_key"), zone.highKey);
	out.insert(QStringLiteral("low_velocity"), zone.lowVelocity);
	out.insert(QStringLiteral("high_velocity"), zone.highVelocity);
	out.insert(QStringLiteral("chain"), zone.chain);
	out.insert(QStringLiteral("sample"), zone.sample);
	return out;
}

int resolveMacroIndex(const Rack& rack, const QString& id, ControlResult* error)
{
	const int index = idToIndex(id, QStringLiteral("macro-"));
	if (index < 0 || rack.macros().macro(index) == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("'%1' is not a macro of this rack (it has %2, macro-0..macro-%3)")
				.arg(id)
				.arg(rack.macros().macroCount())
				.arg(rack.macros().macroCount() - 1));
		return -1;
	}
	return index;
}

int resolveZoneIndex(const Rack& rack, const QString& id, ControlResult* error)
{
	const int index = idToIndex(id, QStringLiteral("zone-"));
	if (index < 0 || rack.zones().zone(index) == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("'%1' is not a zone of this rack (it has %2, zone-0..zone-%3)")
				.arg(id)
				.arg(rack.zones().zoneCount())
				.arg(rack.zones().zoneCount() - 1));
		return -1;
	}
	return index;
}

void writeMacroAssignments(const MacroRestore& restore)
{
	ControlResult ignored;
	// restore.channelId is the channel's PERSISTENT id, so the step writes into
	// the channel it was recorded for even after a sibling channel was deleted
	// or the mixer was reordered (SPEC-stable-ids.md slice 2); a channel that
	// is gone makes resolveRack answer nullptr and the step does nothing -
	// the skip rule this struct's own comment states.
	Rack* rack = resolveRack(restore.channelId, &ignored);
	if (rack == nullptr) { return; }
	rack->macros().setValue(restore.macro, restore.value);
	for (const std::pair<RackMacroTarget, float>& entry : restore.parameters)
	{
		QString why;
		AutomatableModel* model = rackMacroTargetModel(*rack, entry.first, &why);
		if (model == nullptr) { continue; }
		model->setValue(entry.second);
	}
}

QJsonObject rackState(MixerChannel* channel, Rack& rack)
{
	QJsonObject out;
	out.insert(QStringLiteral("channel"), channelIdOf(channel));
	out.insert(QStringLiteral("chain_count"), rack.chainCount());
	out.insert(QStringLiteral("selected"), rack.selectedChain());

	QJsonArray chains;
	for (int i = 0; i < rack.chainCount(); ++i)
	{
		EffectChain* chain = rack.chain(i);
		QJsonObject entry;
		entry.insert(QStringLiteral("chain"), i);
		entry.insert(QStringLiteral("devices"),
			chain != nullptr ? static_cast<int>(chain->effects().size()) : 0);
		chains.append(entry);
	}
	out.insert(QStringLiteral("chains"), chains);

	QJsonArray routed;
	for (const int index : rack.routedChains()) { routed.append(index); }
	out.insert(QStringLiteral("routed"), routed);

	QJsonArray macros;
	for (int i = 0; i < rack.macros().macroCount(); ++i)
	{
		const RackMacro* macro = rack.macros().macro(i);
		if (macro != nullptr) { macros.append(macroJson(*macro, i)); }
	}
	out.insert(QStringLiteral("macros"), macros);
	out.insert(QStringLiteral("macro_count"), macros.size());

	QJsonArray zones;
	for (int i = 0; i < rack.zones().zoneCount(); ++i)
	{
		const RackZone* zone = rack.zones().zone(i);
		if (zone != nullptr) { zones.append(zoneJson(*zone, i)); }
	}
	out.insert(QStringLiteral("zones"), zones);
	out.insert(QStringLiteral("zone_count"), zones.size());
	return out;
}

} // namespace lmms
