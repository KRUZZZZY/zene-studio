/*
 * ControlCommandsCompGroup.cpp - comp edges moved, and one comp choice over several tracks
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
/*! 0.5.0 L03 (W3), comping beyond one range on one track:
 *  - comp.move_edge (ED-169): the boundary between two takes nearest a tick moves to another tick -
 *    the take on the side that grows keeps its alignment, so the music stays in time;
 *  - comp.select_group (ED-172): one comp choice over several tracks at once - a drum kit's tracks,
 *    named directly or by a VCA group's edit set - as one undo step;
 *  - comp.split_passes (ED-167): a take recorded across loop wraps cut into one piece a pass, each on its
 *    own take lane at the loop's start, the last pass comped in - loop recording into takes. */

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <QJsonArray>

#include "ArrangementEdits.h"
#include "Clip.h"
#include "Timeline.h"
#include "ControlCommandDef.h"
#include "ControlCompSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "Song.h"
#include "TakeLane.h"
#include "Track.h"
#include "VcaGroup.h"

namespace lmms
{

using namespace control;

namespace
{

//! The index of the segment whose END is the boundary (with the next one) nearest @a tick, or -1.
int nearestEdge(const std::vector<TakeLaneSegment>& segments, int tick)
{
	int best = -1;
	for (std::size_t i = 0; i + 1 < segments.size(); ++i)
	{
		if (segments[i].endTick != segments[i + 1].beginTick) { continue; }
		if (best < 0 || std::abs(segments[i].endTick - tick) < std::abs(segments[static_cast<std::size_t>(best)].endTick - tick))
		{
			best = static_cast<int>(i);
		}
	}
	return best;
}

//! The range, lane and slip that move the boundary between @a left and @a right to @a to: the growing side's take
//! carries on in time - the same lane at the same slip (include/TakeLane.h, 0.5.0 E2), so the merge joins it.
TakeLaneSegment edgeMove(const TakeLaneSegment& left, const TakeLaneSegment& right, int to)
{
	const bool growLeft = to > left.endTick;
	const TakeLaneSegment& growing = growLeft ? left : right;
	TakeLaneSegment move;
	move.beginTick = growLeft ? left.endTick : to;
	move.endTick = growLeft ? to : left.endTick;
	move.laneIndex = growing.laneIndex;
	move.sourceOffset = growing.sourceOffset;
	return move;
}

//! The timeline half-open span over which ONE take of @a move's lane plays: its own range, cut
//! short by the slip (at timeline tick t the handle reads the take at t + sourceOffset, so the
//! last slip's worth of ticks is where its slipped read runs off the end).
struct TakeSpan
{
	int begin = 0;
	int end = 0;
};

//! Every take of @a move's lane as a span it covers on the timeline.
std::vector<TakeSpan> takeSpans(const Track& track, const TakeLaneSegment& move)
{
	std::vector<TakeSpan> spans;
	for (const Clip* clip : track.getClips())
	{
		if (clip == nullptr || clip->laneIndex() != move.laneIndex) { continue; }
		const TakeSpan span{static_cast<int>(clip->startPosition().getTicks()),
			static_cast<int>(clip->endPosition().getTicks()) - move.sourceOffset};
		if (span.end > span.begin) { spans.push_back(span); }
	}
	return spans;
}

/*! Whether the takes of @a move's lane cover EVERY tick of the range it grows into.
 *
 *  One lane may hold SEVERAL takes, so the two ends of the range can be supplied by different
 *  clips with a gap between them: asking only the two ends let a boundary move across that gap
 *  and left the composite naming a lane with no take there - which TakeLaneModel::takeAt() then
 *  reports as 'unresolved', and the engine plays nothing for. Every tick is walked instead. */
bool takeReaches(const Track& track, const TakeLaneSegment& move)
{
	std::vector<TakeSpan> spans = takeSpans(track, move);
	std::sort(spans.begin(), spans.end(), [](const TakeSpan& a, const TakeSpan& b) { return a.begin < b.begin; });
	int reached = move.beginTick;
	for (const TakeSpan& span : spans)
	{
		if (span.begin > reached) { break; }  // a gap no later span can bridge
		reached = std::max(reached, span.end);
		if (reached >= move.endTick) { return true; }
	}
	return false;
}

//! The inverse a composite edit's record names (SPEC A16 §3): the Track checkpoint, which
//! control.undo replays. No single command re-creates the previous composite - there is no
//! un-select, a composite being total over its span - so the before-state carries it.
const QString CompositeInverseOp = QStringLiteral("control.undo");

const QString CompositeMechanism = QStringLiteral("ProjectJournal (Track checkpoint: "
	"Track::saveTrack writes the <takelanes> element - the lane list AND the composite - and "
	"Track::loadTrack re-reads it)");

//! The same mechanism for the grouped half, which takes one checkpoint PER TRACK: the registry
//! folds them into one step after the handler returns (ControlRegistry::runHandler ->
//! ProjectJournal::mergeCheckpointsFrom), so one control.undo puts every track back.
const QString CompositeGroupMechanism = QStringLiteral("ProjectJournal (a Track checkpoint per "
	"track, folded by the registry into ONE undo step, so one control.undo restores every track "
	"of the group)");

ControlResult moveEdge(const QJsonObject& args)
{
	ControlResult error;
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	TakeLaneModel& model = track->takeLanes();
	const int edge = nearestEdge(model.segments(), args.value(QStringLiteral("tick")).toInt());
	if (edge < 0) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the composite has no boundary between two takes")); }
	const TakeLaneSegment left = model.segments()[static_cast<std::size_t>(edge)];
	const TakeLaneSegment right = model.segments()[static_cast<std::size_t>(edge) + 1];
	const int to = args.value(QStringLiteral("to")).toInt();
	if (to <= left.beginTick || to >= right.endTick)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'to' is inside the two takes, %1..%2").arg(left.beginTick + 1).arg(right.endTick - 1));
	}
	const QString id = trackIdOf(track);
	QJsonObject result{{QStringLiteral("track"), id}, {QStringLiteral("edge"), to}};
	if (to == left.endTick) { return ControlResult::success(result); }  // nothing moved: no record
	const TakeLaneSegment move = edgeMove(left, right, to);
	if (!takeReaches(*track, move))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the take on that side does not reach %1").arg(to));
	}
	if (!model.canSelect(move.beginTick, move.endTick, move.laneIndex, move.sourceOffset))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the take-lane model refuses this edge move (each lane supports at most %1 spans)")
				.arg(TakeLaneModel::MaxCompositeSpansPerLane));
	}
	const QJsonObject before{{QStringLiteral("track"), id}, {QStringLiteral("composite"), compositeState(track)}};
	track->addJournalCheckPoint();
	model.selectSegment(move.beginTick, move.endTick, move.laneIndex, move.sourceOffset);
	Engine::getSong()->setModified();
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(before, CompositeInverseOp, QJsonObject{{QStringLiteral("track"), id}},
			true, CompositeMechanism));
	return ControlResult::success(result);
}

