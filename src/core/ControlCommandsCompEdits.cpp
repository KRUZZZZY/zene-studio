/*
 * ControlCommandsCompEdits.cpp - the comp.* group's COMPOSITE half: the per-segment
 *                                 selection, the rebuild, and the state query.
 *
 * The take half (comp.lane_add / lane_remove / lane_list / assign) is
 * ControlCommandsComp.cpp. The two files register separately and both declare
 * group="comp", exactly like the clip and warp groups' edit halves.
 *
 * What the composite IS, in one line: an ordered, gapless list of {tick range,
 * lane, source offset} choices, resolved back onto the take clips the track
 * already holds. It copies no audio and writes no take - docs/COMPING.md.
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
#include <QStringList>

#include "ControlCompSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Song.h"   // MaxSongLength, the schema's upper bound
#include "TakeLane.h"
#include "Track.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

const QString ArgTrack = QStringLiteral("track");
const QString ArgLane = QStringLiteral("lane");
const QString ArgBegin = QStringLiteral("begin");
const QString ArgEnd = QStringLiteral("end");
const QString ArgSrcpos = QStringLiteral("srcpos");

const QString ClauseTrackJournalled = QStringLiteral("ProjectJournal (Track checkpoint: "
	"Track::saveTrack writes the <takelanes> element - the lane list AND the composite - and "
	"Track::loadTrack re-reads it)");

//! The reason a result's transaction records when no single command re-creates the
//! previous composite. The checkpoint is the real inverse; this names what would.
const QString ManualCompositeInverse = QStringLiteral(
	"UNIMPLEMENTED: re-select each previous segment (there is no un-select, because a "
	"composite is total over its span); the previous segment list is in this before-state");

//! "lanes 0, 2" - what a refusal tells the caller they could have named instead.
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

/*! Validates the range the composite commands take. A reversed or empty range is
 *  a REFUSAL, never a silent clamp: the caller asked for something the model
 *  cannot express, and the design's I4 rule says a violated bound is rejected
 *  rather than guessed at. */
bool checkRange(int begin, int end, QString* reason)
{
	if (begin < 0)
	{
		*reason = QStringLiteral("'%1' must be >= 0, got %2").arg(ArgBegin).arg(begin);
		return false;
	}
	if (end <= begin)
	{
		*reason = QStringLiteral("'%1' (%2) must be greater than '%3' (%4)")
			.arg(ArgEnd).arg(end).arg(ArgBegin).arg(begin);
		return false;
	}
	return true;
}

//! The keys a selection result carries to say which range it just decided.
void insertRange(QJsonObject* result, int begin, int end, int lane, int srcpos)
{
	result->insert(ArgBegin, begin);
	result->insert(ArgEnd, end);
	result->insert(ArgLane, lane);
	result->insert(ArgSrcpos, srcpos);
}

void registerCompSelect(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("comp.select");
	cmd.group = QStringLiteral("comp");
	cmd.verb = QStringLiteral("select");
	cmd.description = QStringLiteral("Choose which take lane supplies the composite over "
		"[begin, end) ticks, slipped 'srcpos' ticks into that lane's take (default 0 = the "
		"take's own start). A selection paints over what was there: the range is cut out of "
		"every existing segment and the new choice is inserted, so the composite stays "
		"ordered and gapless over its span. The take audio is not touched, and nothing is "
		"copied. Reversible through the ProjectJournal (Track checkpoint).");
	cmd.argsSchema = objectSchema({
		{ArgTrack, stringProperty()},
		{ArgBegin, tickProperty()},
		{ArgEnd, tickProperty()},
		{ArgLane, integerProperty(0, MaxSongLength)},
		{ArgSrcpos, tickProperty()},
	}, {ArgTrack, ArgBegin, ArgEnd, ArgLane});
	cmd.resultSchema = objectSchema({
		{ArgTrack, stringProperty()},
		{ArgBegin, tickProperty()},
		{ArgEnd, tickProperty()},
		{ArgLane, integerProperty(0, MaxSongLength)},
		{ArgSrcpos, tickProperty()},
		{QStringLiteral("composite"), objectProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		Track* track = resolveTrack(args.value(ArgTrack).toString(), &error);
		if (track == nullptr) { return error; }
		const int begin = args.value(ArgBegin).toInt(-1);
		const int end = args.value(ArgEnd).toInt(-1);
		QString reason;
		if (!checkRange(begin, end, &reason))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, reason);
		}
		const int lane = args.value(ArgLane).toInt(-1);
		const int srcpos = args.value(ArgSrcpos).toInt(0);
		TakeLaneModel& model = track->takeLanes();
		if (!model.hasLane(lane))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("%1 has no take lane %2 (its lanes: %3); add one with "
					"comp.lane_add").arg(trackIdOf(track)).arg(lane).arg(laneIndexList(model)));
		}
		if (srcpos < 0)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'%1' must be >= 0, got %2").arg(ArgSrcpos).arg(srcpos));
		}

		const QString id = trackIdOf(track);
		if (!model.canSelect(begin, end, lane, srcpos))
		{
			// The model's own rule, asked BEFORE the checkpoint: a checkpoint
			// taken for a write that never happens would leave an undo step
			// behind and shadow the undo of the real edit under it.
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the take-lane model refuses [%1, %2) on lane %3 (srcpos %4)")
					.arg(begin).arg(end).arg(lane).arg(srcpos));
		}
		const QJsonObject before = compositeState(track);
		track->addJournalCheckPoint();
		model.selectSegment(begin, end, lane, srcpos);

		QJsonObject result;
		result.insert(ArgTrack, id);
		insertRange(&result, begin, end, lane, srcpos);
		result.insert(QStringLiteral("composite"), compositeState(track));
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(QJsonObject{{ArgTrack, id}, {QStringLiteral("composite"), before}},
				ManualCompositeInverse,
				QJsonObject{{ArgTrack, id}, {ArgBegin, begin}, {ArgEnd, end},
					{ArgLane, lane}, {ArgSrcpos, srcpos}},
				true, ClauseTrackJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}


