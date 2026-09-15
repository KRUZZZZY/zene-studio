/*
 * ControlCommandsRecordingInput.cpp - the INPUT PATH verbs of the `record.`
 *                                      group: what this instance was configured
 *                                      to capture, and what the device did with
 *                                      it (0.3.0, feature row 64 "Arbitrary input
 *                                      count / multiple simultaneous inputs").
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

/*
 * WHY THIS GROUP EXISTS, IN THE AUDIT'S OWN WORDS. FEATURE-LIST-0.3.0.md row 64
 * records the input side as "to build - the default Linux backend is
 * playback-only today (AudioAlsa has no capture path)", and the measurement
 * behind that sentence was that under ALSA AudioEngine::inputBufferFrames() was
 * ALWAYS 0, so every record route took zero inputs. The engine half of the fix
 * is include/AudioInputPath.h + include/AudioWideInputStage.h +
 * src/core/audio/AudioAlsa.cpp's capture path; this unit is the two verbs that
 * make it drivable and observable - the configured plan, the device's own
 * answer, and the counter that says whether frames are arriving.
 *
 * THE SIBLING UNIT is ControlCommandsRecordingRoutes.cpp: the route verbs
 * (record.get_state / arm_track / disarm_track / disarm_all) that drive the
 * multi-track recorder, row 14's gap. The split is the fork's file-length
 * ratchet and the seam is real - one half answers "what may this instance
 * capture", the other "what is it capturing" - and both report through the
 * helpers in ControlRecordingSupport.h.
 *
 * WHAT IS NOT VERIFIED HERE, said plainly: whether a real sound card opens, how
 * many channels it grants and whether it delivers frames. This box's answer is
 * reported by record.input_get_state (capture_capable / capture_open /
 * capture_reason / the granted channels and rate / the staged and dropped
 * counters) and never assumed; docs/KNOWN-LIMITATIONS.md carries the sentence.
 */

#include <algorithm>
#include <cstdint>

#include <QJsonObject>
#include <QString>

#include "AudioEngine.h"
#include "AudioInputPath.h"
#include "ControlEdit.h"
#include "ControlRecordingSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MultiTrackRecorder.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

// --------------------------------------------------------------------------
// record.input_get_state
// --------------------------------------------------------------------------
ControlResult inputGetState(const QJsonObject&)
{
	return ControlResult::success(inputPathJson());
}

// --------------------------------------------------------------------------
// record.input_set
// --------------------------------------------------------------------------
//! The plan the caller asked for: the current plan with every field the caller
//! named replaced. No validation here - that is AudioInputPath::validatePlan's
//! job, and it is the same code the config loader's readers go through.
AudioInputPath::Plan requestedPlan(const QJsonObject& args)
{
	AudioInputPath::Plan plan = AudioInputPath::configuredPlan();
	if (args.contains(QStringLiteral("device")))
	{
		plan.device = args.value(QStringLiteral("device")).toString();
	}
	if (args.contains(QStringLiteral("channels")))
	{
		plan.channels = args.value(QStringLiteral("channels")).toInt();
	}
	if (args.contains(QStringLiteral("left")))
	{
		plan.left = args.value(QStringLiteral("left")).toInt();
	}
	if (args.contains(QStringLiteral("right")))
	{
		plan.right = args.value(QStringLiteral("right")).toInt();
	}
	return plan;
}

QJsonObject planJson(const AudioInputPath::Plan& plan)
{
	QJsonObject out;
	out.insert(QStringLiteral("device"), plan.device);
	out.insert(QStringLiteral("channels"), plan.channels);
	out.insert(QStringLiteral("left"), plan.left);
	out.insert(QStringLiteral("right"), plan.right);
	return out;
}

ControlResult inputSet(const QJsonObject& args)
{
	const AudioInputPath::Plan before = AudioInputPath::configuredPlan();
	const AudioInputPath::Plan wanted = requestedPlan(args);

	QString error;
	if (!AudioInputPath::writePlan(wanted, &error))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, error);
	}

	const AudioInputPath::Plan written = AudioInputPath::configuredPlan();
	QJsonObject result = planJson(written);
	result.insert(QStringLiteral("route_capacity"), AudioInputPath::recordRouteCapacity());
	result.insert(QStringLiteral("previous"), planJson(before));
	// The device is opened when the backend starts, and the recorder's routes
	// are built when the engine is constructed, so a change here is a change for
	// the NEXT start. Said plainly rather than implied.
	result.insert(QStringLiteral("restart_required"), true);

	// SPEC A16: ConfigManager is not a JournallingObject, but the inverse is a
	// command - write the previous plan back - which is what the settings.set
	// precedent records for the same kind of write (the config file, not the
	// project).
	result.insert(QStringLiteral("__transaction"),
		commandTransaction(planJson(before), QStringLiteral("record.input_set"), planJson(before)));
	return ControlResult::success(result);
}

