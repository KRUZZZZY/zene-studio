/*
 * ControlCommandsAutomationRamp.cpp - the automation.* group's sample-accuracy
 *                                     half (feature-list row 9 / board task
 *                                     646): automation.ramp_set and
 *                                     automation.ramp_get.
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
#include "AutomationRamp.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

using control::AutomationParameter;

/*! Resolve the target track + parameter, and the clip that drives it.
 *
 *  The clip has to EXIST for either command to mean anything: feature row 9 is
 *  a property of a curve (how the engine reads it), not of a parameter, so a
 *  parameter with no automation is a typed not_found rather than a write that
 *  changes nothing.
 */
bool resolveRampTarget(const QJsonObject& args, ControlTarget* target,
	AutomationParameter* parameter, AutomationClip** clip, ControlResult* error)
{
	if (!resolveControlTarget(args.value(QStringLiteral("track")).toString(), target, error))
	{
		return false;
	}
	if (!control::findAutomationParameter(*target,
		args.value(QStringLiteral("parameter")).toString(), parameter, error))
	{
		return false;
	}
	*clip = control::existingAutomationClip(parameter->model);
	if (*clip == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1 has no automation clip on %2, so there is no curve whose "
				"sample accuracy could be turned on or off. Write one with "
				"automation.add_point first.").arg(parameter->id(), target->id));
		return false;
	}
	return true;
}

//! The mode name the wire uses. "block" is what every project has always had.
QString modeName(bool sampleAccurate)
{
	return sampleAccurate ? QStringLiteral("sample") : QStringLiteral("block");
}

/*! One parameter's line: what the clip asked for, and what the audio thread
 *  actually did with it.
 *
 *  The ramp fields are the engine's own record - knot count, the frames the
 *  ramp spans, how many frames it moved over, and the knots its fixed capacity
 *  had to refuse - read back from the model the audio path reads its per-sample
 *  values from. `ramp_live` says whether a ramp is published to that parameter
 *  RIGHT NOW, which is the difference between "the clip asked for sample
 *  accuracy" and "the last rendered block used it".
 */
QJsonObject rampStateJson(Track* track, const AutomationParameter& parameter, AutomationClip* clip,
	const AutomationRamp* ramp)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("track"), control::trackIdOf(track));
	entry.insert(QStringLiteral("parameter"), parameter.id());
	entry.insert(QStringLiteral("clip"), clip->name());
	entry.insert(QStringLiteral("clip_type"), clip->nodeName());
	entry.insert(QStringLiteral("mode"), modeName(clip->sampleAccurate()));
	entry.insert(QStringLiteral("ramp_live"), ramp != nullptr);
	entry.insert(QStringLiteral("knots"), ramp != nullptr ? ramp->knotCount() : 0);
	entry.insert(QStringLiteral("frames"), ramp != nullptr ? static_cast<qint64>(ramp->frames()) : 0);
	entry.insert(QStringLiteral("moves_inside_block"),
		ramp != nullptr && ramp->sampleAccurate());
	entry.insert(QStringLiteral("refused_knots"),
		ramp != nullptr ? static_cast<qint64>(ramp->refusals()) : 0);
	entry.insert(QStringLiteral("automation"), control::automationJson(clip, parameter.model));
	return entry;
}

/*! automation.ramp_set - the opt-in, and the whole write side of feature row 9.
 *
 *  The clip is a JournallingObject, so the inverse is a live checkpoint - the
 *  same mechanism automation.add_point uses, and the same reason the checkpoint
 *  is taken BEFORE the flag moves. The flag is serialized only when it is ON
 *  and loadSettings RESETS it on absence, so the checkpoint's own XML (which
 *  omits it) does take the mode back, including on the very first call.
 */
ControlResult automationRampSet(const QJsonObject& args)
{
	ControlTarget target;
	AutomationParameter parameter;
	AutomationClip* clip = nullptr;
	ControlResult error;
	if (!resolveRampTarget(args, &target, &parameter, &clip, &error)) { return error; }

	const QString mode = args.value(QStringLiteral("mode")).toString();
	if (mode != QStringLiteral("sample") && mode != QStringLiteral("block"))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("mode must be 'sample' or 'block', not '%1'").arg(mode));
	}
	if (clip->isInPattern())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 lives in a pattern clip: a pattern's automation is re-read "
				"against the pattern's own tick grid and has no single block timeline to "
				"ramp over, so its sample accuracy cannot be turned on "
				"(docs/KNOWN-LIMITATIONS.md).").arg(parameter.id()));
	}
	if (!clip->hasAutomation())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1's clip holds no points. Sample accuracy is a property of a "
				"curve and an empty clip drives nothing either way: write the curve with "
				"automation.add_point first.").arg(parameter.id()));
	}

	const bool requested = mode == QStringLiteral("sample");
	const bool before = clip->sampleAccurate();
	const bool changed = before != requested;
	if (changed)
	{
		clip->addJournalCheckPoint();
		clip->setSampleAccurate(requested);
	}

	const QJsonObject beforeState{
		{QStringLiteral("track"), target.id},
		{QStringLiteral("parameter"), parameter.id()},
		{QStringLiteral("mode"), modeName(before)},
	};
	QJsonObject result;
	result.insert(QStringLiteral("track"), target.id);
	result.insert(QStringLiteral("parameter"), parameter.id());
	result.insert(QStringLiteral("mode"), modeName(requested));
	result.insert(QStringLiteral("mode_before"), modeName(before));
	result.insert(QStringLiteral("changed"), changed);
	result.insert(QStringLiteral("automation"), control::automationJson(clip, parameter.model));
	result.insert(QStringLiteral("__transaction"), control::transactionPayload(beforeState,
		QStringLiteral("automation.ramp_set"),
		QJsonObject{{QStringLiteral("track"), target.id},
			{QStringLiteral("parameter"), parameter.id()},
			{QStringLiteral("mode"), modeName(before)}},
		true,
		QStringLiteral("live checkpoint: the clip is a JournallingObject, so control.undo "
			"replays the checkpoint taken before the flag moved and the clip's own "
			"loadSettings resets the flag on absence, which is what takes the FIRST "
			"ramp_set back too")));
	return ControlResult::success(result);
}