//! The tracks the arguments name: 'tracks' ids, or the edit set of VCA group 'group'.
bool groupTracks(const QJsonObject& args, std::vector<Track*>* out, ControlResult* error)
{
	QJsonArray ids = args.value(QStringLiteral("tracks")).toArray();
	if (args.contains(QStringLiteral("group")))
	{
		const VcaGroup* group = Engine::mixer()->vcaGroup(args.value(QStringLiteral("group")).toInt(-1));
		if (group == nullptr) { *error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no such group")); return false; }
		for (const int id : group->editTracks()) { ids.append(trackId(id)); }
	}
	for (const QJsonValue& id : ids)
	{
		Track* track = resolveTrack(id.toString(), error);
		if (track == nullptr) { return false; }
		out->push_back(track);
	}
	if (out->empty()) { *error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("name the 'tracks' or a 'group'")); }
	return !out->empty();
}

ControlResult selectGroup(const QJsonObject& args)
{
	std::vector<Track*> tracks;
	ControlResult error;
	if (!groupTracks(args, &tracks, &error)) { return error; }
	const int begin = args.value(QStringLiteral("begin")).toInt(-1);
	const int end = args.value(QStringLiteral("end")).toInt(-1);
	const int lane = args.value(QStringLiteral("lane")).toInt(-1);
	const int srcpos = args.value(QStringLiteral("srcpos")).toInt(0);
	// Every track is asked first: one that cannot take the choice refuses the whole edit.
	for (Track* track : tracks)
	{
		if (!slipAccepted(track, srcpos, &error)) { return error; }
		if (!track->takeLanes().hasLane(lane) || !track->takeLanes().canSelect(begin, end, lane, srcpos))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("%1 cannot take lane %2 over [%3, %4); a lane supports at most %5 spans")
					.arg(trackIdOf(track)).arg(lane).arg(begin).arg(end)
					.arg(TakeLaneModel::MaxCompositeSpansPerLane));
		}
	}
	QJsonArray done;
	QJsonArray before;
	for (Track* track : tracks)
	{
		QJsonObject entry{{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("composite"), compositeState(track)}};
		track->addJournalCheckPoint();
		track->takeLanes().selectSegment(begin, end, lane, srcpos);
		done.append(trackIdOf(track));
		before.append(entry);
	}
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("tracks"), done}, {QStringLiteral("lane"), lane}, {QStringLiteral("begin"), begin},
		{QStringLiteral("end"), end}};
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(QJsonObject{{QStringLiteral("tracks"), before}}, CompositeInverseOp,
			QJsonObject{{QStringLiteral("tracks"), done}}, true, CompositeGroupMechanism));
	return ControlResult::success(result);
}

