/*
 * ControlCommandsDeviceTools.cpp - rack.split: a rack's chains fed by band, mid / side or left / right
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
/*! 0.5.0 L10 (W3):
 *  - rack.split (MX-108): what each chain of a parallel rack is fed - the whole signal, one band of
 *    an up-to-six-way Linkwitz-Riley crossover, the mid or the side, the left or the right; the
 *    rack's sum puts the parts back together (include/RackSplit.h). Saved with the rack, one undo
 *    step. */

#include <algorithm>

#include <QJsonArray>

#include "ControlDeviceTools.h"
#include "ControlRackSupport.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "Rack.h"
#include "RackSplit.h"
#include "Song.h"

namespace lmms
{

namespace control
{

void addDeviceCommand(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args,
	const QStringList& required, bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd;
	cmd.id = QLatin1String(id);
	const int dot = cmd.id.indexOf(QLatin1Char('.'));
	cmd.group = cmd.id.left(dot);
	cmd.verb = cmd.id.mid(dot + 1);
	cmd.description = description;
	cmd.argsSchema = objectSchema(args, QJsonArray::fromStringList(required));
	cmd.resultSchema = objectProperty();
	cmd.mutating = mutating;
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace control

using namespace control;

namespace
{

//! The crossovers in @a args, checked and sorted into @a split.
bool readCrossovers(const QJsonObject& args, RackSplit* split, ControlResult* error)
{
	const QJsonArray values = args.value(QStringLiteral("crossovers")).toArray();
	if (values.size() > RackSplit::MaxBands - 1)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("at most %1 crossovers (%2 bands)").arg(RackSplit::MaxBands - 1).arg(RackSplit::MaxBands));
		return false;
	}
	for (const QJsonValue& value : values)
	{
		const double hz = value.toDouble(-1.0);
		if (!value.isDouble() || hz < RackSplit::MinCrossoverHz || hz > RackSplit::MaxCrossoverHz)
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("a crossover is a frequency in Hz between %1 and %2").arg(static_cast<double>(RackSplit::MinCrossoverHz)).arg(static_cast<double>(RackSplit::MaxCrossoverHz)));
			return false;
		}
		split->crossovers[static_cast<std::size_t>(split->crossoverCount++)] = static_cast<float>(hz);
	}
	std::sort(split->crossovers.begin(), split->crossovers.begin() + split->crossoverCount);
	return true;
}

void applySplit(const QString& channel, const RackSplit& split)
{
	ControlResult ignored;
	if (Rack* rack = resolveRack(channel, &ignored)) { rack->setSplit(split); }
}

QJsonObject splitResult(const QString& channel, const Rack& rack)
{
	QJsonObject result = rackSplitJson(rack);
	result.insert(QStringLiteral("channel"), channel);
	result.insert(QStringLiteral("chains"), rack.chainCount());
	return result;
}

ControlResult rackSplit(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveRackChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	Rack& rack = channel->m_rack;
	const QString id = channelIdOf(channel);
	if (!args.contains(QStringLiteral("mode"))) { return ControlResult::success(splitResult(id, rack)); }
	RackSplit split;
	RackSplit::modeFromName(args.value(QStringLiteral("mode")).toString(), &split.mode);
	if (!readCrossovers(args, &split, &error)) { return error; }
	if (split.active() && rack.chainCount() < 2)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("a split divides the input between the rack's chains: %1 has one - add a parallel chain first").arg(id));
	}
	const RackSplit before = rack.split();
	rack.setSplit(split);
	addUndoStep([id, before]() { applySplit(id, before); }, [id, split]() { applySplit(id, split); });
	Engine::getSong()->setModified();
	return ControlResult::success(splitResult(id, rack));
}

} // namespace

void registerDeviceToolCommands(ControlRegistry& registry)
{
	addDeviceCommand(registry, "rack.split", QStringLiteral("Feed each chain of a channel's parallel rack a part of the signal: "
		"'mode' bands (chain k gets band k, low to high, of a split at 'crossovers' - up to five frequencies in Hz, spread "
		"in octaves when fewer are given), mid_side (chain 0 the mid, chain 1 the side), left_right (chain 0 the left, chain 1 "
		"the right) or none (every chain the whole signal). The rack's sum puts the parts back together (the bands are "
		"Linkwitz-Riley crossovers that sum flat), so empty chains sound as the channel did. Applies while the rack routes in parallel. With no 'mode' it reports the split. Saved "
		"with the rack; one undo step."),
		{{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("mode"), enumProperty(RackSplit::modeNames())},
			{QStringLiteral("crossovers"), arrayProperty()}},
		{QStringLiteral("channel")}, true, rackSplit);
}

} // namespace lmms
