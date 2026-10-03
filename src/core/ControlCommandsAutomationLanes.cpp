/*
 * ControlCommandsAutomationLanes.cpp - automation lanes under a track, trim lanes, shapes, transform, thin, curves
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

/*! 0.5.0 L06: ED-181 a lane under the track for one of its parameters (automation.lane_add -
 *  an automation track placed right below it that follows it), ED-199 a trim lane (a relative
 *  clip, automation.add_trim), ED-184 shapes, ED-186 transform, ED-187 thinning, ED-185 a
 *  point's own curve, ED-191 the last touched parameter. Every edit is one undo step: a clip it
 *  edits is checkpointed, a lane it creates is removed again by one action step. */

#include <memory>

#include <QJsonArray>

#include "AutomationClip.h"
#include "AutomationNode.h"
#include "AutomationTrack.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlStructuralSupport.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int DefaultLaneBars = 4;

QJsonObject clipReply(const ControlTarget& target, const AutomationParameter& parameter, AutomationClip* clip)
{
	return {{QStringLiteral("track"), target.id}, {QStringLiteral("parameter"), parameter.id()},
		{QStringLiteral("automation"), automationJson(clip, parameter.model)}};
}

//! A new automation track right under @a owner, following it, with one clip bound to @a model
//! across the song (at least DefaultLaneBars); its creation is ONE undo step that removes it.
AutomationClip* createLane(Track* owner, AutomatableModel* model, const QString& name, bool relative)
{
	Song* song = Engine::getSong();
	auto* lane = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, song));
	auto holder = std::make_shared<Track*>(lane);
	addUndoStep([holder] { if (*holder != nullptr) { removeTrack(*holder); *holder = nullptr; } });
	lane->setName(name);
	lane->setFollowsTrack(owner->id());
	moveTrackToIndex(lane, trackIndexIn(song, owner) + 1);
	auto* clip = dynamic_cast<AutomationClip*>(lane->createClip(TimePos(0)));
	clip->addObject(model);
	clip->setRelative(relative);
	clip->changeLength(TimePos(std::max<int>(DefaultLaneBars, song->length()) * TimePos::ticksPerBar()));
	clip->setAutoResize(false);
	const float start = relative ? (clip->getMin() + clip->getMax()) / 2.f : model->inverseScaledValue(model->value<float>());
	clip->putValue(TimePos(0), start, false, true);
	return clip;
}

//! The parameter and the track that owns it, for the lane commands: `track` must be a trk- target.
bool laneOwner(const QJsonObject& args, ControlTarget* target, AutomationParameter* parameter, Track** owner, ControlResult* error)
{
	if (!resolveAutomationParameter(args, target, parameter, error)) { return false; }
	*owner = resolveTrack(target->id, error);
	return *owner != nullptr;
}

ControlResult addLane(const QJsonObject& args, bool relative)
{
	ControlTarget target;
	AutomationParameter parameter;
	Track* owner = nullptr;
	ControlResult error;
	if (!laneOwner(args, &target, &parameter, &owner, &error)) { return error; }
	const QString name = owner->name() + QStringLiteral(": ") + parameter.model->displayName()
		+ (relative ? QObject::tr(" (trim)") : QString());
	AutomationClip* clip = createLane(owner, parameter.model, name, relative);
	QJsonObject result = clipReply(target, parameter, clip);
	result.insert(QStringLiteral("lane"), trackIdOf(clip->getTrack()));
	result.insert(QStringLiteral("relative"), relative);
	result.insert(QStringLiteral("__transaction"), transactionPayload({}, QStringLiteral("track.remove"),
		{{QStringLiteral("track"), trackIdOf(clip->getTrack())}}, true, QStringLiteral("action step: undo removes the lane")));
	return ControlResult::success(result);
}

//! The parameter's clip readied for one undoable edit (automationClipForEdit), or a failure.
AutomationClip* editableClip(const QJsonObject& args, ControlTarget* target, AutomationParameter* parameter, ControlResult* error)
{
	if (!resolveAutomationParameter(args, target, parameter, error)) { return nullptr; }
	bool created = false;
	return automationClipForEdit(parameter->model, &created, error);
}

const QStringList& shapeNames()
{
	static const QStringList names{QStringLiteral("sine"), QStringLiteral("triangle"), QStringLiteral("square"),
		QStringLiteral("saw"), QStringLiteral("random"), QStringLiteral("adsr")};
	return names;
}