/*! automation.ramp_get - the read half: for every automation clip in the song,
 *  the mode it asked for and the ramp the audio thread published for it.
 *
 *  Reports what the ENGINE did rather than what the project asked for, because
 *  those are the two things an agent has to be able to tell apart: a clip in
 *  "sample" mode whose `ramp_live` is false after a render is a bug report, and
 *  a `refused_knots` above zero is the fixed-capacity fallback the row's
 *  limitation line names.
 */
ControlResult automationRampGet(const QJsonObject& args)
{
	const QString trackFilter = args.value(QStringLiteral("track")).toString();
	const bool includeBlockMode = args.value(QStringLiteral("include_block_mode")).toBool(true);

	QJsonArray entries;
	int sampleAccurateCount = 0;
	int liveRampCount = 0;
	for (Track* track : Engine::getSong()->tracks())
	{
		const auto type = track->type();
		if (type != Track::Type::Automation && type != Track::Type::HiddenAutomation) { continue; }
		if (!trackFilter.isEmpty() && trackFilter != control::trackIdOf(track)) { continue; }
		ControlTarget target;
		ControlResult error;
		if (!resolveControlTarget(control::trackIdOf(track), &target, &error)) { continue; }

		for (const AutomationParameter& parameter : control::automationParameters(target))
		{
			AutomationClip* clip = control::existingAutomationClip(parameter.model);
			if (clip == nullptr) { continue; }
			if (clip->sampleAccurate()) { ++sampleAccurateCount; }
			else if (!includeBlockMode) { continue; }

			const AutomationRamp* ramp = parameter.model->automationRamp();
			if (ramp != nullptr) { ++liveRampCount; }
			entries.append(rampStateJson(track, parameter, clip, ramp));
		}
	}

	QJsonObject result;
	result.insert(QStringLiteral("parameters"), entries);
	result.insert(QStringLiteral("count"), entries.size());
	result.insert(QStringLiteral("sample_accurate_count"), sampleAccurateCount);
	result.insert(QStringLiteral("live_ramp_count"), liveRampCount);
	result.insert(QStringLiteral("ramp_capacity"), AutomationRamp::MaxKnots);
	return ControlResult::success(result);
}

} // namespace

void registerAutomationRampCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("automation.ramp_set");
		cmd.group = QStringLiteral("automation");
		cmd.verb = QStringLiteral("ramp_set");
		cmd.description = QStringLiteral("Render one automation clip at SAMPLE precision "
			"inside each audio block ('sample'), or leave it block-quantised ('block', the "
			"behaviour every project has always had). In 'sample' mode the parameter's "
			"per-sample buffer - what the mixer, the fx chain and the tracks multiply their "
			"samples with - carries the curve at every frame instead of one value smeared "
			"over the whole block. Reversible through the ProjectJournal.");
		cmd.argsSchema = control::objectSchema({
			{QStringLiteral("track"), control::stringProperty()},
			{QStringLiteral("parameter"), control::stringProperty()},
			{QStringLiteral("mode"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
				{QStringLiteral("enum"), QJsonArray{QStringLiteral("sample"), QStringLiteral("block")}}}},
		}, {QStringLiteral("track"), QStringLiteral("parameter"), QStringLiteral("mode")});
		cmd.resultSchema = control::objectSchema({
			{QStringLiteral("track"), control::stringProperty()},
			{QStringLiteral("parameter"), control::stringProperty()},
			{QStringLiteral("mode"), control::stringProperty()},
			{QStringLiteral("mode_before"), control::stringProperty()},
			{QStringLiteral("changed"), control::booleanProperty()},
			{QStringLiteral("automation"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return automationRampSet(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("automation.ramp_get");
		cmd.group = QStringLiteral("automation");
		cmd.verb = QStringLiteral("ramp_get");
		cmd.description = QStringLiteral("Per automated parameter: the sample-accuracy mode its "
			"clip asked for, and the ramp the audio thread built for it in the last rendered "
			"block - knots, frames, whether the value MOVES inside the block, and how many "
			"knots the fixed capacity had to refuse. 'include_block_mode' false trims it to "
			"the parameters asking for sample accuracy.");
		cmd.argsSchema = control::objectSchema({
			{QStringLiteral("track"), control::stringProperty()},
			{QStringLiteral("include_block_mode"), control::booleanProperty()},
		});
		cmd.resultSchema = control::objectSchema({
			{QStringLiteral("parameters"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
			{QStringLiteral("count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("sample_accurate_count"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("live_ramp_count"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("ramp_capacity"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		});
		cmd.handler = [](const QJsonObject& args) { return automationRampGet(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
