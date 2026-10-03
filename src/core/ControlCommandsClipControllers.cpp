/*
 * ControlCommandsClipControllers.cpp - clip.controllers_get / set: a MIDI clip's CC, pitch-bend and aftertouch lanes
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

/*! ED-130 (0.5.0 L07): the controller lanes a MIDI clip plays to its instrument - CC n, pitch bend,
 *  channel pressure - each a breakpoint curve (MidiClipControllers.h). A replace is one MidiClip
 *  journal checkpoint (the lanes are saved inside the clip's element). */

#include <QJsonArray>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "MidiClip.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxPoints = 4096;

QJsonArray pointsJson(const std::vector<ControllerPoint>& points)
{
	QJsonArray out;
	for (const ControllerPoint& point : points)
	{
		out.append(QJsonObject{{QStringLiteral("tick"), point.tick}, {QStringLiteral("value"), point.value}});
	}
	return out;
}

QJsonObject laneJson(const ControllerLane& lane)
{
	static const char* const kinds[] = {"cc", "pitchbend", "aftertouch", "program"};
	return {{QStringLiteral("lane"), lane.id()}, {QStringLiteral("kind"), QLatin1String(kinds[static_cast<int>(lane.kind)])},
		{QStringLiteral("controller"), lane.controller}, {QStringLiteral("maximum"), lane.maximum()},
		{QStringLiteral("points"), pointsJson(lane.points)}};
}

//! The `points` argument checked against the lane's range; false (with @a error) on any bad point.
bool readPoints(const QJsonArray& json, int maximum, std::vector<ControllerPoint>* out, ControlResult* error)
{
	if (json.size() > MaxPoints)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("at most %1 points").arg(MaxPoints));
		return false;
	}
	for (const QJsonValue& value : json)
	{
		const QJsonObject point = value.toObject();
		const int tick = point.value(QStringLiteral("tick")).toInt(-1);
		const int v = point.value(QStringLiteral("value")).toInt(-1);
		if (tick < 0 || v < 0 || v > maximum)
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("a point needs tick >= 0 and 0 <= value <= %1").arg(maximum));
			return false;
		}
		out->push_back({tick, v});
	}
	return true;
}

ControlResult getControllers(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	QJsonArray lanes;
	for (const ControllerLane& lane : clip->controllers().lanes()) { lanes.append(laneJson(lane)); }
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("lanes"), lanes}});
}

ControlResult setControllers(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	const QString id = args.value(QStringLiteral("lane")).toString();
	ControllerLane probe;
	if (!ControllerLane::fromId(id, &probe.kind, &probe.controller))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'lane' is cc:<0..127>, pitchbend, aftertouch or program, got '%1'").arg(id));
	}
	std::vector<ControllerPoint> points;
	if (!readPoints(args.value(QStringLiteral("points")).toArray(), probe.maximum(), &points, &error)) { return error; }
	const ControllerLane* existing = clip->controllers().lane(probe.kind, probe.controller);
	const QJsonArray before = existing != nullptr ? pointsJson(existing->points) : QJsonArray();
	clip->addJournalCheckPoint();
	clip->setControllerPoints(probe.kind, probe.controller, std::move(points));
	const ControllerLane* now = clip->controllers().lane(probe.kind, probe.controller);
	QJsonObject result{{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("lane"), id},
		{QStringLiteral("points"), now != nullptr ? pointsJson(now->points) : QJsonArray()}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("points"), before}},
		QStringLiteral("clip.controllers_set"), {{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("lane"), id},
			{QStringLiteral("points"), before}}, true, QStringLiteral("MidiClip journal checkpoint: undo restores the clip's lanes")));
	return ControlResult::success(result);
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd;
	cmd.group = QStringLiteral("clip");
	cmd.verb = QLatin1String(verb);
	cmd.id = cmd.group + QLatin1Char('.') + cmd.verb;
	cmd.description = description;
	cmd.argsSchema = args;
	cmd.resultSchema = objectProperty();
	cmd.mutating = mutating;
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void registerClipControllerCommands(ControlRegistry& registry)
{
	add(registry, "controllers_get", QStringLiteral("A MIDI clip's controller lanes - the CC, pitch-bend and aftertouch "
		"curves it plays to its instrument: [{lane (cc:<n> | pitchbend | aftertouch), kind, controller, maximum (127, "
		"16383 for pitch bend), points [{tick, value}]}]."), objectSchema({{QStringLiteral("clip"), stringProperty()}},
		{QStringLiteral("clip")}), false, getControllers);
	add(registry, "controllers_set", QStringLiteral("Replace one controller lane of a MIDI clip: 'lane' is cc:<0..127>, "
		"pitchbend (0..16383, 8192 centre), aftertouch or program (a program change, held from each point to the next); "
		"'points' [{tick (clip time), value}] - linear between points, held before the first and after the last; no points removes the lane. Played to the instrument "
		"(and the track's MIDI out) as the clip plays. One undo step (MidiClip checkpoint)."),
		objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("lane"), stringProperty()},
			{QStringLiteral("points"), arrayProperty()}}, {QStringLiteral("clip"), QStringLiteral("lane"), QStringLiteral("points")}),
		true, setControllers);
}

} // namespace lmms
