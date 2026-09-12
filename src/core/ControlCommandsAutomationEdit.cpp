/*
 * ControlCommandsAutomationEdit.cpp - the mutating half of the automation.*
 *                                     command group (SPEC A11-A16):
 *                                     add_point / remove_point / clear.
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

#include <memory>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

using control::AutomationParameter;

//! Resolves the track + parameter the mutating commands address.
bool resolveParameter(const QJsonObject& args, ControlTarget* target,
	AutomationParameter* parameter, ControlResult* error)
{
	if (!resolveControlTarget(args.value(QStringLiteral("track")).toString(), target, error))
	{
		return false;
	}
	return control::findAutomationParameter(*target,
		args.value(QStringLiteral("parameter")).toString(), parameter, error);
}

tick_t ticksArg(const QJsonObject& args)
{
	return static_cast<tick_t>(args.value(QStringLiteral("ticks")).toDouble());
}

QJsonObject pointRefusal(const QString& parameter, tick_t ticks)
{
	return QJsonObject{{QStringLiteral("parameter"), parameter},
		{QStringLiteral("ticks"), static_cast<qint64>(ticks)}};
}

ControlResult automationAddPoint(const QJsonObject& args)
{
	ControlTarget target;
	AutomationParameter parameter;
	ControlResult error;
	if (!resolveParameter(args, &target, &parameter, &error)) { return error; }

	const double value = args.value(QStringLiteral("value")).toDouble();
	if (value < static_cast<double>(parameter.model->minValue<float>()) ||
		value > static_cast<double>(parameter.model->maxValue<float>()))
	{
		return control::automatableRangeRefusal(parameter.model, value);
	}

	bool created = false;
	AutomationClip* clip = control::automationClipForModel(parameter.model, &created, &error);
	if (clip == nullptr) { return error; }

	const QJsonArray before = control::automationPointsJson(clip, parameter.model);
	if (created)
	{
		// SPEC A16 deliverable 5: this call created a whole AutomationTrack,
		// which no clip checkpoint can take back (the clip lives inside it). ONE
		// action step, recorded before the point is written, removes the track
		// again - so the first point is one undoable step like every later one.
		// No redo is offered: re-issue automation.add_point instead.
		auto holder = std::make_shared<Track*>(clip->getTrack());
		control::addUndoStep([holder]() {
			if (*holder != nullptr) { control::removeTrack(*holder); *holder = nullptr; }
		});
	}
	else
	{
		// SPEC A16: the clip is a JournallingObject, so this checkpoint is the
		// inverse the engine's own undo stack replays.
		clip->addJournalCheckPoint();
	}
	const tick_t ticks = ticksArg(args);
	// The wire value is the model's own unit; the clip stores the model's
	// inverse-scaled value, because Song::processAutomations() applies
	// scaledValue() to whatever the clip holds.
	clip->putValue(TimePos(ticks),
		parameter.model->inverseScaledValue(static_cast<float>(value)), false, true);

	QJsonObject result;
	result.insert(QStringLiteral("track"), target.id);
	result.insert(QStringLiteral("parameter"), parameter.id());
	result.insert(QStringLiteral("ticks"), static_cast<qint64>(ticks));
	result.insert(QStringLiteral("value"), value);
	result.insert(QStringLiteral("created_automation_track"), created);
	result.insert(QStringLiteral("automation"), control::automationJson(clip, parameter.model));
	result.insert(QStringLiteral("__transaction"),
		control::automationTransaction(parameter, before, before.size(), created));
	return ControlResult::success(result);
}

ControlResult automationRemovePoint(const QJsonObject& args)
{
	ControlTarget target;
	AutomationParameter parameter;
	ControlResult error;
	if (!resolveParameter(args, &target, &parameter, &error)) { return error; }

	const tick_t ticks = ticksArg(args);
	AutomationClip* clip = control::existingAutomationClip(parameter.model);
	if (clip == nullptr || !clip->getTimeMap().contains(static_cast<int>(ticks)))
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1 has no automation point at tick %2")
				.arg(parameter.id()).arg(static_cast<qint64>(ticks)));
	}

	const QJsonArray before = control::automationPointsJson(clip, parameter.model);
	clip->addJournalCheckPoint();
	clip->removeNode(TimePos(ticks));

	QJsonObject result;
	result.insert(QStringLiteral("track"), target.id);
	result.insert(QStringLiteral("parameter"), parameter.id());
	result.insert(QStringLiteral("removed"), pointRefusal(parameter.id(), ticks));
	result.insert(QStringLiteral("automation"), control::automationJson(clip, parameter.model));
	result.insert(QStringLiteral("__transaction"),
		control::automationTransaction(parameter, before, before.size(), false));
	return ControlResult::success(result);
}

ControlResult automationClear(const QJsonObject& args)
{
	ControlTarget target;
	AutomationParameter parameter;
	ControlResult error;
	if (!resolveParameter(args, &target, &parameter, &error)) { return error; }

	AutomationClip* clip = control::existingAutomationClip(parameter.model);
	if (clip == nullptr || !clip->hasAutomation())
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1 is not automated on %2").arg(parameter.id(), target.id));
	}

	const QJsonArray before = control::automationPointsJson(clip, parameter.model);
	clip->addJournalCheckPoint();
	// clear() empties the time map; the clip stays bound to the model, so the
	// parameter keeps whatever value the engine last applied to it.
	clip->clear();

	QJsonObject result;
	result.insert(QStringLiteral("track"), target.id);
	result.insert(QStringLiteral("parameter"), parameter.id());
	result.insert(QStringLiteral("cleared_points"), before.size());
	result.insert(QStringLiteral("automation"), control::automationJson(clip, parameter.model));
	result.insert(QStringLiteral("__transaction"),
		control::automationTransaction(parameter, before, before.size(), false));
	return ControlResult::success(result);
}

} // namespace

void registerAutomationEditCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("automation.add_point");
		cmd.group = QStringLiteral("automation");
		cmd.verb = QStringLiteral("add_point");
		cmd.description = QStringLiteral("Add or replace one automation point (ticks, value) on a "
			"track's device parameter. The value is the model's own unit and is range-checked "
			"against the model's own min..max. Creates the automation clip when the parameter has "
			"none yet, and says so: that first call is the one control.undo cannot fully reverse "
			"(the new AutomationTrack has no journal checkpoint). Later calls are reversible.");
		cmd.argsSchema = control::objectSchema({
			{QStringLiteral("track"), control::stringProperty()},
			{QStringLiteral("parameter"), control::stringProperty()},
			{QStringLiteral("ticks"), control::tickProperty()},
			{QStringLiteral("value"), control::numberProperty()},
		}, {QStringLiteral("track"), QStringLiteral("parameter"), QStringLiteral("ticks"),
			QStringLiteral("value")});
		cmd.resultSchema = control::objectSchema({
			{QStringLiteral("track"), control::stringProperty()},
			{QStringLiteral("parameter"), control::stringProperty()},
			{QStringLiteral("ticks"), control::tickProperty()},
			{QStringLiteral("value"), control::numberProperty()},
			{QStringLiteral("created_automation_track"), control::booleanProperty()},
			{QStringLiteral("automation"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return automationAddPoint(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("automation.remove_point");
		cmd.group = QStringLiteral("automation");
		cmd.verb = QStringLiteral("remove_point");
		cmd.description = QStringLiteral("Remove the automation point at an exact tick. The node "
			"list is a map keyed by tick, so the tick must match one. Reversible through the "
			"ProjectJournal.");
		cmd.argsSchema = control::objectSchema({
			{QStringLiteral("track"), control::stringProperty()},
			{QStringLiteral("parameter"), control::stringProperty()},
			{QStringLiteral("ticks"), control::tickProperty()},
		}, {QStringLiteral("track"), QStringLiteral("parameter"), QStringLiteral("ticks")});
		cmd.resultSchema = control::objectSchema({
			{QStringLiteral("track"), control::stringProperty()},
			{QStringLiteral("parameter"), control::stringProperty()},
			{QStringLiteral("removed"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
			{QStringLiteral("automation"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return automationRemovePoint(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("automation.clear");
		cmd.group = QStringLiteral("automation");
		cmd.verb = QStringLiteral("clear");
		cmd.description = QStringLiteral("Remove every automation point from a parameter's clip, "
			"leaving the clip bound to the model (an empty clip no longer drives the parameter). "
			"Reversible through the ProjectJournal.");
		cmd.argsSchema = control::objectSchema({
			{QStringLiteral("track"), control::stringProperty()},
			{QStringLiteral("parameter"), control::stringProperty()},
		}, {QStringLiteral("track"), QStringLiteral("parameter")});
		cmd.resultSchema = control::objectSchema({
			{QStringLiteral("track"), control::stringProperty()},
			{QStringLiteral("parameter"), control::stringProperty()},
			{QStringLiteral("cleared_points"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("automation"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return automationClear(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
