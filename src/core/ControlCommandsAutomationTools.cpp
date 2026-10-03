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
 *    keyboard and a drag in the automation editor send. */

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
	if (offset < -range || offset > range) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'offset' is within the control's range")); }
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

ControlResult movePoint(const QJsonObject& args)
{
	ControlTarget target;
	AutomationParameter parameter;
	ControlResult error;
	if (!resolveAutomationParameter(args, &target, &parameter, &error)) { return error; }
	AutomationClip* clip = existingAutomationClip(parameter.model);
	const auto ticks = static_cast<tick_t>(args.value(QStringLiteral("ticks")).toDouble());
	if (clip == nullptr || !clip->getTimeMap().contains(TimePos(ticks)))
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("%1 has no point at %2").arg(parameter.id()).arg(static_cast<qint64>(ticks)));
	}
	const float old = clip->getTimeMap().value(TimePos(ticks)).getInValue();
	const auto to = static_cast<tick_t>(args.value(QStringLiteral("new_ticks")).toDouble(static_cast<double>(ticks)));
	const double value = args.value(QStringLiteral("new_value")).toDouble(parameter.model->scaledValue(old));
	if (value < parameter.model->minValue<float>() || value > parameter.model->maxValue<float>() || to < 0)
	{
		return automatableRangeRefusal(parameter.model, value);
	}
	const QJsonArray before = automationPointsJson(clip, parameter.model);
	clip->addJournalCheckPoint();
	clip->removeNode(TimePos(ticks));
	clip->putValue(TimePos(to), parameter.model->inverseScaledValue(static_cast<float>(value)), false);
	QJsonObject result{{QStringLiteral("track"), target.id}, {QStringLiteral("parameter"), parameter.id()}, {QStringLiteral("ticks"), static_cast<double>(to)},
		{QStringLiteral("value"), value}, {QStringLiteral("automation"), automationJson(clip, parameter.model)}};
	result.insert(QStringLiteral("__transaction"), automationTransaction(parameter, before, before.size(), false));
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
}

} // namespace lmms
