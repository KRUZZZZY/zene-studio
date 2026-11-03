/*
 * ControlCommandsControllerConvert.cpp - clip.controllers_to_automation / controllers_from_automation
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

/*! Q050-291.1 (R-editing-93): a MIDI clip's CC lane and a device parameter's automation curve of the clip's
 *  OWNING track, converted in place in either direction - the same breakpoints, on the other model.
 *
 *  - Value: CC 0..127 <-> the parameter's own unit, linearly across its min..max (the unit automation.get_state
 *    reports as "value"). Going back rounds to the nearest CC step and clamps.
 *  - Time: a lane tick t is the song tick `clip.start + clip.startTimeOffset + t` (InstrumentTrack::play's own
 *    mapping); an automation node at clip-local tick n is the song tick `automationClip.start + n`.
 *  - Both directions COPY: the source stays. to_automation replaces the target clip's nodes between the first and
 *    last converted tick; from_automation replaces the whole lane.
 *  - Every refusal is decided before the first write, so a refused call leaves both models untouched. One call is one
 *    undo step: the registry merges the MidiClip / AutomationClip checkpoints (or the created track's action step). */

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include <QJsonArray>

#include "AutomationClip.h"
#include "ComboBoxModel.h"
#include "ControlAutomationSupport.h"
#include "ControlCommandDef.h"
#include "ControlDeviceSupport.h"
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
constexpr int MaxCc = 127;
constexpr int MaxListedCandidates = 8;

ControlResult refuse(ControlErrorKind kind, const QString& message) { return ControlResult::failure(kind, message); }

QJsonArray pointsJson(const std::vector<ControllerPoint>& points)
{
	QJsonArray out;
	for (const ControllerPoint& point : points)
	{
		out.append(QJsonObject{{QStringLiteral("tick"), point.tick}, {QStringLiteral("value"), point.value}});
	}
	return out;
}

//! The names automation.get_state reports a clip's progression by.
QString progressionLabel(AutomationClip::ProgressionType type)
{
	switch (type)
	{
		case AutomationClip::ProgressionType::Discrete: return QStringLiteral("discrete");
		case AutomationClip::ProgressionType::Linear: return QStringLiteral("linear");
		case AutomationClip::ProgressionType::CubicHermite: return QStringLiteral("cubic_hermite");
	}
	return QStringLiteral("unknown");
}

//! The lane named by args.lane: a CC lane only (pitch bend, aftertouch and program change have no mapping here).
bool ccLaneId(const QJsonObject& args, int* controller, QString* id, ControlResult* error)
{
	*id = args.value(QStringLiteral("lane")).toString();
	ControllerKind kind = ControllerKind::ControlChange;
	if (!ControllerLane::fromId(*id, &kind, controller))
	{
		*error = refuse(ControlErrorKind::InvalidArgs, QStringLiteral("'lane' is cc:<0..127>, got '%1'").arg(*id));
		return false;
	}
	if (kind != ControllerKind::ControlChange)
	{
		*error = refuse(ControlErrorKind::Refused, QStringLiteral("only a cc:<n> lane converts to a parameter; '%1' has no mapping").arg(*id));
		return false;
	}
	return true;
}

//! Whether @a model's range can carry a CC lane: a continuous or integer range, not a switch or a list of choices.
bool mappable(const AutomationParameter& parameter, ControlResult* error)
{
	const AutomatableModel* model = parameter.model;
	const double low = model->minValue<float>();
	const double high = model->maxValue<float>();
	if (dynamic_cast<const BoolModel*>(model) != nullptr || dynamic_cast<const ComboBoxModel*>(model) != nullptr
		|| !std::isfinite(low) || !std::isfinite(high) || !(high > low))
	{
		*error = refuse(ControlErrorKind::Refused, QStringLiteral("%1 is a switch, a list of choices or has no range: a CC lane cannot map onto it").arg(parameter.id()));
		return false;
	}
	return true;
}

