/*
 * ControlCommandsAutomation.cpp - automation.get_state / automation.mode_set
 *                                 (SPEC A11-A16).
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

#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

using control::AutomationParameter;

QString trackTypeName(Track::Type type)
{
	switch (type)
	{
		case Track::Type::Instrument: return QStringLiteral("instrument");
		case Track::Type::Pattern: return QStringLiteral("pattern");
		case Track::Type::Sample: return QStringLiteral("sample");
		case Track::Type::Event: return QStringLiteral("event");
		case Track::Type::Video: return QStringLiteral("video");
		case Track::Type::Automation: return QStringLiteral("automation");
		case Track::Type::HiddenAutomation: return QStringLiteral("hidden_automation");
		// A folder track (owner items 3+20+21).
		case Track::Type::Folder: return QStringLiteral("folder");
		case Track::Type::Count: break;
	}
	return QStringLiteral("unknown");
}

//! The parameters of one song track, honouring the automated_only filter.
QJsonArray parameterListJson(Track* track, bool automatedOnly, int* automatedCount)
{
	QJsonArray parameters;
	ControlTarget target;
	ControlResult error;
	const QString id = control::trackIdOf(track);
	if (!resolveControlTarget(id, &target, &error)) { return parameters; }

	for (const AutomationParameter& parameter : control::automationParameters(target))
	{
		const QJsonObject entry = control::automationParameterJson(parameter);
		if (entry.value(QStringLiteral("automated")).toBool())
		{
			++(*automatedCount);
		}
		else if (automatedOnly)
		{
			continue;
		}
		parameters.append(entry);
	}
	return parameters;
}

QJsonObject trackState(Track* track, int index, bool automatedOnly, int* automatedCount)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("id"), control::trackIdOf(track));
	entry.insert(QStringLiteral("index"), index);
	entry.insert(QStringLiteral("name"), track->name());
	entry.insert(QStringLiteral("type"), trackTypeName(track->type()));
	entry.insert(QStringLiteral("device_chain"), dynamic_cast<InstrumentTrack*>(track) != nullptr ||
		track->type() == Track::Type::Sample);
	entry.insert(QStringLiteral("parameters"),
		parameterListJson(track, automatedOnly, automatedCount));
	return entry;
}

/*! automation.get_state: per track, the automatable device parameters and, for
 * each automated one, the points on its clip. */
ControlResult automationGetState(const QJsonObject& args)
{
	const QString filter = args.value(QStringLiteral("track")).toString();
	const bool automatedOnly = args.value(QStringLiteral("automated_only")).toBool(false);
	const TrackContainer::TrackList& tracks = Engine::getSong()->tracks();

	QJsonArray entries;
	int automatedCount = 0;
	bool matched = filter.isEmpty();
	for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
	{
		if (!filter.isEmpty() && filter != control::trackIdOf(tracks[i])) { continue; }
		matched = true;
		QJsonObject entry = trackState(tracks[i], i, automatedOnly, &automatedCount);
		if (automatedOnly && entry.value(QStringLiteral("parameters")).toArray().isEmpty())
		{
			continue;
		}
		entries.append(entry);
	}
	if (!matched)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no track %1 (the song has %2)").arg(filter).arg(static_cast<int>(tracks.size())));
	}

	QJsonObject result;
	result.insert(QStringLiteral("tracks"), entries);
	result.insert(QStringLiteral("count"), entries.size());
	result.insert(QStringLiteral("track_count"), static_cast<int>(tracks.size()));
	result.insert(QStringLiteral("automated_parameter_count"), automatedCount);
	return ControlResult::success(result);
}