void registerCompRebuild(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("comp.rebuild");
	cmd.group = QStringLiteral("comp");
	cmd.verb = QStringLiteral("rebuild");
	cmd.description = QStringLiteral("Rebuild the composite: sort it, merge neighbouring "
		"segments that are the same lane at a continuous offset, and - when a span is given - "
		"clamp the composite to exactly [begin, end) and fill every gap in it with the "
		"track's base lane. With no span it normalises what is already there (idempotent on a "
		"well-formed composite). The take audio is not touched. Reversible through the "
		"ProjectJournal (Track checkpoint).");
	cmd.argsSchema = objectSchema({
		{ArgTrack, stringProperty()},
		{ArgBegin, tickProperty()},
		{ArgEnd, tickProperty()},
	}, {ArgTrack});
	cmd.resultSchema = objectSchema({
		{ArgTrack, stringProperty()},
		{QStringLiteral("segments_before"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("segments_after"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("composite"), objectProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		Track* track = resolveTrack(args.value(ArgTrack).toString(), &error);
		if (track == nullptr) { return error; }

		// A span is optional, but HALF a span is not a request: both ends or
		// neither, refused typed rather than guessed.
		const bool hasBegin = args.contains(ArgBegin);
		const bool hasEnd = args.contains(ArgEnd);
		const int begin = args.value(ArgBegin).toInt(-1);
		const int end = args.value(ArgEnd).toInt(-1);
		if (hasBegin != hasEnd)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("a span needs both '%1' and '%2'; got only one").arg(ArgBegin, ArgEnd));
		}
		QString reason;
		if (hasBegin && !checkRange(begin, end, &reason))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, reason);
		}

		const QString id = trackIdOf(track);
		TakeLaneModel& model = track->takeLanes();
		const QJsonObject before = compositeState(track);
		const int beforeCount = before.value(QStringLiteral("segments")).toArray().size();

		track->addJournalCheckPoint();
		const int after = model.rebuild(hasBegin ? begin : -1, hasBegin ? end : -1);

		QJsonObject result;
		result.insert(ArgTrack, id);
		result.insert(QStringLiteral("segments_before"), beforeCount);
		result.insert(QStringLiteral("segments_after"), after);
		result.insert(QStringLiteral("composite"), compositeState(track));
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(QJsonObject{{ArgTrack, id}, {QStringLiteral("composite"), before}},
				ManualCompositeInverse,
				QJsonObject{{ArgTrack, id}}, true, ClauseTrackJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}


void registerCompGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("comp.get_state");
	cmd.group = QStringLiteral("comp");
	cmd.verb = QStringLiteral("get_state");
	cmd.description = QStringLiteral("One track's comping state: its take lanes (with the ids "
		"of their takes), the composite's ordered segments, and what each segment resolves to "
		"- the take clip and the source frame its first tick reads. A segment whose lane has "
		"no take covering it is reported as 'unresolved', never guessed at. Reads only.");
	cmd.argsSchema = objectSchema({
		{ArgTrack, stringProperty()},
	}, {ArgTrack});
	cmd.resultSchema = objectSchema({
		{ArgTrack, stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("lanes"), arrayProperty()},
		{QStringLiteral("lane_count"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("take_count"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("composite"), objectProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		Track* track = resolveTrack(args.value(ArgTrack).toString(), &error);
		if (track == nullptr) { return error; }
		const QJsonArray lanes = lanesState(track);
		int takes = 0;
		for (const QJsonValue& value : lanes)
		{
			takes += value.toObject().value(QStringLiteral("take_count")).toInt();
		}
		QJsonObject result;
		result.insert(ArgTrack, trackIdOf(track));
		result.insert(QStringLiteral("name"), track->name());
		result.insert(QStringLiteral("lanes"), lanes);
		result.insert(QStringLiteral("lane_count"), lanes.size());
		result.insert(QStringLiteral("take_count"), takes);
		result.insert(QStringLiteral("composite"), compositeState(track));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace


void registerCompEditCommands(ControlRegistry& registry)
{
	registerCompSelect(registry);
	registerCompRebuild(registry);
	registerCompGetState(registry);
}

} // namespace lmms