//! The candidates for an unnamed parameter: all of @a target's, or - @a automatedOnly - the ones that carry automation points.
QList<AutomationParameter> candidatesOf(const ControlTarget& target, bool automatedOnly)
{
	QList<AutomationParameter> out;
	for (const AutomationParameter& each : automationParameters(target))
	{
		const AutomationClip* clip = automatedOnly ? existingAutomationClip(each.model) : nullptr;
		if (!automatedOnly || (clip != nullptr && clip->hasAutomation())) { out.append(each); }
	}
	return out;
}

ControlResult ambiguousTarget(const ControlTarget& target, const QList<AutomationParameter>& candidates)
{
	QStringList names;
	for (int i = 0; i < std::min<int>(candidates.size(), MaxListedCandidates); ++i) { names << candidates.at(i).id(); }
	const QString count = candidates.isEmpty() ? QStringLiteral("no candidate") : QStringLiteral("%1 candidates").arg(candidates.size());
	const QString more = candidates.size() > MaxListedCandidates ? QStringLiteral(", ...") : QString();
	return refuse(ControlErrorKind::InvalidArgs, QStringLiteral("'parameter' is required: %1 on %2 (%3%4); automation.get_state names them")
		.arg(count, target.id, names.join(QStringLiteral(", ")), more));
}

//! The parameter of @a owner to convert: args.parameter, or - left out - the only candidate. @a automatedOnly narrows
//! the candidates to parameters that already carry automation points (the from_automation direction).
bool ownerParameter(const QJsonObject& args, Track* owner, bool automatedOnly, AutomationParameter* out, ControlResult* error)
{
	ControlTarget target;
	if (!resolveControlTarget(trackIdOf(owner), &target, error)) { return false; }
	const QString wanted = args.value(QStringLiteral("parameter")).toString();
	if (!wanted.isEmpty()) { return findAutomationParameter(target, wanted, out, error); }
	const QList<AutomationParameter> candidates = candidatesOf(target, automatedOnly);
	if (candidates.size() != 1)
	{
		*error = ambiguousTarget(target, candidates);
		return false;
	}
	*out = candidates.first();
	return true;
}

float valueOfCc(const AutomatableModel* model, int cc)
{
	const float low = model->minValue<float>();
	return low + (model->maxValue<float>() - low) * static_cast<float>(cc) / MaxCc;
}

int ccOfValue(const AutomatableModel* model, float value)
{
	const double low = model->minValue<float>();
	const double fraction = (static_cast<double>(value) - low) / (static_cast<double>(model->maxValue<float>()) - low);
	return std::clamp(static_cast<int>(std::lround(fraction * MaxCc)), 0, MaxCc);
}

//! Song tick of lane tick @a tick of @a clip.
qint64 songTickOfLane(const MidiClip* clip, qint64 tick)
{
	return static_cast<qint64>(clip->startPosition().getTicks()) + clip->startTimeOffset().getTicks() + tick;
}

//! One converted breakpoint of to_automation, before it is written: the automation-clip tick and the model's value.
struct Node
{
	int tick;
	float value;
};

//! What a conversion call addresses: the clip, its CC lane id and the owning track's parameter.
struct Conversion
{
	ClipRef ref;
	MidiClip* clip = nullptr;
	int controller = 0;
	QString laneId;
	AutomationParameter parameter;
};

//! The call's clip, lane and parameter (a refusal in @a error when any of them cannot be used).
bool resolveConversion(const QJsonObject& args, bool automatedOnly, Conversion* out, ControlResult* error)
{
	out->clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &out->ref, error);
	return out->clip != nullptr && ccLaneId(args, &out->controller, &out->laneId, error)
		&& ownerParameter(args, out->ref.track, automatedOnly, &out->parameter, error) && mappable(out->parameter, error);
}

QJsonObject conversionReply(const Conversion& c, int converted)
{
	return {{QStringLiteral("clip"), clipId(c.ref.id)}, {QStringLiteral("lane"), c.laneId}, {QStringLiteral("track"), trackIdOf(c.ref.track)},
		{QStringLiteral("parameter"), c.parameter.id()}, {QStringLiteral("converted"), converted}};
}

