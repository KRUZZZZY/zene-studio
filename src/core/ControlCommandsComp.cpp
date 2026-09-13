/*
 * ControlCommandsComp.cpp - the comp.* group's TAKE half: the lanes and the
 *                           assignment of takes to them.
 *
 * The composite half (comp.select / comp.rebuild / comp.get_state) is
 * ControlCommandsCompEdits.cpp, the same split the clip and warp groups use, and
 * for the same reason: the group's boilerplate alone does not fit under the
 * file-length ratchet, and the two halves are genuinely different operations
 * (lane membership vs. the composite view).
 *
 * docs/COMPING.md holds the decisions this file implements: the group's name,
 * the element shape, what a composite is, and what is deliberately NOT wired.
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

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "Clip.h"
#include "ControlCompSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The reason every row of this half records: the lane list and the composite
//! are fields of the Track's own serialized state.
const QString ClauseTrackJournalled = QStringLiteral("ProjectJournal (Track checkpoint: "
	"Track::saveTrack writes the <takelanes> element and Track::loadTrack re-reads it, so "
	"the checkpoint restores the lane list, the composite and every segment that named the "
	"removed lane in one undo)");

//! The wire names, in one place: five commands read "track" and two read "lane".
const QString ArgTrack = QStringLiteral("track");
const QString ArgLane = QStringLiteral("lane");
const QString ArgName = QStringLiteral("name");

//! "lanes 0, 2" - the lanes a refusal tells the caller they could have named.
QString laneIndexList(const TakeLaneModel& model)
{
	QStringList indexes;
	for (const TakeLane& lane : model.lanes())
	{
		indexes.append(QString::number(lane.index));
	}
	if (indexes.isEmpty()) { return QStringLiteral("none"); }
	return indexes.join(QStringLiteral(", "));
}

//! The id of \p lane as a string of the lane model, for a result's own copy.
QString laneNameOf(const TakeLaneModel& model, int index)
{
	for (const TakeLane& lane : model.lanes())
	{
		if (lane.index == index) { return lane.name; }
	}
	return QString();
}

//! The state every lane-mutating result reports: the lane list with each lane's
//! takes, and how many lanes there now are.
void insertLaneState(QJsonObject* result, const Track* track)
{
	const QJsonArray lanes = lanesState(track);
	result->insert(QStringLiteral("lanes"), lanes);
	result->insert(QStringLiteral("lane_count"), lanes.size());
}

Track* resolveTrackArg(const QJsonObject& args, ControlResult* error)
{
	return resolveTrack(args.value(ArgTrack).toString(), error);
}

void registerCompLaneAdd(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("comp.lane_add");
	cmd.group = QStringLiteral("comp");
	cmd.verb = QStringLiteral("lane_add");
	cmd.description = QStringLiteral("Add a take lane to a track and return its index. The "
		"index is the lowest one the track does not use; a lane is never renumbered by a "
		"removal (docs/COMPING.md). The lane holds no audio of its own: the takes are the "
		"clips tagged with that index, assigned with comp.assign. Reversible through the "
		"ProjectJournal (Track checkpoint).");
	cmd.argsSchema = objectSchema({
		{ArgTrack, stringProperty()},
		{ArgName, stringProperty()},
	}, {ArgTrack});
	cmd.resultSchema = objectSchema({
		{ArgTrack, stringProperty()},
		{ArgLane, integerProperty(0, MaxSongLength)},
		{ArgName, stringProperty()},
		{QStringLiteral("lanes"), arrayProperty()},
		{QStringLiteral("lane_count"), integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		Track* track = resolveTrackArg(args, &error);
		if (track == nullptr) { return error; }
		const QString id = trackIdOf(track);
		const QString name = args.value(ArgName).toString();
		const QJsonArray before = lanesState(track);

		TakeLaneModel& model = track->takeLanes();
		track->addJournalCheckPoint();
		const int lane = model.addLane(name);

		QJsonObject result;
		result.insert(ArgTrack, id);
		result.insert(ArgLane, lane);
		result.insert(ArgName, laneNameOf(model, lane));
		insertLaneState(&result, track);
		// The recorded inverse is a REAL command this surface implements, not a
		// manual note: comp.lane_remove takes the lane back out.
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(QJsonObject{{ArgTrack, id}, {QStringLiteral("lanes"), before}},
				QStringLiteral("comp.lane_remove"),
				QJsonObject{{ArgTrack, id}, {ArgLane, lane}}, true, ClauseTrackJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}


void registerCompLaneRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("comp.lane_remove");
	cmd.group = QStringLiteral("comp");
	cmd.verb = QStringLiteral("lane_remove");
	cmd.description = QStringLiteral("Remove a take lane from a track. Any composite segment "
		"that named the lane falls back to the track's base lane, because a composite stays "
		"gapless over its span; removing the last lane clears the composite. A lane the track "
		"does not have is refused, typed, naming the lanes it does have. Reversible through "
		"the ProjectJournal (Track checkpoint).");
	cmd.argsSchema = objectSchema({
		{ArgTrack, stringProperty()},
		{ArgLane, integerProperty(0, MaxSongLength)},
	}, {ArgTrack, ArgLane});
	cmd.resultSchema = objectSchema({
		{ArgTrack, stringProperty()},
		{ArgLane, integerProperty(0, MaxSongLength)},
		{ArgName, stringProperty()},
		{QStringLiteral("lanes"), arrayProperty()},
		{QStringLiteral("lane_count"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("composite"), objectProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		Track* track = resolveTrackArg(args, &error);
		if (track == nullptr) { return error; }
		const QString id = trackIdOf(track);
		TakeLaneModel& model = track->takeLanes();
		const int lane = args.value(ArgLane).toInt(-1);
		if (lane < 0)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'%1' must be a lane index (>= 0), got %2")
					.arg(ArgLane).arg(args.value(ArgLane).toInt()));
		}
		if (!model.hasLane(lane))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("%1 has no take lane %2 (its lanes: %3)")
					.arg(id).arg(lane).arg(laneIndexList(model)));
		}
		const QString name = laneNameOf(model, lane);
		const QJsonArray beforeLanes = lanesState(track);
		const QJsonObject beforeComposite = compositeState(track);

		track->addJournalCheckPoint();
		model.removeLane(lane);

		QJsonObject result;
		result.insert(ArgTrack, id);
		result.insert(ArgLane, lane);
		result.insert(ArgName, name);
		insertLaneState(&result, track);
		result.insert(QStringLiteral("composite"), compositeState(track));
		QJsonObject before;
		before.insert(ArgTrack, id);
		before.insert(QStringLiteral("lanes"), beforeLanes);
		before.insert(QStringLiteral("composite"), beforeComposite);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(before,
				QStringLiteral("UNIMPLEMENTED: re-add the lane at its own index (comp.lane_add "
					"hands out the lowest free one) and re-point the segments that named it; the "
					"replacement lane's name is in this before-state"),
				QJsonObject{{ArgTrack, id}, {ArgLane, lane}, {ArgName, name}},
				true, ClauseTrackJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}


void registerCompLaneList(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("comp.lane_list");
	cmd.group = QStringLiteral("comp");
	cmd.verb = QStringLiteral("lane_list");
	cmd.description = QStringLiteral("List take lanes: the named track's, or every track's in "
		"song order when no track is given. Each lane reports its index, its name and the ids "
		"of the takes assigned to it (comp.assign). Reads only.");
	cmd.argsSchema = objectSchema({
		{ArgTrack, stringProperty()},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("tracks"), arrayProperty()},
		{QStringLiteral("count"), integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		QJsonArray tracks;
		if (args.contains(ArgTrack))
		{
			ControlResult error;
			Track* track = resolveTrackArg(args, &error);
			if (track == nullptr) { return error; }
			QJsonObject entry;
			entry.insert(ArgTrack, trackIdOf(track));
			entry.insert(QStringLiteral("name"), track->name());
			entry.insert(QStringLiteral("lanes"), lanesState(track));
			tracks.append(entry);
		}
		else
		{
			for (Track* track : Engine::getSong()->tracks())
			{
				QJsonObject entry;
				entry.insert(ArgTrack, trackIdOf(track));
				entry.insert(QStringLiteral("name"), track->name());
				entry.insert(QStringLiteral("lanes"), lanesState(track));
				tracks.append(entry);
			}
		}
		QJsonObject result;
		result.insert(QStringLiteral("tracks"), tracks);
		result.insert(QStringLiteral("count"), tracks.size());
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}


void registerCompAssign(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("comp.assign");
	cmd.group = QStringLiteral("comp");
	cmd.verb = QStringLiteral("assign");
	cmd.description = QStringLiteral("Assign an audio clip to a take lane of its own track: "
		"the lane tag is a field on the clip, so the take is the clip and its audio is never "
		"copied or moved. A lane the clip's track does not have is refused, as is a MIDI clip "
		"(take lanes carry audio takes in this release; docs/COMPING.md). Reversible through "
		"the ProjectJournal (Clip checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{ArgLane, integerProperty(0, MaxSongLength)},
	}, {QStringLiteral("clip"), ArgLane});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{ArgTrack, stringProperty()},
		{ArgLane, integerProperty(0, MaxSongLength)},
		{QStringLiteral("previous_lane"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("lanes"), arrayProperty()},
		{QStringLiteral("lane_count"), integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ClipRef ref;
		if (!resolveTakeClip(args.value(QStringLiteral("clip")).toString(), &ref, &error))
		{
			return error;
		}
		const int lane = args.value(ArgLane).toInt(-1);
		if (lane < 0)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'%1' must be a lane index (>= 0), got %2")
					.arg(ArgLane).arg(args.value(ArgLane).toInt()));
		}
		TakeLaneModel& model = ref.track->takeLanes();
		if (!model.hasLane(lane))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("%1 has no take lane %2 (its lanes: %3); add one with "
					"comp.lane_add").arg(trackIdOf(ref.track)).arg(lane).arg(laneIndexList(model)));
		}
		const QString clipIdText = clipId(ref.ordinal);
		const int previous = ref.clip->laneIndex();

		ref.clip->addJournalCheckPoint();
		ref.clip->setLaneIndex(lane);

		QJsonObject result;
		result.insert(QStringLiteral("clip"), clipIdText);
		result.insert(ArgTrack, trackIdOf(ref.track));
		result.insert(ArgLane, lane);
		result.insert(QStringLiteral("previous_lane"), previous);
		insertLaneState(&result, ref.track);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(
				QJsonObject{{QStringLiteral("clip"), clipIdText}, {ArgLane, previous}},
				QStringLiteral("comp.assign"),
				QJsonObject{{QStringLiteral("clip"), clipIdText}, {ArgLane, previous}},
				true, QStringLiteral("ProjectJournal (Clip checkpoint: Clip::saveClipEdits writes "
					"the lane attribute onto the clip's own element)")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace


void registerCompCommands(ControlRegistry& registry)
{
	registerCompLaneAdd(registry);
	registerCompLaneRemove(registry);
	registerCompLaneList(registry);
	registerCompAssign(registry);
}

} // namespace lmms