ControlResult drawShape(const QJsonObject& args)
{
	if (args.value(QStringLiteral("end")).toInt() <= args.value(QStringLiteral("start")).toInt())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'end' must be after 'start'"));
	}
	ControlTarget target;
	AutomationParameter parameter;
	ControlResult error;
	AutomationClip* clip = editableClip(args, &target, &parameter, &error);
	if (clip == nullptr) { return error; }
	AutomatableModel* model = parameter.model;
	const float low = model->inverseScaledValue(static_cast<float>(args.value(QStringLiteral("min")).toDouble(model->minValue<float>())));
	const float high = model->inverseScaledValue(static_cast<float>(args.value(QStringLiteral("max")).toDouble(model->maxValue<float>())));
	const int start = args.value(QStringLiteral("start")).toInt();
	const int end = args.value(QStringLiteral("end")).toInt();
	clip->drawShape(start - clip->startPosition().getTicks(), end - clip->startPosition().getTicks(),
		static_cast<AutomationClip::Shape>(shapeNames().indexOf(args.value(QStringLiteral("shape")).toString())),
		args.value(QStringLiteral("cycles")).toInt(1), low, high, args.value(QStringLiteral("step")).toInt(12),
		static_cast<unsigned>(args.value(QStringLiteral("seed")).toInt(1)));
	return ControlResult::success(clipReply(target, parameter, clip));
}

ControlResult transform(const QJsonObject& args)
{
	ControlTarget target;
	AutomationParameter parameter;
	ControlResult error;
	AutomationClip* clip = editableClip(args, &target, &parameter, &error);
	if (clip == nullptr) { return error; }
	const int origin = clip->startPosition().getTicks();
	const int moved = clip->transformRange(args.value(QStringLiteral("start")).toInt() - origin,
		args.value(QStringLiteral("end")).toInt() - origin, args.value(QStringLiteral("time_scale")).toDouble(1.0),
		args.value(QStringLiteral("time_offset")).toInt(0), args.value(QStringLiteral("value_scale")).toDouble(1.0),
		args.value(QStringLiteral("value_offset")).toDouble(0.0));
	QJsonObject result = clipReply(target, parameter, clip);
	result.insert(QStringLiteral("moved"), moved);
	return ControlResult::success(result);
}

ControlResult thin(const QJsonObject& args)
{
	ControlTarget target;
	AutomationParameter parameter;
	ControlResult error;
	AutomationClip* clip = editableClip(args, &target, &parameter, &error);
	if (clip == nullptr) { return error; }
	const int removed = clip->thin(static_cast<float>(args.value(QStringLiteral("tolerance")).toDouble(0.01)));
	QJsonObject result = clipReply(target, parameter, clip);
	result.insert(QStringLiteral("removed"), removed);
	return ControlResult::success(result);
}

const QStringList& curveNames()
{
	static const QStringList names{QStringLiteral("clip"), QStringLiteral("hold"), QStringLiteral("linear"),
		QStringLiteral("smooth"), QStringLiteral("bend")};
	return names;
}

ControlResult setCurve(const QJsonObject& args)
{
	ControlTarget target;
	AutomationParameter parameter;
	ControlResult error;
	if (!resolveAutomationParameter(args, &target, &parameter, &error)) { return error; }
	AutomationClip* clip = existingAutomationClip(parameter.model);
	const int at = args.value(QStringLiteral("ticks")).toInt() - (clip != nullptr ? clip->startPosition().getTicks() : 0);
	if (clip == nullptr || !clip->getTimeMap().contains(at))
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no automation point at that tick"));
	}
	clip->addJournalCheckPoint();
	clip->getTimeMap()[at].setCurve(static_cast<AutomationNode::Curve>(curveNames().indexOf(args.value(QStringLiteral("curve")).toString())),
		static_cast<float>(args.value(QStringLiteral("bend")).toDouble(0.0)));
	emit clip->dataChanged();
	return ControlResult::success(clipReply(target, parameter, clip));
}

