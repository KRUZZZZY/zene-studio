/*
 * ControlCommandsAutomationTools.cpp - automation switches, trim and moving a point
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
/*! 0.5.0 L06 (W3), automation's working tools:
 *  - automation.switches (ED-200, ED-201): read and write over all automation at once, and writes
 *    punched to the transport's punch region;
 *  - automation.trim_get / trim_set (ED-201): a control's trim - the offset added to its written
 *    automation as it plays, never written into the clip;
 *  - automation.move_point (ED-194): one point moved in time and value, one undo step - what the
 *    keyboard and a drag in the automation editor send;
 *  - automation.write_to_end (ED-201): a write-pass helper - from a tick to the song's end the control
 *    holds one value. */

#include <algorithm>
#include <iterator>

#include <QJsonArray>

#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "AutomationSwitches.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

QJsonObject switchesJson()
{
	const AutomationSwitches& s = AutomationSwitches::instance();
	return {{QStringLiteral("read"), s.reads()}, {QStringLiteral("write"), s.writes()}, {QStringLiteral("punched_writes"), s.punchedWrites()}};
}

ControlResult switches(const QJsonObject& args)
{
	AutomationSwitches& s = AutomationSwitches::instance();
	const bool reads = args.value(QStringLiteral("read")).toBool(s.reads());
	const bool writes = args.value(QStringLiteral("write")).toBool(s.writes());
	const bool punched = args.value(QStringLiteral("punched_writes")).toBool(s.punchedWrites());
	const bool r0 = s.reads();
	const bool w0 = s.writes();
	const bool p0 = s.punchedWrites();
	if (reads != r0 || writes != w0 || punched != p0)
	{
		s.set(reads, writes, punched);
		addUndoStep([r0, w0, p0]() { AutomationSwitches::instance().set(r0, w0, p0); },
			[reads, writes, punched]() { AutomationSwitches::instance().set(reads, writes, punched); });
		Engine::getSong()->setModified();
	}
	return ControlResult::success(switchesJson());
}

ControlResult trimGet(const QJsonObject& args)
{
	ControlTarget target;
	AutomationParameter parameter;
	ControlResult error;
	if (!resolveAutomationParameter(args, &target, &parameter, &error)) { return error; }
	return ControlResult::success({{QStringLiteral("track"), target.id}, {QStringLiteral("parameter"), parameter.id()},
		{QStringLiteral("offset"), static_cast<double>(parameter.model->trimOffset())}});
}

ControlResult trimSet(const QJsonObject& args)
{
	ControlTarget target;
	AutomationParameter parameter;
	ControlResult error;
	if (!resolveAutomationParameter(args, &target, &parameter, &error)) { return error; }
	const auto offset = static_cast<float>(args.value(QStringLiteral("offset")).toDouble());
	const float range = parameter.model->maxValue<float>() - parameter.model->minValue<float>();
	if (offset < -range || offset > range) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'offset' must be within the control's range")); }
	// Runtime state, as the mode is (not saved): the step puts the previous offset back by the same id.
	const QJsonObject where{{QStringLiteral("track"), target.id}, {QStringLiteral("parameter"), parameter.id()}};
	const float before = parameter.model->trimOffset();
	const QJsonObject reply{{QStringLiteral("track"), target.id}, {QStringLiteral("parameter"), parameter.id()},
		{QStringLiteral("offset"), static_cast<double>(offset)}};
	if (before == offset) { return ControlResult::success(reply); }  // nothing changed: no undo step (review W3-batchB #4)
	parameter.model->setTrimOffset(offset);
	const auto apply = [where](float value) {
		ControlTarget t;
		AutomationParameter p;
		ControlResult e;
		if (resolveAutomationParameter(where, &t, &p, &e)) { p.model->setTrimOffset(value); }
	};
	addUndoStep([apply, before]() { apply(before); }, [apply, offset]() { apply(offset); });
	return ControlResult::success(reply);
}

//! A time map is keyed by tick, so moving a point onto a tick another node already holds would
//! silently delete that node. Moving a point onto its own tick is a plain value change, and stays
//! allowed.
ControlResult occupiedDestination(const AutomationClip* clip, tick_t ticks, tick_t to,
	const QString& parameterId, tick_t toSong)
{
	if (to != ticks && clip->getTimeMap().contains(TimePos(to)))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 already has a point at %2").arg(parameterId).arg(static_cast<qint64>(toSong)));
	}
	return ControlResult::success();
}

void putMovedNode(AutomationClip* clip, tick_t to, const AutomationNode& source, float inValue)
{
	const float outValue = source.getValueOffset() == 0.f ? inValue : source.getOutValue();
	clip->putValues(TimePos(to), inValue, outValue, false);
	AutomationNode& movedNode = clip->getTimeMap()[TimePos(to)];
	movedNode.setCurve(source.curve(), source.bend());
	if (source.lockedTangents())
	{
		movedNode.setInTangent(source.getInTangent());
		movedNode.setOutTangent(source.getOutTangent());
		movedNode.setLockedTangents(true);
	}
}

