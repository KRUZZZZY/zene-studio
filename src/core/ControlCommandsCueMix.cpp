/*
 * ControlCommandsCueMix.cpp - mixer.cue_send / cue_master: cue and headphone mixes
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

/*! 0.5.0 F1 (MX-036): cue (headphone) mixes, A to D. Each channel has a send to each cue - level, pan and
 *  pre- or post-fader (pre by default, so a performer's mix does not move with the engineer's faders);
 *  each cue mix sums its sends, scales them by its level and plays on the output pair it is routed to.
 *  Solo mutes the other channels, so a cue follows it; while a channel listens (AFL/PFL), a cue that
 *  follows the listen carries the listened signal instead of its sends. listen_to "cue" is Ableton's
 *  Solo/Cue switch: listening then reaches only those cues and the main output keeps the mix. Saved with
 *  the project; one undo step each. */

#include <array>

#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlHardwareIoSupport.h"
#include "ControlMixerSupport.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "HardwareIo.h"
#include "Mixer.h"
#include "Song.h"

namespace lmms
{

using namespace control;
using namespace control::hardware;

namespace
{

QString cueLetter(int cue)
{
	return QString(QChar('A' + cue));
}

void applyCueSend(const QString& id, int cue, hardwareio::CueSend send)
{
	MixerChannel* channel = channelById(id);
	if (channel == nullptr) { return; }
	underChangeLock([channel, cue, send]() { channel->m_hardwareIo.cues[static_cast<std::size_t>(cue)] = send; });
}

bool readSendFields(const QJsonObject& args, hardwareio::CueSend* send, ControlResult* error)
{
	const double level = args.value(QStringLiteral("level")).toDouble(send->level);
	const double pan = args.value(QStringLiteral("pan")).toDouble(send->pan);
	if (level < 0.0 || level > 2.0 || pan < -1.0 || pan > 1.0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'level' is 0..2 (1 is unity, 0 no send), 'pan' -1..1"));
		return false;
	}
	send->level = static_cast<float>(level);
	send->pan = static_cast<float>(pan);
	send->preFader = args.value(QStringLiteral("pre_fader")).toBool(send->preFader);
	return true;
}

QJsonObject sendJson(const QString& id, int cue, const hardwareio::CueSend& send)
{
	return {{QStringLiteral("channel"), id}, {QStringLiteral("cue"), cueLetter(cue)}, {QStringLiteral("name"), hardwareio::cueName(cue)},
		{QStringLiteral("level"), static_cast<double>(send.level)}, {QStringLiteral("pan"), static_cast<double>(send.pan)},
		{QStringLiteral("pre_fader"), send.preFader}};
}

ControlResult cueSend(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	int cue = 0;
	if (channel == nullptr || !readCue(args, &cue, &error)) { return error; }
	const hardwareio::CueSend before = channel->m_hardwareIo.cues[static_cast<std::size_t>(cue)];
	hardwareio::CueSend after = before;
	if (!readSendFields(args, &after, &error)) { return error; }
	const QString id = channelIdOf(channel);
	QJsonObject result = sendJson(id, cue, after);
	if (after == before) { return ControlResult::success(result); }
	applyCueSend(id, cue, after);
	addUndoStep([id, cue, before]() { applyCueSend(id, cue, before); }, [id, cue, after]() { applyCueSend(id, cue, after); });
	Engine::getSong()->setModified();
	result.insert(QStringLiteral("__transaction"), transactionPayload(sendJson(id, cue, before), QStringLiteral("mixer.cue_send"),
		{{QStringLiteral("channel"), id}, {QStringLiteral("cue"), cueLetter(cue)}, {QStringLiteral("level"), static_cast<double>(before.level)},
			{QStringLiteral("pan"), static_cast<double>(before.pan)}, {QStringLiteral("pre_fader"), before.preFader}},
		true, QStringLiteral("action checkpoint: the recorded undo step puts the channel's previous cue send back")));
	return ControlResult::success(result);
}

//! Every cue master and the listen target: what one cue_master call can change, and its undo puts back.
struct CueState
{
	std::array<hardwareio::CueMaster, hardwareio::MaxCues> masters;
	bool listenToCue = false;
	bool operator==(const CueState&) const = default;
};

CueState cueState()
{
	CueState state;
	for (int cue = 0; cue < hardwareio::MaxCues; ++cue) { state.masters[static_cast<std::size_t>(cue)] = hardwareio::cueMaster(cue); }
	state.listenToCue = hardwareio::listenToCue();
	return state;
}

void applyCueState(const CueState& state)
{
	underChangeLock([&state]() {
		for (int cue = 0; cue < hardwareio::MaxCues; ++cue) { hardwareio::setCueMaster(cue, state.masters[static_cast<std::size_t>(cue)]); }
		hardwareio::setListenToCue(state.listenToCue);
	});
}

bool readMasterFields(const QJsonObject& args, hardwareio::CueMaster* master, ControlResult* error)
{
	const double level = args.value(QStringLiteral("level")).toDouble(master->level);
	if (level < 0.0 || level > 2.0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'level' is 0..2 (1 is unity)"));
		return false;
	}
	if (args.contains(QStringLiteral("output")) && !readDestination(args, QStringLiteral("output"), false, &master->output, error)) { return false; }
	master->level = static_cast<float>(level);
	master->name = args.value(QStringLiteral("name")).toString(master->name).trimmed();
	master->followListen = args.value(QStringLiteral("follow_listen")).toBool(master->followListen);
	return true;
}

bool hasCueMasterFields(const QJsonObject& args)
{
	return args.contains(QStringLiteral("name")) || args.contains(QStringLiteral("level")) || args.contains(QStringLiteral("output"))
		|| args.contains(QStringLiteral("follow_listen"));
}

bool readCueState(const QJsonObject& args, CueState* state, ControlResult* error)
{
	const QString listenTo = args.value(QStringLiteral("listen_to")).toString();
	if (!listenTo.isEmpty() && listenTo != QStringLiteral("main") && listenTo != QStringLiteral("cue"))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'listen_to' is main or cue"));
		return false;
	}
	if (!listenTo.isEmpty()) { state->listenToCue = listenTo == QStringLiteral("cue"); }
	if (!args.contains(QStringLiteral("cue")))
	{
		if (hasCueMasterFields(args))
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'cue' is required when changing a cue master"));
			return false;
		}
		return true;
	}
	int cue = 0;
	return readCue(args, &cue, error) && readMasterFields(args, &state->masters[static_cast<std::size_t>(cue)], error);
}