/*! automation.mode_set - set a parameter's automation mode.
 *
 * The engine's mode state machine (AutomatableModel::AutomationMode) is driven
 * from this surface, all five modes: off (ignore the curve: the manual value
 * stands and nothing is written), read (follow the curve, never write), touch,
 * latch, write. The mode is runtime state: it is not persisted in the project
 * file and it is not journalled, so a mode change has no undo -
 * docs/KNOWN-LIMITATIONS.md. The default is Read (follow automation, never
 * write), which is what every existing project already behaves as. The mode is
 * reported back per parameter by automation.get_state, through the same
 * spelling function this handler parses (control::automationModeName), so a set
 * can be observed and not only issued.
 */
ControlResult automationModeSet(const QJsonObject& args)
{
	ControlTarget target;
	AutomationParameter parameter;
	ControlResult error;
	if (!resolveControlTarget(args.value(QStringLiteral("track")).toString(), &target, &error))
	{
		return error;
	}
	if (!control::findAutomationParameter(target,
		args.value(QStringLiteral("parameter")).toString(), &parameter, &error))
	{
		return error;
	}

	const QString modeName = args.value(QStringLiteral("mode")).toString();
	AutomatableModel::AutomationMode mode;
	if (modeName == QStringLiteral("off"))
	{
		mode = AutomatableModel::AutomationMode::Off;
	}
	else if (modeName == QStringLiteral("read"))
	{
		mode = AutomatableModel::AutomationMode::Read;
	}
	else if (modeName == QStringLiteral("touch"))
	{
		mode = AutomatableModel::AutomationMode::Touch;
	}
	else if (modeName == QStringLiteral("latch"))
	{
		mode = AutomatableModel::AutomationMode::Latch;
	}
	else if (modeName == QStringLiteral("write"))
	{
		mode = AutomatableModel::AutomationMode::Write;
	}
	else
	{
		// The schema's closed enum should have caught this, but the handler
		// must still be honest for every string that reaches it.
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("mode must be one of off/read/touch/latch/write, not '%1'").arg(modeName));
	}

	const QString before = control::automationModeName(parameter.model->automationMode());
	const bool changed = before != modeName;
	parameter.model->setAutomationMode(mode);

	QJsonObject result;
	result.insert(QStringLiteral("track"), target.id);
	result.insert(QStringLiteral("parameter"), parameter.id());
	result.insert(QStringLiteral("mode"), control::automationModeName(parameter.model->automationMode()));
	result.insert(QStringLiteral("mode_before"), before);
	result.insert(QStringLiteral("changed"), changed);
	return ControlResult::success(result);
}

/*! automation.record_mode_set - set a parameter's automation CLIP record flag.
 *
 * Every automated parameter has AT MOST ONE clip that drives it. When that clip's
 * record flag is on, the manual value of the control is written into the clip at
 * every tick the transport runs - the legacy per-clip record path that predates
 * the mode state machine. This command toggles that flag. The clip is a
 * JournallingObject, so the inverse is a live checkpoint.
 */
ControlResult automationRecordModeSet(const QJsonObject& args)
{
	ControlTarget target;
	AutomationParameter parameter;
	ControlResult error;
	if (!resolveControlTarget(args.value(QStringLiteral("track")).toString(), &target, &error))
	{
		return error;
	}
	if (!control::findAutomationParameter(target,
		args.value(QStringLiteral("parameter")).toString(), &parameter, &error))
	{
		return error;
	}

	AutomationClip* clip = control::existingAutomationClip(parameter.model);
	if (clip == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1 has no automation clip on %2, so there is no record flag to set. "
				"Write an automation point with automation.add_point first.").arg(parameter.id(), target.id));
	}

	const QString modeName = args.value(QStringLiteral("mode")).toString();
	bool recording;
	if (modeName == QStringLiteral("on"))
	{
		recording = true;
	}
	else if (modeName == QStringLiteral("off"))
	{
		recording = false;
	}
	else
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("mode must be 'on' or 'off', not '%1'").arg(modeName));
	}

	const bool before = clip->isRecording();
	const bool changed = before != recording;
	if (changed)
	{
		clip->addJournalCheckPoint();
		clip->setRecording(recording);
	}

	QJsonObject result;
	result.insert(QStringLiteral("track"), target.id);
	result.insert(QStringLiteral("parameter"), parameter.id());
	result.insert(QStringLiteral("mode"), modeName);
	result.insert(QStringLiteral("mode_before"), before ? QStringLiteral("on") : QStringLiteral("off"));
	result.insert(QStringLiteral("changed"), changed);
	return ControlResult::success(result);
}

} // namespace

void registerAutomationCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("automation.get_state");
		cmd.group = QStringLiteral("automation");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("Per track, every automatable device parameter with its "
			"stable '<plugin>/<index>' id, its current automation 'mode' (what "
			"automation.mode_set sets) and, for each automated one, its clip's points and record "
			"flag. A point's 'value' is the model's own unit (what plugin.param_get reports); "
			"'raw_value' is what the clip stores. 'automated_only' trims the inventory.");
		cmd.argsSchema = control::objectSchema({
			{QStringLiteral("track"), control::stringProperty()},
			{QStringLiteral("automated_only"), control::booleanProperty()},
		});
		cmd.resultSchema = control::objectSchema({
			{QStringLiteral("tracks"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
			{QStringLiteral("count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("track_count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("automated_parameter_count"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		});
		cmd.handler = [](const QJsonObject& args) { return automationGetState(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("automation.mode_set");
		cmd.group = QStringLiteral("automation");
		cmd.verb = QStringLiteral("mode_set");
		cmd.description = QStringLiteral("Set a parameter's automation mode: off (ignore the "
			"curve - the manual value stands and nothing is written), read (follow the curve, "
			"never write), touch (write while the control is held, then return to reading), "
			"latch (write from the first touch until the transport run ends), or write (overwrite "
			"the pass while the transport runs). The mode is runtime state: not persisted and not "
			"journalled, so it has no undo. automation.get_state reports each parameter's mode, so "
			"the change can be observed and not only issued.");
		cmd.argsSchema = control::objectSchema({
			{QStringLiteral("track"), control::stringProperty()},
			{QStringLiteral("parameter"), control::stringProperty()},
			{QStringLiteral("mode"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
				{QStringLiteral("enum"), QJsonArray{QStringLiteral("off"), QStringLiteral("read"),
					QStringLiteral("touch"), QStringLiteral("latch"), QStringLiteral("write")}}}},
		}, {QStringLiteral("track"), QStringLiteral("parameter"), QStringLiteral("mode")});
		cmd.resultSchema = control::objectSchema({
			{QStringLiteral("track"), control::stringProperty()},
			{QStringLiteral("parameter"), control::stringProperty()},
			{QStringLiteral("mode"), control::stringProperty()},
			{QStringLiteral("mode_before"), control::stringProperty()},
			{QStringLiteral("changed"), control::booleanProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return automationModeSet(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("automation.record_mode_set");
		cmd.group = QStringLiteral("automation");
		cmd.verb = QStringLiteral("record_mode_set");
		cmd.description = QStringLiteral("Set a parameter's automation clip record flag: 'on' to "
			"write the control's manual value into the clip at every tick the transport runs, "
			"'off' to stop. The clip is a JournallingObject, so the inverse is a live checkpoint.");
		cmd.argsSchema = control::objectSchema({
			{QStringLiteral("track"), control::stringProperty()},
			{QStringLiteral("parameter"), control::stringProperty()},
			{QStringLiteral("mode"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
				{QStringLiteral("enum"), QJsonArray{QStringLiteral("on"), QStringLiteral("off")}}}},
		}, {QStringLiteral("track"), QStringLiteral("parameter"), QStringLiteral("mode")});
		cmd.resultSchema = control::objectSchema({
			{QStringLiteral("track"), control::stringProperty()},
			{QStringLiteral("parameter"), control::stringProperty()},
			{QStringLiteral("mode"), control::stringProperty()},
			{QStringLiteral("mode_before"), control::stringProperty()},
			{QStringLiteral("changed"), control::booleanProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return automationRecordModeSet(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