//! The lane's points as automation-clip ticks (@a origin: the target clip's start) and model values, or false with @a error.
bool placeNodes(const MidiClip* clip, const ControllerLane& lane, const AutomationParameter& parameter, qint64 origin,
	std::vector<Node>* nodes, ControlResult* error)
{
	for (const ControllerPoint& point : lane.points)
	{
		const qint64 song = songTickOfLane(clip, point.tick);
		const qint64 local = song - origin;
		if (local < 0 || local > std::numeric_limits<int>::max())
		{
			*error = refuse(ControlErrorKind::Refused, QStringLiteral("lane tick %1 is song tick %2, outside the automation clip (which starts at %3)")
				.arg(point.tick).arg(song).arg(origin));
			return false;
		}
		nodes->push_back({static_cast<int>(local), parameter.model->inverseScaledValue(valueOfCc(parameter.model, point.value))});
	}
	return true;
}

/*! A CC lane is linear between its points: the target clip is made linear too, which is only honest while no node outside
 *  the converted span @a first .. @a last would change how it plays. */
bool linearisingKeepsOthers(const AutomationClip& existing, int first, int last, const AutomationParameter& parameter, ControlResult* error)
{
	if (existing.progressionType() == AutomationClip::ProgressionType::Linear) { return true; }
	for (auto it = existing.getTimeMap().constBegin(); it != existing.getTimeMap().constEnd(); ++it)
	{
		if (it.key() < first || it.key() > last)
		{
			*error = refuse(ControlErrorKind::Refused, QStringLiteral("%1's automation clip is not linear and has points outside the converted span: "
				"making it linear would change how they play").arg(parameter.id()));
			return false;
		}
	}
	return true;
}

/*! The nodes @a lane converts to on @a existing (null: a clip to be created at tick 0), or false with @a error when a
 *  point cannot be placed or the target clip would play differently. Reads only. */
bool planNodes(const MidiClip* clip, const ControllerLane& lane, const AutomationParameter& parameter,
	const AutomationClip* existing, std::vector<Node>* nodes, ControlResult* error)
{
	if (existing != nullptr && existing->isRelative())
	{
		*error = refuse(ControlErrorKind::Refused, QStringLiteral("%1's automation clip is a trim (relative) clip: a CC lane's absolute values cannot be written to it").arg(parameter.id()));
		return false;
	}
	if (static_cast<int>(lane.points.size()) > MaxPoints) { *error = refuse(ControlErrorKind::Refused, QStringLiteral("at most %1 points").arg(MaxPoints)); return false; }
	if (!placeNodes(clip, lane, parameter, existing != nullptr ? existing->startPosition().getTicks() : 0, nodes, error)) { return false; }
	return existing == nullptr || linearisingKeepsOthers(*existing, nodes->front().tick, nodes->back().tick, parameter, error);
}

//! Replaces the nodes between the first and last of @a nodes (all of a created clip) with them, as one undoable edit.
ControlResult writeNodes(const Conversion& c, const std::vector<Node>& nodes)
{
	// Nothing below refuses: the edit is checkpointed (or its created track has its action step) and then written.
	bool created = false;
	ControlResult error;
	AutomationClip* target = automationClipForEdit(c.parameter.model, &created, &error);
	if (target == nullptr) { return error; }
	const QJsonArray before = created ? QJsonArray() : automationPointsJson(target, c.parameter.model);
	if (created) { target->clear(); }  // the seed node the new clip starts with is not a lane point
	else { target->removeNodes(nodes.front().tick, nodes.back().tick); }
	target->setProgressionType(AutomationClip::ProgressionType::Linear);
	for (const Node& node : nodes) { target->putValue(TimePos(node.tick), node.value, false, true); }
	const int needed = nodes.back().tick + 1;
	if (target->length().getTicks() < needed)
	{
		const int bar = TimePos::ticksPerBar();
		target->changeLength(TimePos((needed + bar - 1) / bar * bar));
	}
	QJsonObject result = conversionReply(c, static_cast<int>(nodes.size()));
	result.insert(QStringLiteral("created_automation_track"), created);
	result.insert(QStringLiteral("automation"), automationJson(target, c.parameter.model));
	result.insert(QStringLiteral("__transaction"), automationTransaction(c.parameter, before, before.size(), created));
	return ControlResult::success(result);
}