ControlResult movePoint(const QJsonObject& args)
{
	ControlTarget target;
	AutomationParameter parameter;
	ControlResult error;
	if (!resolveAutomationParameter(args, &target, &parameter, &error)) { return error; }
	AutomationClip* clip = existingAutomationClip(parameter.model);
	// A time map is keyed by CLIP-LOCAL ticks, so both the point named by 'ticks' and the
	// destination are song ticks on the way in and local ticks inside the clip - the same
	// conversion automation.set_curve (ControlCommandsAutomationLanes.cpp:209),
	// automation.draw_shape (:158) and automation.transform (:172) make. The reply answers in
	// song ticks, so the caller never has to know the clip's own origin.
	const int origin = clip != nullptr ? clip->startPosition().getTicks() : 0;
	const auto songTicks = static_cast<tick_t>(args.value(QStringLiteral("ticks")).toDouble());
	const auto toSong = static_cast<tick_t>(args.value(QStringLiteral("new_ticks")).toDouble(static_cast<double>(songTicks)));
	const auto ticks = songTicks - origin;
	if (clip == nullptr || ticks < 0 || !clip->getTimeMap().contains(TimePos(ticks)))
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("%1 has no point at %2").arg(parameter.id()).arg(static_cast<qint64>(songTicks)));
	}
	const AutomationNode oldNode = clip->getTimeMap().value(TimePos(ticks));
	const float old = oldNode.getInValue();
	const auto to = toSong - origin;
	const double value = args.value(QStringLiteral("new_value")).toDouble(parameter.model->scaledValue(old));
	if (to < 0)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("destination tick %1 is before automation clip start at %2")
				.arg(static_cast<qint64>(toSong)).arg(static_cast<qint64>(origin)));
	}
	// Written as "inside the range", not "outside it": NaN fails both comparisons, so it is refused with the rest.
	if (!(value >= parameter.model->minValue<float>() && value <= parameter.model->maxValue<float>()))
	{
		return automatableRangeRefusal(parameter.model, value);
	}
	if (const ControlResult taken = occupiedDestination(clip, ticks, to, parameter.id(), toSong); !taken.ok) { return taken; }
	const QJsonArray before = automationPointsJson(clip, parameter.model);
	// Moving the node keeps its out side when it is discontinuous, and a requested new in value
	// moves only that side, as it does in AutomationClip::setDragValue().
	const float inValue = parameter.model->inverseScaledValue(static_cast<float>(value));
	clip->addJournalCheckPoint();
	clip->removeNode(TimePos(ticks));
	putMovedNode(clip, to, oldNode, inValue);
	QJsonObject result{{QStringLiteral("track"), target.id}, {QStringLiteral("parameter"), parameter.id()}, {QStringLiteral("ticks"), static_cast<double>(toSong)},
		{QStringLiteral("value"), value}, {QStringLiteral("automation"), automationJson(clip, parameter.model)}};
	result.insert(QStringLiteral("__transaction"), automationTransaction(parameter, before, before.size(), false));
	return ControlResult::success(result);
}

//! The song's end in ticks: the end of its last clip, never before @a from.
tick_t songEndFrom(tick_t from)
{
	Song* song = Engine::getSong();
	song->updateLength();
	return std::max<tick_t>(from, static_cast<tick_t>(song->length()) * TimePos::ticksPerBar());
}

//! The clip's value arriving at local @a tick: a point there keeps its own in-value, else the curve's.
float arrivingValue(const AutomationClip* clip, tick_t tick)
{
	const auto& map = clip->getTimeMap();
	return map.contains(TimePos(tick)) ? map.value(TimePos(tick)).getInValue() : clip->valueAt(TimePos(tick));
}

//! Every point of @a clip after local @a tick, removed; how many there were.
int removeAfter(AutomationClip* clip, tick_t tick)
{
	const auto& map = clip->getTimeMap();
	const auto first = map.upperBound(TimePos(tick));
	const int removed = static_cast<int>(std::distance(first, map.constEnd()));
	if (removed > 0) { clip->removeNodes(tick + 1, std::prev(map.constEnd()).key()); }
	return removed;
}

/*! ED-201, write to end: from 'ticks' to the song's end the control holds 'value' (its current value when left
 *  out). Every point after 'ticks' goes; the point at 'ticks' arrives at the curve's value there and leaves at the
 *  new one (an in/out step, so the curve before it keeps its shape); a point at the song's end holds it. */