// --------------------------------------------------------------------------
// schemas
// --------------------------------------------------------------------------
QJsonObject inputStateSchema()
{
	return objectSchema({
		{QStringLiteral("configured"), objectSchema({})},
		{QStringLiteral("capture_capable"), booleanProperty()},
		{QStringLiteral("capture_open"), booleanProperty()},
		{QStringLiteral("capture_device"), stringProperty()},
		{QStringLiteral("capture_reason"), stringProperty()},
		{QStringLiteral("capture_channels"), integerProperty()},
		{QStringLiteral("capture_rate"), integerProperty()},
		{QStringLiteral("capture_left"), integerProperty()},
		{QStringLiteral("capture_right"), integerProperty()},
		{QStringLiteral("capture_format"), stringProperty()},
		{QStringLiteral("capture_frames"), integerProperty()},
		{QStringLiteral("capture_overruns"), integerProperty()},
		{QStringLiteral("recordable_channels"), integerProperty()},
		{QStringLiteral("max_channels"), integerProperty()},
		{QStringLiteral("route_capacity"), integerProperty()},
		{QStringLiteral("input_frames_staged"), integerProperty()},
		{QStringLiteral("input_frames_dropped"), integerProperty()},
		{QStringLiteral("bus_frames"), integerProperty()},
		{QStringLiteral("wide_frames"), integerProperty()},
		{QStringLiteral("wide_channels"), integerProperty()},
		{QStringLiteral("wide_frames_staged"), integerProperty()},
		{QStringLiteral("wide_frames_dropped"), integerProperty()},
	});
}

// --------------------------------------------------------------------------
// registration
// --------------------------------------------------------------------------
void registerInputGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.input_get_state");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("input_get_state");
	cmd.description = QStringLiteral("The engine's capture-IN path, asked directly: what the "
		"configuration asks for (device, channel COUNT, which pair of captured channels rides the "
		"stereo bus), what this backend actually did with it (capture_capable, capture_open, "
		"capture_reason, the channels and rate the device granted), and what the two input stages "
		"hold now (bus_frames on the stereo bus, wide_frames/wide_channels on the N-channel stage, "
		"plus the staged and dropped counters). 'capture_capable: false' means this instance's "
		"backend has no capture path at all; 'capture_capable: true, capture_open: false' means "
		"it has one and the device refused - capture_reason carries the device's own message. "
		"Read-only, and the honest place to look before blaming a silent take.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = inputStateSchema();
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) { return inputGetState(args); };
	registry.registerCommand(cmd);
}

void registerInputSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.input_set");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("input_set");
	cmd.description = QStringLiteral("Set the capture input path: 'device' (empty = the playback "
		"device), 'channels' (1..%1 - the ARBITRARY input count of feature row 64), and 'left'/"
		"'right' (which two captured channels the stereo engine bus carries, so an interface's "
		"third and fourth input can be what the rest of the engine hears). Fields not given keep "
		"their current value. The four keys are written to the config file under `audioinput`, "
		"exactly as the settings dialog writes its own, and 'restart_required' is true: the "
		"backend opens the device at start and the recorder's routes are built when the engine is, "
		"so the next start is when this takes effect - there is no live device swap to pretend "
		"about. Reversible: the recorded inverse is this command with the previous plan, and "
		"control.undo dispatches it (the settings.set precedent, which records a command inverse "
		"for the same kind of config write).").arg(AudioInputPath::MaxChannels);
	cmd.argsSchema = objectSchema({
		{QStringLiteral("device"), stringProperty()},
		{QStringLiteral("channels"), integerProperty(AudioInputPath::MinChannels,
			AudioInputPath::MaxChannels)},
		{QStringLiteral("left"), integerProperty(0, AudioInputPath::MaxChannels - 1)},
		{QStringLiteral("right"), integerProperty(0, AudioInputPath::MaxChannels - 1)},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("device"), stringProperty()},
		{QStringLiteral("channels"), integerProperty()},
		{QStringLiteral("left"), integerProperty()},
		{QStringLiteral("right"), integerProperty()},
		{QStringLiteral("route_capacity"), integerProperty()},
		{QStringLiteral("previous"), objectSchema({})},
		{QStringLiteral("restart_required"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return inputSet(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerRecordingInputCommands(ControlRegistry& registry)
{
	registerInputGetState(registry);
	registerInputSet(registry);
}

} // namespace lmms