QJsonObject cueStateJson(const CueState& state)
{
	return {{QStringLiteral("cues"), cueMastersJson(state.masters, currentSetup(), deviceChannels())},
		{QStringLiteral("listen_to"), state.listenToCue ? QStringLiteral("cue") : QStringLiteral("main")},
		{QStringLiteral("main_spared"), hardwareio::listenSparesMain()}};
}

ControlResult cueMaster(const QJsonObject& args)
{
	const CueState before = cueState();
	CueState after = before;
	ControlResult error;
	if (!readCueState(args, &after, &error)) { return error; }
	if (after == before) { return ControlResult::success(cueStateJson(before)); }
	applyCueState(after);
	addUndoStep([before]() { applyCueState(before); }, [after]() { applyCueState(after); });
	Engine::getSong()->setModified();
	QJsonObject result = cueStateJson(after);
	result.insert(QStringLiteral("__transaction"), transactionPayload(cueStateJson(before), QStringLiteral("control.undo"), {}, true,
		QStringLiteral("action checkpoint: the recorded undo step puts every cue master and the listen target back")));
	return ControlResult::success(result);
}

} // namespace

void registerCueMixCommands(ControlRegistry& registry)
{
	defineCommand(registry, QStringLiteral("mixer.cue_send"), QStringLiteral("A channel's send to a cue (headphone) mix, 'cue' A..D: "
		"'level' 0..2 (1 unity, 0 no send), 'pan' -1..1 (a balance on top of the channel's pan) and 'pre_fader' (true by default: "
		"the cue does not follow the channel's fader). What is not given is kept. The master's send is its main mix after the "
		"fader. A muted channel sends nothing, so a cue follows solo. Saved with the project; one undo step."),
		{{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("cue"), stringProperty()}, {QStringLiteral("level"), numberProperty()},
			{QStringLiteral("pan"), numberProperty()}, {QStringLiteral("pre_fader"), booleanProperty()}},
		{QStringLiteral("channel"), QStringLiteral("cue")}, true, cueSend);
	defineCommand(registry, QStringLiteral("mixer.cue_master"), QStringLiteral("A cue mix's master, 'cue' A..D: its 'name', 'level' "
		"0..2, the 'output' it plays on ('none', '3', '3/4' or a bus name of audio.io_list) and 'follow_listen' (true by default: "
		"while a channel listens, AFL or PFL, the cue carries the listened signal instead of its sends). 'listen_to' main (the "
		"default) or cue - Ableton's Solo/Cue switch: with cue, listening reaches only the cue mixes that follow it and the main "
		"output keeps the mix (it falls back to main while no such cue is routed). Without changes it reports. Saved with the "
		"project; one undo step."),
		{{QStringLiteral("cue"), stringProperty()}, {QStringLiteral("name"), stringProperty()}, {QStringLiteral("level"), numberProperty()},
			{QStringLiteral("output"), stringProperty()}, {QStringLiteral("follow_listen"), booleanProperty()},
			{QStringLiteral("listen_to"), enumProperty({QStringLiteral("main"), QStringLiteral("cue")})}},
		{}, true, cueMaster);
}

} // namespace lmms
