/*
 * ControlCommandsRecordingPass.cpp - transport.record_options and comp.cycle: how a pass starts, cycling takes
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
/*! 0.5.0 L03: the recording pass on the spine.
 *  - transport.record_options (WF-080 / WF-082): count-in and pre-roll bars and the record mode
 *    (RecordOptions.h) - a preference in the config file, one undo step;
 *  - comp.cycle (ED-168): the comp segment at a position plays the next (or previous) take lane -
 *    what cycling takes from the keyboard runs - one undo step (a Track checkpoint, as comp.select). */

#include <QJsonArray>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "RecordOptions.h"
#include "Song.h"
#include "TakeLane.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

ControlResult recordOptions(const QJsonObject& args)
{
	const recordoptions::Options before = recordoptions::options();
	recordoptions::Options after = before;
	after.countInBars = args.value(QStringLiteral("count_in_bars")).toInt(after.countInBars);
	after.preRollBars = args.value(QStringLiteral("pre_roll_bars")).toInt(after.preRollBars);
	if (args.contains(QStringLiteral("mode")))
	{
		after.mode = static_cast<recordoptions::Mode>(recordoptions::modeNames().indexOf(args.value(QStringLiteral("mode")).toString()));
	}
	const bool changed = after.countInBars != before.countInBars || after.preRollBars != before.preRollBars || after.mode != before.mode;
	if (changed)  // only a real change is an undo step (review #10)
	{
		recordoptions::setOptions(after);
		addUndoStep([before]() { recordoptions::setOptions(before); }, [after]() { recordoptions::setOptions(after); });
	}
	return ControlResult::success(recordoptions::toJson(recordoptions::options()));
}

//! The segment of @a model that holds @a tick, or false.
bool segmentAt(const TakeLaneModel& model, int tick, TakeLaneSegment* out)
{
	for (const TakeLaneSegment& segment : model.segments())
	{
		if (tick >= segment.beginTick && tick < segment.endTick) { *out = segment; return true; }
	}
	return false;
}

//! The lane after (or before) @a lane in the model's lane list, wrapping round.
int neighbourLane(const TakeLaneModel& model, int lane, int step)
{
	const std::vector<TakeLane>& lanes = model.lanes();
	int at = 0;
	for (std::size_t i = 0; i < lanes.size(); ++i)
	{
		if (lanes[i].index == lane) { at = static_cast<int>(i); }
	}
	const int count = static_cast<int>(lanes.size());
	return lanes[static_cast<std::size_t>(((at + step) % count + count) % count)].index;
}

ControlResult compCycle(const QJsonObject& args)
{
	ControlResult error;
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	TakeLaneModel& model = track->takeLanes();
	const int tick = args.value(QStringLiteral("tick")).toInt(static_cast<int>(Engine::getSong()->getPlayPos(Song::PlayMode::Song).getTicks()));
	TakeLaneSegment segment;
	if (model.laneCount() < 2 || !segmentAt(model, tick, &segment))
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1 has no comp segment at tick %2 with another lane to cycle to").arg(trackIdOf(track)).arg(tick));
	}
	const int step = args.value(QStringLiteral("direction")).toString() == QLatin1String("previous") ? -1 : 1;
	const int lane = neighbourLane(model, segment.laneIndex, step);
	if (!model.canSelect(segment.beginTick, segment.endTick, lane, segment.sourceOffset))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the take-lane model refuses lane %1 there").arg(lane));
	}
	track->addJournalCheckPoint();
	model.selectSegment(segment.beginTick, segment.endTick, lane, segment.sourceOffset);
	QJsonObject result{{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("begin"), segment.beginTick},
		{QStringLiteral("end"), segment.endTick}, {QStringLiteral("lane"), lane}, {QStringLiteral("previous_lane"), segment.laneIndex}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("lane"), segment.laneIndex}}, QStringLiteral("control.undo"), {},
		true, QStringLiteral("ProjectJournal (Track checkpoint): the track carries its take lanes, so the checkpoint puts the "
			"previous lane back on the segment")));
	return ControlResult::success(result);
}

} // namespace

void registerRecordingPassCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("transport.record_options");
	cmd.group = QStringLiteral("transport");
	cmd.verb = QStringLiteral("record_options");
	cmd.description = QStringLiteral("How a recording pass starts and what it keeps: 'count_in_bars' (0..4 - the metronome clicks "
		"them before the play position, on or not), 'pre_roll_bars' (0..8 - played without the click), so the transport starts "
		"that many bars early and audio is captured only from the play position; 'mode' overdub (a MIDI pass keeps the notes "
		"it plays over) or replace (the notes a recorded note overlaps are removed). A field left out keeps its value; none "
		"reads them. A preference in the config file, not the project; a change is one undo step.");
	cmd.argsSchema = objectSchema({{QStringLiteral("count_in_bars"), integerProperty(0, 4)},
		{QStringLiteral("pre_roll_bars"), integerProperty(0, 8)}, {QStringLiteral("mode"), enumProperty(recordoptions::modeNames())}});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.requiresEngine = false;
	cmd.handler = recordOptions;
	registry.registerCommand(cmd);

	cmd.id = QStringLiteral("comp.cycle");
	cmd.group = QStringLiteral("comp");
	cmd.verb = QStringLiteral("cycle");
	cmd.description = QStringLiteral("Cycle the take a comp segment plays: the segment at 'tick' (the play position when left out) "
		"on 'track' switches to the next take lane ('direction' next, the default) or the previous one, wrapping round. One "
		"undo step.");
	cmd.argsSchema = objectSchema({{QStringLiteral("track"), stringProperty()}, {QStringLiteral("tick"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("direction"), enumProperty({QStringLiteral("next"), QStringLiteral("previous")})}}, {QStringLiteral("track")});
	cmd.requiresEngine = true;
	cmd.handler = compCycle;
	registry.registerCommand(cmd);
}

} // namespace lmms
