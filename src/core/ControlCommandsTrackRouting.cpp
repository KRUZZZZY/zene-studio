/*
 * ControlCommandsTrackRouting.cpp - track.add_to_new_bus: several new tracks and the channel they share (Q050-212)
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

#include <memory>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlTrackCreation.h"
#include "ControlTrackRouting.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxTracksOnNewBus = 64;
constexpr int MaxBusNameLength = 64;

/*! Everything one call makes, kept by id for its undo step: a redo makes the channel again under the same id and fresh
 *  tracks, whose ids are written back here. */
struct BusBuild
{
	Track::Type type = Track::Type::Instrument;
	int count = 0;
	QString busName;
	NewTrackSpec spec;
	int channelId = -1;
	std::vector<int> trackIds;
};

QString numberedName(const QString& name, int number, int count)
{
	return name.isEmpty() || count == 1 ? name : QStringLiteral("%1 %2").arg(name).arg(number);
}

void build(BusBuild* made)
{
	const MixerChannel* channel = makeNamedChannel(made->busName, &made->channelId);
	NewTrackSpec spec = made->spec;
	spec.output = channelIdOf(channel);
	const QString baseName = spec.name;
	made->trackIds.clear();
	for (int i = 1; i <= made->count; ++i)
	{
		Track* track = Track::create(made->type, Engine::getSong());
		spec.name = numberedName(baseName, i, made->count);
		finishNewTrack(track, spec, &made->channelId);
		made->trackIds.push_back(track->id());
	}
}

void unbuild(const BusBuild* made)
{
	for (auto it = made->trackIds.crbegin(); it != made->trackIds.crend(); ++it)
	{
		if (Track* live = trackWithId(*it)) { removeTrack(live); }
	}
	deleteChannelWithId(made->channelId);
}

ControlResult addToNewBus(const QJsonObject& args)
{
	auto made = std::make_shared<BusBuild>();
	made->type = args.value(QStringLiteral("type")).toString() == QLatin1String("sample") ? Track::Type::Sample : Track::Type::Instrument;
	made->count = args.value(QStringLiteral("count")).toInt();
	made->busName = args.value(QStringLiteral("bus_name")).toString().trimmed();
	if (made->count < 1 || made->count > MaxTracksOnNewBus)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'count' is 1..%1").arg(MaxTracksOnNewBus));
	}
	if (made->busName.size() > MaxBusNameLength)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'bus_name' is at most %1 characters").arg(MaxBusNameLength));
	}
	ControlResult refusal;
	if (!readNewTrackSpec(args, made->type, &made->spec, &refusal)) { return refusal; }
	made->spec.ownChannel = false;  // the bus is their destination: the explicit one wins over the preference
	const int channelsBefore = static_cast<int>(Engine::mixer()->numChannels());
	if (made->busName.isEmpty()) { made->busName = QStringLiteral("Bus %1").arg(channelsBefore); }

	addUndoStep([made]() { unbuild(made.get()); }, [made]() { build(made.get()); });
	build(made.get());

	QJsonArray tracks;
	for (const int id : made->trackIds) { tracks.append(trackId(id)); }
	const QString channel = channelId(made->channelId);
	QJsonObject result{{QStringLiteral("channel"), channel}, {QStringLiteral("bus_name"), made->busName},
		{QStringLiteral("tracks"), tracks}, {QStringLiteral("track_count"), static_cast<int>(Engine::getSong()->tracks().size())}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("channels"), channelsBefore}},
		QStringLiteral("control.undo"), {}, true,
		QStringLiteral("action checkpoint: one recorded undo step removes the created tracks and the created channel")));
	return ControlResult::success(result);
}

} // namespace

void registerTrackRoutingCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.add_to_new_bus");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("add_to_new_bus");
	cmd.description = QStringLiteral("Add `count` (1..64) instrument or sample tracks (`type`, default instrument) all playing into "
		"one new mixer channel, the bus they share: a regular channel, since a parallel bus (bus.create) takes no track "
		"input. `bus_name` names it (default Bus <n>). `name` names the tracks, numbered when there are several. "
		"`instrument` loads that plug-in on each. One undo step removes the tracks and the channel; redo makes "
		"them again, the channel under the same ch-<n> and the tracks with fresh trk-<n> ids.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("count"), integerProperty(1, MaxTracksOnNewBus)},
		{QStringLiteral("type"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
			{QStringLiteral("enum"), QJsonArray{QStringLiteral("instrument"), QStringLiteral("sample")}}}},
		{QStringLiteral("bus_name"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("instrument"), stringProperty()},
	}, {QStringLiteral("count")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("bus_name"), stringProperty()},
		{QStringLiteral("tracks"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("track_count"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = addToNewBus;
	registry.registerCommand(cmd);
}

} // namespace lmms