ControlResult splitPasses(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	// The take validator, not the general resolver: only an audio or MIDI take can be comped
	// (comp.assign's own rule, docs/COMPING.md), so cutting an automation or pattern clip into
	// passes and tagging the pieces with take lanes would produce lanes nothing can resolve to.
	if (!resolveTakeClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	const Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
	const tick_t begin = timeline.loopBegin().getTicks();
	const tick_t loop = timeline.loopEnd().getTicks() - begin;
	const tick_t start = ref.clip->startPosition().getTicks();
	const tick_t length = ref.clip->length().getTicks();
	if (loop <= 0 || start != begin || length <= loop)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the clip starts at the loop's start and runs past its end, or there is nothing to split"));
	}
	const tick_t passes = (length + loop - 1) / loop;
	if (passes > 32)
	{
		// Capping the cuts would leave every pass past the 32nd merged into the last take.
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the clip spans %1 passes; at most 32 can be split").arg(passes));
	}
	const QString id = trackIdOf(ref.track);
	// A16 §3: the Track checkpoint below is what control.undo replays, so the record says so and
	// carries the composite as it was - like its siblings comp.move_edge and comp.select_group.
	QJsonObject result{{QStringLiteral("track"), id}};
	QJsonObject before{{QStringLiteral("track"), id}, {QStringLiteral("composite"), compositeState(ref.track)}};
	ref.track->addJournalCheckPoint();
	std::vector<Clip*> pieces{ref.clip};
	for (int k = passes - 1; k >= 1; --k)
	{
		if (Clip* piece = arrangement::splitClipAt(ref.clip, begin + k * loop)) { pieces.insert(pieces.begin() + 1, piece); }
	}
	TakeLaneModel& model = ref.track->takeLanes();
	int lane = 0;
	for (std::size_t k = 0; k < pieces.size(); ++k)
	{
		lane = model.addLane(QStringLiteral("Pass %1").arg(k + 1));
		assignTakeLane(pieces[k], lane);  // the lane tag's one writer (ControlCommandsComp.cpp)
		pieces[k]->movePosition(TimePos(begin));
	}
	// The last piece can be shorter than the loop; selecting the last lane over the whole loop would
	// name it where no take covers, which TakeLaneModel::takeAt() reports 'unresolved'. So only the
	// last piece's own length is composited.
	const tick_t covered = std::min<tick_t>(loop, pieces.back()->length().getTicks());
	model.selectSegment(static_cast<int>(begin), static_cast<int>(begin + covered), lane, 0);
	Engine::getSong()->setModified();
	result.insert(QStringLiteral("passes"), static_cast<int>(pieces.size()));
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(before, CompositeInverseOp, QJsonObject{{QStringLiteral("track"), id}},
			true, CompositeMechanism));
	return ControlResult::success(result);
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, const QStringList& required,
	ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QLatin1String(id), description, objectSchema(args, QJsonArray::fromStringList(required)), true, handler);
}