ControlResult convertToAutomation(const QJsonObject& args)
{
	Conversion c;
	ControlResult error;
	if (!resolveConversion(args, false, &c, &error)) { return error; }
	const ControllerLane* lane = c.clip->controllers().lane(ControllerKind::ControlChange, c.controller);
	if (lane == nullptr || lane->points.empty())
	{
		return refuse(ControlErrorKind::NotFound, QStringLiteral("%1 has no lane %2 with points").arg(clipId(c.ref.id), c.laneId));
	}
	std::vector<Node> nodes;
	if (!planNodes(c.clip, *lane, c.parameter, existingAutomationClip(c.parameter.model), &nodes, &error)) { return error; }
	return writeNodes(c, nodes);
}

//! The window of lane ticks a clip plays, and where lane tick 0 sits on the song and the source clip's own tick 0.
struct LaneWindow
{
	qint64 shift;   //!< song tick of lane tick 0
	qint64 origin;  //!< song tick of the automation clip's tick 0
	qint64 low;     //!< first lane tick the clip plays (a lane has none below 0)
	qint64 high;    //!< last lane tick the clip plays
};

LaneWindow windowOf(const MidiClip* clip, const AutomationClip* source)
{
	const qint64 offset = clip->startTimeOffset().getTicks();
	return {static_cast<qint64>(clip->startPosition().getTicks()) + offset, source->startPosition().getTicks(),
		std::max<qint64>(0, -offset), -offset + clip->length().getTicks()};
}

//! The source's value at lane tick @a tick, as a lane point.
ControllerPoint sampleAt(const AutomationParameter& parameter, AutomationClip* source, const LaneWindow& w, qint64 tick)
{
	const float raw = source->valueAt(TimePos(static_cast<int>(w.shift + tick - w.origin)));
	return {static_cast<tick_t>(tick), ccOfValue(parameter.model, parameter.model->scaledValue(raw))};
}

//! The source's nodes inside the window as lane points; @a cutBefore / @a cutAfter say whether nodes lie outside it.
void collectPoints(const AutomationParameter& parameter, const AutomationClip* source, const LaneWindow& w,
	std::vector<ControllerPoint>* points, bool* cutBefore, bool* cutAfter)
{
	for (auto it = source->getTimeMap().constBegin(); it != source->getTimeMap().constEnd(); ++it)
	{
		const qint64 tick = w.origin + it.key() - w.shift;
		*cutBefore = *cutBefore || tick < w.low;
		*cutAfter = *cutAfter || tick > w.high;
		if (tick >= w.low && tick <= w.high)
		{
			points->push_back({static_cast<tick_t>(tick), ccOfValue(parameter.model, parameter.model->scaledValue(it.value().getInValue()))});
		}
	}
}

//! The lane points the automation of @a parameter converts to inside @a clip's own span (plus the curve's value at an
//! edge the span cuts through), or false with @a error. Reads only.
bool planLane(const MidiClip* clip, AutomationClip* source, const AutomationParameter& parameter,
	std::vector<ControllerPoint>* points, ControlResult* error)
{
	const LaneWindow w = windowOf(clip, source);
	bool cutBefore = false;
	bool cutAfter = false;
	collectPoints(parameter, source, w, points, &cutBefore, &cutAfter);
	if (cutBefore && (points->empty() || points->front().tick > w.low)) { points->insert(points->begin(), sampleAt(parameter, source, w, w.low)); }
	if (cutAfter && (points->empty() || points->back().tick < w.high)) { points->push_back(sampleAt(parameter, source, w, w.high)); }
	if (points->empty())
	{
		*error = refuse(ControlErrorKind::NotFound, QStringLiteral("%1 has no automation inside this clip (it spans lane ticks %2..%3)").arg(parameter.id()).arg(w.low).arg(w.high));
		return false;
	}
	if (static_cast<int>(points->size()) > MaxPoints) { *error = refuse(ControlErrorKind::Refused, QStringLiteral("at most %1 points").arg(MaxPoints)); return false; }
	return true;
}