ControlResult writeToEnd(const QJsonObject& args)
{
	ControlTarget target;
	AutomationParameter parameter;
	ControlResult error;
	if (!resolveAutomationParameter(args, &target, &parameter, &error)) { return error; }
	AutomatableModel* model = parameter.model;
	const double value = args.value(QStringLiteral("value")).toDouble(model->value<float>());
	if (!(value >= model->minValue<float>() && value <= model->maxValue<float>())) { return automatableRangeRefusal(model, value); }
	const auto fromSong = static_cast<tick_t>(args.value(QStringLiteral("ticks")).toDouble());
	AutomationClip* existing = existingAutomationClip(model);
	if (fromSong < 0 || (existing != nullptr && fromSong < existing->startPosition().getTicks()))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'ticks' is before the automation clip of %1").arg(parameter.id()));
	}
	const QJsonArray before = existing != nullptr ? automationPointsJson(existing, model) : QJsonArray{};
	const tick_t endSong = songEndFrom(fromSong);
	bool created = false;
	AutomationClip* clip = automationClipForEdit(model, &created, &error);  // checked above: nothing is refused after this
	if (clip == nullptr) { return error; }
	const tick_t from = fromSong - clip->startPosition().getTicks();
	const float stored = model->inverseScaledValue(static_cast<float>(value));
	const float arriving = arrivingValue(clip, from);
	const int removed = removeAfter(clip, from);
	clip->putValues(TimePos(from), arriving, stored, false);
	const tick_t end = endSong - clip->startPosition().getTicks();
	if (end > from) { clip->putValue(TimePos(end), stored, false); }
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("track"), target.id}, {QStringLiteral("parameter"), parameter.id()},
		{QStringLiteral("ticks"), static_cast<double>(fromSong)}, {QStringLiteral("end"), static_cast<double>(endSong)},
		{QStringLiteral("value"), value}, {QStringLiteral("removed"), removed}, {QStringLiteral("automation"), automationJson(clip, model)}};
	result.insert(QStringLiteral("__transaction"), automationTransaction(parameter, before, before.size(), created));
	return ControlResult::success(result);
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, const QStringList& required,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QLatin1String(id), description, objectSchema(args, QJsonArray::fromStringList(required)), mutating, handler);
}

} // namespace

void registerAutomationToolCommands(ControlRegistry& registry)
{
	AutomationSwitches::instance();  // registers its project section before a project loads
	const QJsonObject which{{QStringLiteral("track"), stringProperty()}, {QStringLiteral("parameter"), stringProperty()}};
	const QStringList both{QStringLiteral("track"), QStringLiteral("parameter")};
	add(registry, "automation.switches", QStringLiteral("Switches over all automation at once, on top of each control's own mode: "
		"'read' off keeps every control at its manual value, 'write' off stops every control recording, 'punched_writes' "
		"records only inside the transport's punch region. Saved with the project when not at their defaults. With no "
		"arguments it reports them. One undo step."),
		{{QStringLiteral("read"), booleanProperty()}, {QStringLiteral("write"), booleanProperty()}, {QStringLiteral("punched_writes"), booleanProperty()}},
		{}, true, switches);
	add(registry, "automation.trim_get", QStringLiteral("A control's trim: the offset added to its written automation as it plays."),
		which, both, false, trimGet);
	QJsonObject trim = which;
	trim.insert(QStringLiteral("offset"), numberProperty());
	add(registry, "automation.trim_set", QStringLiteral("Set a control's trim - an offset (in the control's own unit) added to its "
		"written automation as it plays, never written into the clip, so the curve's shape stays and the whole of it moves. "
		"Runtime state like the automation mode (not saved). One undo step."),
		trim, {QStringLiteral("track"), QStringLiteral("parameter"), QStringLiteral("offset")}, true, trimSet);
	QJsonObject move = which;
	for (const char* key : {"ticks", "new_ticks"}) { move.insert(QLatin1String(key), tickProperty()); }
	move.insert(QStringLiteral("new_value"), numberProperty());
	add(registry, "automation.move_point", QStringLiteral("Move the automation point at 'ticks' to 'new_ticks' and/or 'new_value' (the "
		"control's own unit, range-checked) - what the automation editor's arrow keys and drags send. One undo step."),
		move, {QStringLiteral("track"), QStringLiteral("parameter"), QStringLiteral("ticks")}, true, movePoint);
	QJsonObject toEnd = which;
	toEnd.insert(QStringLiteral("ticks"), tickProperty());
	toEnd.insert(QStringLiteral("value"), numberProperty());
	add(registry, "automation.write_to_end", QStringLiteral("Write to end: from 'ticks' (song ticks) to the song's end the control holds "
		"'value' (its own unit, range-checked; its current value when left out). Every later point is removed, the point at "
		"'ticks' steps from the curve's value there to the new one, and a point at the song's end (the end of its last clip) "
		"holds it - what a write pass leaves to the end. Creates the automation clip when the control has none. Answers "
		"{track, parameter, ticks, end, value, removed, automation}. One undo step."),
		toEnd, {QStringLiteral("track"), QStringLiteral("parameter"), QStringLiteral("ticks")}, true, writeToEnd);
}

} // namespace lmms
