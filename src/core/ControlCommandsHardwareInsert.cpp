/*
 * ControlCommandsHardwareInsert.cpp - mixer.hardware_insert / hardware_insert_ping: outboard gear in a channel
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

/*! 0.5.0 F1 (MX-061): a hardware insert in a channel (HardwareInsert.h) - after the channel's devices,
 *  before its fader: send to an output pair, return from an input pair, dry/wet, and the round trip's
 *  latency, measured by a ping or typed, reported to delay compensation. mixer.hardware_insert sets it up;
 *  mixer.hardware_insert_ping measures it (the result arrives a few periods later: read it with
 *  mixer.hardware_get). Saved with the project; one undo step each. */

#include <memory>

#include "AudioEngine.h"
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

void applyInsert(const QString& id, const HardwareInsert::Settings& settings)
{
	MixerChannel* channel = channelById(id);
	if (channel == nullptr) { return; }
	const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
	underChangeLock([channel, &settings, frames]() { channel->m_hardwareIo.insert.setSettings(settings, frames); });
}

bool readInsertFields(const QJsonObject& args, HardwareInsert::Settings* s, ControlResult* error)
{
	const double mix = args.value(QStringLiteral("mix")).toDouble(s->mix);
	const int latency = args.value(QStringLiteral("latency_frames")).toInt(s->latencyOverride);
	if (mix < 0.0 || mix > 1.0 || latency > LatencyCompensation::MaxFrames)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'mix' is 0 (dry) .. 1 (wet); 'latency_frames' is "
			"at most %1 (negative: use the measured figure)").arg(LatencyCompensation::MaxFrames));
		return false;
	}
	if (args.contains(QStringLiteral("send")) && !readDestination(args, QStringLiteral("send"), false, &s->send, error)) { return false; }
	if (args.contains(QStringLiteral("return")) && !readDestination(args, QStringLiteral("return"), true, &s->ret, error)) { return false; }
	s->mix = static_cast<float>(mix);
	s->latencyOverride = latency < 0 ? -1 : latency;
	s->enabled = args.value(QStringLiteral("enabled")).toBool(s->enabled);
	return true;
}

QJsonObject insertReply(MixerChannel& channel)
{
	QJsonObject reply = insertJson(channel.m_hardwareIo.insert, currentSetup(), deviceChannels());
	reply.insert(QStringLiteral("channel"), channelIdOf(&channel));
	return reply;
}

QJsonObject settingsJson(const QString& id, const HardwareInsert::Settings& s)
{
	return {{QStringLiteral("channel"), id}, {QStringLiteral("enabled"), s.enabled}, {QStringLiteral("send"), audioio::channelLabel(s.send)},
		{QStringLiteral("return"), audioio::channelLabel(s.ret)}, {QStringLiteral("mix"), static_cast<double>(s.mix)},
		{QStringLiteral("latency_frames"), s.latencyOverride}};
}

ControlResult hardwareInsert(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	const HardwareInsert::Settings before = channel->m_hardwareIo.insert.settings();
	HardwareInsert::Settings after = before;
	if (!readInsertFields(args, &after, &error)) { return error; }
	if (after == before) { return ControlResult::success(insertReply(*channel)); }
	const QString id = channelIdOf(channel);
	applyInsert(id, after);
	addUndoStep([id, before]() { applyInsert(id, before); }, [id, after]() { applyInsert(id, after); });
	Engine::getSong()->setModified();
	QJsonObject result = insertReply(*channel);
	result.insert(QStringLiteral("__transaction"), transactionPayload(settingsJson(id, before), QStringLiteral("mixer.hardware_insert"),
		settingsJson(id, before), true, QStringLiteral("action checkpoint: the recorded undo step puts the channel's previous hardware "
			"insert settings back")));
	return ControlResult::success(result);
}

ControlResult hardwareInsertPing(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	HardwareInsert& insert = channel->m_hardwareIo.insert;
	if (!insert.enabled())
	{
		return ControlResult::failure(ControlErrorKind::Requires, QStringLiteral("the channel's hardware insert is off: switch it on "
			"with mixer.hardware_insert first"));
	}
	const QString id = channelIdOf(channel);
	const HardwareInsert::Settings before = insert.settings();
	// The figure the ping will find lands later, on the audio thread: an undo keeps what it found then, for
	// its redo to put back.
	auto found = std::make_shared<HardwareInsert::Settings>(before);
	insert.requestPing();
	addUndoStep(
		[id, before, found]() {
			if (MixerChannel* undone = channelById(id)) { *found = undone->m_hardwareIo.insert.settings(); }
			applyInsert(id, before);
		},
		[id, found]() { applyInsert(id, *found); });
	Engine::getSong()->setModified();
	QJsonObject result = insertReply(*channel);
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("channel"), id}, {QStringLiteral("measured"), before.measured}},
		QStringLiteral("control.undo"), {}, true, QStringLiteral("action checkpoint: the recorded undo step puts the previous measured "
			"latency back")));
	return ControlResult::success(result);
}

} // namespace

void registerHardwareInsertCommands(ControlRegistry& registry)
{
	defineCommand(registry, QStringLiteral("mixer.hardware_insert"), QStringLiteral("A channel's hardware insert - outboard gear "
		"after the channel's devices and before its fader: 'enabled', the 'send' output pair ('3/4', '3' or a bus name), the "
		"'return' input pair ('1/2', ...), 'mix' 0 (dry) .. 1 (wet, the default) and 'latency_frames', the round trip typed in "
		"(negative: the figure mixer.hardware_insert_ping measured). The latency is reported to delay compensation and the dry "
		"signal is delayed by it. A device that cannot carry the send or return says so in its status. An export runs no "
		"device, so it renders the dry signal. What is not given is kept. Saved with the project; one undo step."),
		{{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("enabled"), booleanProperty()}, {QStringLiteral("send"), stringProperty()},
			{QStringLiteral("return"), stringProperty()}, {QStringLiteral("mix"), numberProperty()},
			{QStringLiteral("latency_frames"), integerProperty()}},
		{QStringLiteral("channel")}, true, hardwareInsert);
	defineCommand(registry, QStringLiteral("mixer.hardware_insert_ping"), QStringLiteral("Measure a hardware insert's round trip: "
		"one click goes out of its send, and the frames until it comes back on its return are its latency (the music is kept off "
		"the send and the dry signal plays while it measures). Answers at once with ping 'measuring'; mixer.hardware_get reports "
		"'done' with 'measured', or 'failed' when nothing came back within two seconds. The insert must be on. One undo step "
		"(the previous figure)."),
		{{QStringLiteral("channel"), stringProperty()}}, {QStringLiteral("channel")}, true, hardwareInsertPing);
}

} // namespace lmms
