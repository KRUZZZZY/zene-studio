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

/*! automation.mode_set - registered, and refused by name.
 *
 * The modes exist in the engine (AutomatableModel's Read/Touch/Latch/Write enum, with the
 * touch state machine and a test) but nothing can SELECT or PERSIST one: setAutomationMode
 * has no caller outside its own test, the mode is not serialised, and the interface does not
 * offer it - docs/KNOWN-LIMITATIONS.md. The engine has the modes; the product cannot
 * select or persist one, which is the limitation this refusal reports.
 * The command is registered with its full schema and asks for nothing it cannot read, the
 * shape mixer.set_pan and track.set_arm use.
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

	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("this build has automation modes in the engine (Read/Touch/Latch/Write, "
			"AutomatableModel) but no way to select or persist one: the mode is not saved with "
			"the project and neither the interface nor this surface can set it "
			"(docs/KNOWN-LIMITATIONS.md). Use automation.add_point to write a curve instead."));
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
			"stable '<plugin>/<index>' id and, for each automated one, its clip's points. A "
			"point's 'value' is the model's own unit (what plugin.param_get reports); "
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
		cmd.description = QStringLiteral("Set a parameter's automation mode. Refused: this build "
			"has automation modes in the engine but no way to select or persist one "
			"(docs/KNOWN-LIMITATIONS.md), so no write is faked.");
		cmd.argsSchema = control::objectSchema({
			{QStringLiteral("track"), control::stringProperty()},
			{QStringLiteral("parameter"), control::stringProperty()},
			{QStringLiteral("mode"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
				{QStringLiteral("enum"), QJsonArray{QStringLiteral("off"), QStringLiteral("read"),
					QStringLiteral("touch"), QStringLiteral("latch"), QStringLiteral("write")}}}},
		}, {QStringLiteral("track"), QStringLiteral("parameter"), QStringLiteral("mode")});
		cmd.resultSchema = control::objectSchema({});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return automationModeSet(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