void registerTakeSelect(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("comp.take_select");
	cmd.group = QStringLiteral("comp");
	cmd.verb = QStringLiteral("take_select");
	cmd.description = QStringLiteral("Choose which pass of a recorded clip group plays. Name any take clip in the group and its lane; the choice stays active through later loop passes until chosen again. Reversible through the ProjectJournal (Track checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("lane"), integerProperty(0, MaxSongLength)},
	}, {QStringLiteral("clip"), QStringLiteral("lane")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("lane"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("active_lane"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("user_chosen"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ClipRef ref;
		const QString clipId = args.value(QStringLiteral("clip")).toString();
		if (!resolveTakeClip(clipId, &ref, &error)) { return error; }
		const int groupId = ref.clip->takeGroupId();
		const int lane = args.value(QStringLiteral("lane")).toInt(-1);
		TakeLaneModel& model = ref.track->takeLanes();
		if (!ref.clip->hasTakeGroup() || groupId < 0 || !model.canSelectTake(groupId, lane, ref.track->getClips()))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("%1 is not in a recorded take group containing lane %2")
					.arg(clipId).arg(lane));
		}
		const int previousLane = model.activeTakeLane(groupId);
		const bool previousChoice = model.userChoseTake(groupId);
		const bool alreadyChosen = previousChoice && previousLane == lane;
		if (!alreadyChosen)
		{
			ref.track->addJournalCheckPoint();
			model.selectTake(groupId, lane, ref.track->getClips());
			Engine::getSong()->setModified();
		}
		QJsonObject result{{QStringLiteral("clip"), control::takeClipId(ref.clip)},
			{QStringLiteral("track"), trackIdOf(ref.track)}, {QStringLiteral("lane"), lane},
			{QStringLiteral("active_lane"), model.activeTakeLane(groupId)},
			{QStringLiteral("user_chosen"), model.userChoseTake(groupId)}};
		if (!alreadyChosen)
		{
			const QJsonObject before{{QStringLiteral("group"), groupId},
				{QStringLiteral("active_lane"), previousLane}, {QStringLiteral("user_chosen"), previousChoice}};
			result.insert(QStringLiteral("__transaction"), transactionPayload(before,
				QStringLiteral("control.undo"),
				QJsonObject{{QStringLiteral("clip"), clipId}, {QStringLiteral("lane"), previousLane}}, true,
				QStringLiteral("ProjectJournal (Track checkpoint: take-group selection is in Track::saveTrack)")));
		}
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}


void registerPlayAllTakes(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("comp.play_all_takes");
	cmd.group = QStringLiteral("comp");
	cmd.verb = QStringLiteral("play_all_takes");
	cmd.description = QStringLiteral("Set whether every take on this track plays together. This is track-wide and saved with the project. Reversible through the ProjectJournal (Track checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("enabled"), booleanProperty()},
	}, {QStringLiteral("track"), QStringLiteral("enabled")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("previous"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
		if (track == nullptr) { return error; }
		const bool enabled = args.value(QStringLiteral("enabled")).toBool();
		TakeLaneModel& model = track->takeLanes();
		const bool previous = model.playAllTakes();
		if (previous != enabled)
		{
			track->addJournalCheckPoint();
			model.setPlayAllTakes(enabled);
			Engine::getSong()->setModified();
		}
		QJsonObject result{{QStringLiteral("track"), trackIdOf(track)},
			{QStringLiteral("enabled"), enabled}, {QStringLiteral("previous"), previous}};
		if (previous != enabled)
		{
			result.insert(QStringLiteral("__transaction"), transactionPayload(
				QJsonObject{{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("enabled"), previous}},
				QStringLiteral("control.undo"),
				QJsonObject{{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("enabled"), previous}}, true,
				QStringLiteral("ProjectJournal (Track checkpoint: play-all-takes state is in Track::saveTrack)")));
		}
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerCompGroupCommands(ControlRegistry& registry)
{
	add(registry, "comp.move_edge", QStringLiteral("Move the boundary between two takes of the composite nearest 'tick' to 'to': the "
		"take on the growing side carries on in time (the same lane at its own slip). Refused where no take of that lane reaches "
		"over the range it would grow into. "
		"Reversible through the ProjectJournal (Track checkpoint)."),
		{{QStringLiteral("track"), stringProperty()}, {QStringLiteral("tick"), tickProperty()}, {QStringLiteral("to"), tickProperty()}},
		{QStringLiteral("track"), QStringLiteral("tick"), QStringLiteral("to")}, moveEdge);
	add(registry, "comp.split_passes", QStringLiteral("Turn a take recorded across loop wraps into takes: the clip (starting at the "
		"loop's start, longer than the loop) is cut at each wrap, every pass goes on its own take lane at the loop's start, and "
		"the last pass is comped in (refused past 32 passes). Reversible through the ProjectJournal (Track checkpoint)."),
		{{QStringLiteral("clip"), stringProperty()}}, {QStringLiteral("clip")}, splitPasses);
	add(registry, "comp.select_group", QStringLiteral("comp.select on several tracks at once - 'tracks', or the edit set of VCA "
		"group 'group' (a kit recorded across tracks): lane 'lane' supplies [begin, end) on every one, slipped 'srcpos'. "
		"Refused, with nothing changed, when any track cannot take it. One undo step for all of them."),
		{{QStringLiteral("tracks"), arrayProperty()}, {QStringLiteral("group"), integerProperty()}, {QStringLiteral("begin"), tickProperty()},
			{QStringLiteral("end"), tickProperty()}, {QStringLiteral("lane"), integerProperty(0, MaxSongLength)}, {QStringLiteral("srcpos"), tickProperty()}},
		{QStringLiteral("begin"), QStringLiteral("end"), QStringLiteral("lane")}, selectGroup);
	registerTakeSelect(registry);
	registerPlayAllTakes(registry);
}

} // namespace lmms