ControlResult convertFromAutomation(const QJsonObject& args)
{
	Conversion c;
	ControlResult error;
	if (!resolveConversion(args, true, &c, &error)) { return error; }
	AutomationClip* source = existingAutomationClip(c.parameter.model);
	if (source == nullptr || !source->hasAutomation())
	{
		return refuse(ControlErrorKind::NotFound, QStringLiteral("%1 has no automation points to convert").arg(c.parameter.id()));
	}
	std::vector<ControllerPoint> points;
	if (!planLane(c.clip, source, c.parameter, &points, &error)) { return error; }

	const ControllerLane* existing = c.clip->controllers().lane(ControllerKind::ControlChange, c.controller);
	const QJsonArray before = existing != nullptr ? pointsJson(existing->points) : QJsonArray();
	c.clip->addJournalCheckPoint();
	c.clip->setControllerPoints(ControllerKind::ControlChange, c.controller, std::move(points));
	const ControllerLane* now = c.clip->controllers().lane(ControllerKind::ControlChange, c.controller);
	QJsonObject result = conversionReply(c, now != nullptr ? static_cast<int>(now->points.size()) : 0);
	result.insert(QStringLiteral("source_progression"), progressionLabel(source->progressionType()));
	result.insert(QStringLiteral("points"), now != nullptr ? pointsJson(now->points) : QJsonArray());
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("points"), before}},
		QStringLiteral("clip.controllers_set"), {{QStringLiteral("clip"), clipId(c.ref.id)}, {QStringLiteral("lane"), c.laneId}, {QStringLiteral("points"), before}},
		true, QStringLiteral("MidiClip journal checkpoint: undo restores the clip's lanes")));
	return ControlResult::success(result);
}

QJsonObject convertSchema()
{
	return objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("lane"), stringProperty()},
		{QStringLiteral("parameter"), stringProperty()}}, {QStringLiteral("clip"), QStringLiteral("lane")});
}

} // namespace

void registerClipControllerConvertCommands(ControlRegistry& registry)
{
	const QString shared = QStringLiteral("'clip' is a MIDI clip, 'lane' its cc:<0..127> lane (pitch bend, aftertouch and program lanes are refused), "
		"'parameter' an id automation.get_state lists for the clip's OWNING track ('<plugin>/<index>'); left out it must name exactly one candidate, "
		"otherwise the call is refused and lists them. A switch or a list of choices is refused (the CC range maps linearly onto min..max). "
		"Refused calls change nothing. One undo step. Ticks keep their song time: lane tick t is song tick clip start + start offset + t.");
	control::defineCommandWithSchema(registry, QStringLiteral("clip.controllers_to_automation"),
		QStringLiteral("Copy a MIDI clip's CC lane to the automation of one of its track's device parameters: CC 0..127 spans the parameter's range, "
			"the target clip's nodes between the first and last converted tick are replaced and the clip is made linear (refused when that would change "
			"other nodes of a non-linear clip). Creates the parameter's automation track if it has none. Returns the automation as automation.get_state shows it. ") + shared,
		convertSchema(), true, convertToAutomation);
	control::defineCommandWithSchema(registry, QStringLiteral("clip.controllers_from_automation"),
		QStringLiteral("Copy the automation of one of a MIDI clip's track's device parameters into a CC lane of that clip, replacing the lane: the nodes that "
			"fall inside the clip become points (rounded to the nearest CC step), and where the clip cuts through the curve its value at the cut is added. "
			"The lane is linear between points whatever the clip's progression ('source_progression' reports it). Returns the lane's points. ") + shared,
		convertSchema(), true, convertFromAutomation);
}

} // namespace lmms