ControlResult lastTouched(const QJsonObject&)
{
	auto* model = dynamic_cast<AutomatableModel*>(Engine::projectJournal()->journallingObject(AutomatableModel::lastTouched()));
	ParameterAddress address;
	if (model == nullptr || !addressableParameterForModel(model, &address))
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no addressable parameter has been touched yet"));
	}
	return ControlResult::success({{QStringLiteral("track"), address.target}, {QStringLiteral("parameter"), address.parameter.id()},
		{QStringLiteral("name"), model->fullDisplayName()}});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	bool mutating, std::function<ControlResult(const QJsonObject&)> handler)
{
	control::defineCommandWithSchema(registry, QStringLiteral("automation.") + QLatin1String(verb), description, args, mutating, handler);
}

QJsonObject withParameter(QJsonObject properties, const QJsonArray& required = {})
{
	properties.insert(QStringLiteral("track"), stringProperty());
	properties.insert(QStringLiteral("parameter"), stringProperty());
	QJsonArray all{QStringLiteral("track"), QStringLiteral("parameter")};
	for (const QJsonValue& key : required) { all.append(key); }
	return objectSchema(properties, all);
}

} // namespace

void registerAutomationLaneCommands(ControlRegistry& registry)
{
	const QString step = QStringLiteral(" One undo step.");
	add(registry, "lane_add", QStringLiteral("A lane for one of a track's parameters (automation.get_state names them): an "
		"automation track placed right under the track, following it (its points move with the track's clips when "
		"arrangement.modes says so), with a clip across the song starting at the parameter's value.") + step,
		withParameter({}), true, [](const QJsonObject& args) { return addLane(args, false); });
	add(registry, "add_trim", QStringLiteral("A trim lane: a RELATIVE clip under the track whose distance from the middle of "
		"the parameter's range is added to what the parameter's own automation (or its value) says - the offset curve "
		"of trim automation.") + step, withParameter({}), true, [](const QJsonObject& args) { return addLane(args, true); });
	add(registry, "draw_shape", QStringLiteral("Replace a parameter's points between 'start' and 'end' (song ticks) with a "
		"shape - sine, triangle, square, saw, random or adsr - over 'cycles' periods between 'min' and 'max' (the "
		"parameter's own units; default its range), a point every 'step' ticks ('seed' repeats random).") + step,
		withParameter({{QStringLiteral("start"), tickProperty()}, {QStringLiteral("end"), tickProperty()},
			{QStringLiteral("shape"), enumProperty(shapeNames())}, {QStringLiteral("cycles"), integerProperty(1, 256)},
			{QStringLiteral("min"), numberProperty()}, {QStringLiteral("max"), numberProperty()},
			{QStringLiteral("step"), integerProperty(1, 3072)}, {QStringLiteral("seed"), integerProperty()}},
			{QStringLiteral("start"), QStringLiteral("end"), QStringLiteral("shape")}), true, drawShape);
	add(registry, "transform", QStringLiteral("Rescale a parameter's points between 'start' and 'end': in time about 'start' "
		"('time_scale', then 'time_offset' ticks) and in value about the middle of its range ('value_scale', then "
		"'value_offset').") + step, withParameter({{QStringLiteral("start"), tickProperty()}, {QStringLiteral("end"), tickProperty()},
			{QStringLiteral("time_scale"), numberProperty()}, {QStringLiteral("time_offset"), integerProperty()},
			{QStringLiteral("value_scale"), numberProperty()}, {QStringLiteral("value_offset"), numberProperty()}},
			{QStringLiteral("start"), QStringLiteral("end")}), true, transform);
	add(registry, "thin", QStringLiteral("Remove the points of a parameter that lie within 'tolerance' of the line through "
		"their kept neighbours (Douglas-Peucker; the first and last stay).") + step,
		withParameter({{QStringLiteral("tolerance"), numberProperty()}}), true, thin);
	add(registry, "set_curve", QStringLiteral("Give the point at 'ticks' its own curve to the next point: clip (the clip's "
		"progression), hold, linear, smooth or bend ('bend' -1..1).") + step,
		withParameter({{QStringLiteral("ticks"), tickProperty()}, {QStringLiteral("curve"), enumProperty(curveNames())},
			{QStringLiteral("bend"), numberProperty()}}, {QStringLiteral("ticks"), QStringLiteral("curve")}), true, setCurve);
	add(registry, "last_touched", QStringLiteral("The parameter a person or a command last changed (not the automation): "
		"its track and parameter ids, ready for automation.lane_add."), objectSchema({}), false, lastTouched);
}

} // namespace lmms
