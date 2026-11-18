/*
 * ControlCommandsHardwareIo.cpp - audio.io_list / io_setup and mixer.hardware_output / hardware_get
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

/*! 0.5.0 F1 (MX-040, MX-041): the interface's channels and a channel's route to them.
 *  - audio.io_list: the running device's outputs and inputs - how many it opened against how many the
 *    setup asks for, every channel's name, the stereo and mono buses, and what is routed to each output.
 *    A backend that opens a stereo pair only says so (multichannel false).
 *  - audio.io_setup: names, buses and the output count of the running device's interface. Machine
 *    settings, kept per device in the config file (AudioIoSetup.h); one undo step puts the previous back.
 *    A new output count takes effect when the device is next opened.
 *  - mixer.hardware_output: a channel's direct out (the master's: where the main mix plays). Saved with
 *    the project, one undo step.
 *  - mixer.hardware_get: every channel's hardware side and the cue masters. Read only. */

#include <QJsonArray>

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

QJsonObject route(const QString& source, const audioio::Destination& to, const audioio::Setup& setup, const DeviceChannels& device)
{
	return {{QStringLiteral("source"), source}, {QStringLiteral("output"), destinationJson(to, setup, device)}};
}

void appendChannelRoutes(QJsonArray& routes, MixerChannel& channel, const audioio::Setup& setup, const DeviceChannels& device)
{
	const QString id = channelIdOf(&channel);
	const audioio::Destination out = channel.m_hardwareIo.effectiveOutput(channel.index() == 0);
	if (!out.none()) { routes.append(route(id, out, setup, device)); }
	const HardwareInsert::Settings insert = channel.m_hardwareIo.insert.settings();
	if (insert.enabled) { routes.append(route(id + QStringLiteral("/insert"), insert.send, setup, device)); }
}

//! What plays on the outputs: every channel's direct out and insert send, and every routed cue mix.
QJsonArray routesJson(const audioio::Setup& setup, const DeviceChannels& device)
{
	QJsonArray routes;
	Mixer* mixer = Engine::mixer();
	for (int i = 0; i < mixer->numChannels(); ++i) { appendChannelRoutes(routes, *mixer->mixerChannel(i), setup, device); }
	for (int cue = 0; cue < hardwareio::MaxCues; ++cue)
	{
		const hardwareio::CueMaster& master = hardwareio::cueMaster(cue);
		if (!master.output.none()) { routes.append(route(hardwareio::cueName(cue), master.output, setup, device)); }
	}
	return routes;
}

QJsonObject ioReport()
{
	const DeviceChannels device = deviceChannels();
	const audioio::Setup setup = currentSetup();
	QJsonObject outputs = audioio::sideJson(setup.outputs, device.outputs);
	outputs.insert(QStringLiteral("multichannel"), device.multichannel);
	QJsonObject inputs = audioio::sideJson(setup.inputs, device.inputs);
	inputs.insert(QStringLiteral("capture"), device.capture);
	QJsonObject out{{QStringLiteral("device"), device.key}, {QStringLiteral("backend"), device.backend},
		{QStringLiteral("outputs"), outputs}, {QStringLiteral("inputs"), inputs}, {QStringLiteral("routes"), routesJson(setup, device)}};
	if (!device.multichannel)
	{
		out.insert(QStringLiteral("note"), QStringLiteral("this backend opens a stereo pair only: anything routed beyond "
			"outputs 1/2 plays nowhere (JACK, ALSA and PortAudio open more)"));
	}
	return out;
}

ControlResult ioList(const QJsonObject&)
{
	return ControlResult::success(ioReport());
}

//! Read 'outputs' and 'inputs' on top of @a setup; the input count is the capture plan's, not this command's.
bool readSides(const QJsonObject& args, audioio::Setup* setup, ControlResult* error)
{
	QString message;
	const QJsonObject inputs = args.value(QStringLiteral("inputs")).toObject();
	if (inputs.contains(QStringLiteral("channels")))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("the input count is the capture plan's: "
			"set it with record.input_set"));
		return false;
	}
	if (!audioio::parseSide(args.value(QStringLiteral("outputs")).toObject(), &setup->outputs, &message)
		|| !audioio::parseSide(inputs, &setup->inputs, &message))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, message);
		return false;
	}
	return true;
}

ControlResult ioSetup(const QJsonObject& args)
{
	const QString key = audioio::currentDeviceKey();
	audioio::Setup setup = currentSetup();
	const audioio::Setup setupBefore = setup;
	const int countBefore = setup.outputs.channels;
	ControlResult error;
	if (!readSides(args, &setup, &error)) { return error; }
	if (setup == setupBefore)
	{
		QJsonObject result = ioReport();
		const bool countChanged = setup.outputs.channels != countBefore;
		const bool reopen = setup.outputs.channels != deviceChannels().outputs;
		result.insert(QStringLiteral("applied"), countChanged ? QStringLiteral("names_now_count_next_start") : QStringLiteral("now"));
		result.insert(QStringLiteral("restart_required"), reopen);
		return ControlResult::success(result);
	}
	const QJsonObject before = audioio::storedEntry(key);
	QString message;
	if (!audioio::writeSetup(key, setup, &message)) { return ControlResult::failure(ControlErrorKind::InvalidArgs, message); }
	const QJsonObject after = audioio::storedEntry(key);
	addUndoStep([key, before]() { audioio::restoreEntry(key, before); }, [key, after]() { audioio::restoreEntry(key, after); });
	QJsonObject result = ioReport();
	const bool countChanged = setup.outputs.channels != countBefore;
	const bool reopen = setup.outputs.channels != deviceChannels().outputs;
	result.insert(QStringLiteral("applied"), countChanged ? QStringLiteral("names_now_count_next_start") : QStringLiteral("now"));
	result.insert(QStringLiteral("restart_required"), reopen);
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("device"), key}, {QStringLiteral("entry"), before}},
		QStringLiteral("control.undo"), {}, true, QStringLiteral("action checkpoint: the recorded undo step puts the device's previous "
			"I/O setup back in the config file")));
	return ControlResult::success(result);
}

void applyOutput(const QString& id, bool set, audioio::Destination to)
{
	MixerChannel* channel = channelById(id);
	if (channel == nullptr) { return; }
	underChangeLock([channel, set, to]() {
		channel->m_hardwareIo.outputSet = set;
		channel->m_hardwareIo.output = to;
	});
}

QJsonObject outputJson(MixerChannel& channel)
{
	const audioio::Destination out = channel.m_hardwareIo.effectiveOutput(channel.index() == 0);
	QJsonObject json = destinationJson(out, currentSetup(), deviceChannels());
	json.insert(QStringLiteral("default"), !channel.m_hardwareIo.outputSet);
	return json;
}

ControlResult hardwareOutput(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	const QString text = args.value(QStringLiteral("output")).toString();
	const bool reset = text.compare(QStringLiteral("default"), Qt::CaseInsensitive) == 0;
	audioio::Destination to;
	if (!reset && !readDestination(args, QStringLiteral("output"), false, &to, &error)) { return error; }
	const QString id = channelIdOf(channel);
	const bool setBefore = channel->m_hardwareIo.outputSet;
	const audioio::Destination before = channel->m_hardwareIo.output;
	if (setBefore == !reset && before == to)
	{
		QJsonObject result{{QStringLiteral("channel"), id}, {QStringLiteral("output"), outputJson(*channel)}};
		return ControlResult::success(result);
	}
	applyOutput(id, !reset, to);
	addUndoStep([id, setBefore, before]() { applyOutput(id, setBefore, before); }, [id, reset, to]() { applyOutput(id, !reset, to); });
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("channel"), id}, {QStringLiteral("output"), outputJson(*channel)}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("channel"), id},
		{QStringLiteral("output"), setBefore ? audioio::channelLabel(before) : QStringLiteral("default")}}, QStringLiteral("mixer.hardware_output"),
		{{QStringLiteral("channel"), id}, {QStringLiteral("output"), setBefore ? audioio::channelLabel(before) : QStringLiteral("default")}},
		true, QStringLiteral("action checkpoint: the recorded undo step puts the channel's previous hardware output back")));
	return ControlResult::success(result);
}

QJsonArray cueSendsJson(const ChannelHardwareIo& io)
{
	QJsonArray sends;
	for (int cue = 0; cue < hardwareio::MaxCues; ++cue)
	{
		const hardwareio::CueSend& send = io.cues[static_cast<std::size_t>(cue)];
		if (send.level <= 0.f) { continue; }
		sends.append(QJsonObject{{QStringLiteral("cue"), QString(QChar('A' + cue))}, {QStringLiteral("level"), static_cast<double>(send.level)},
			{QStringLiteral("pan"), static_cast<double>(send.pan)}, {QStringLiteral("pre_fader"), send.preFader}});
	}
	return sends;
}

QJsonObject channelJson(MixerChannel& channel, const audioio::Setup& setup, const DeviceChannels& device)
{
	return {{QStringLiteral("channel"), channelIdOf(&channel)}, {QStringLiteral("name"), channel.m_name},
		{QStringLiteral("output"), outputJson(channel)}, {QStringLiteral("cue_sends"), cueSendsJson(channel.m_hardwareIo)},
		{QStringLiteral("insert"), insertJson(channel.m_hardwareIo.insert, setup, device)}};
}

std::array<hardwareio::CueMaster, hardwareio::MaxCues> cueMasters()
{
	std::array<hardwareio::CueMaster, hardwareio::MaxCues> masters;
	for (int cue = 0; cue < hardwareio::MaxCues; ++cue) { masters[static_cast<std::size_t>(cue)] = hardwareio::cueMaster(cue); }
	return masters;
}

ControlResult hardwareGet(const QJsonObject& args)
{
	const audioio::Setup setup = currentSetup();
	const DeviceChannels device = deviceChannels();
	QJsonArray channels;
	if (args.contains(QStringLiteral("channel")))
	{
		ControlResult error;
		MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
		if (channel == nullptr) { return error; }
		channels.append(channelJson(*channel, setup, device));
	}
	else
	{
		for (int i = 0; i < Engine::mixer()->numChannels(); ++i) { channels.append(channelJson(*Engine::mixer()->mixerChannel(i), setup, device)); }
	}
	return ControlResult::success({{QStringLiteral("channels"), channels}, {QStringLiteral("cues"), cueMastersJson(cueMasters(), setup, device)},
		{QStringLiteral("listen_to"), hardwareio::listenToCue() ? QStringLiteral("cue") : QStringLiteral("main")},
		{QStringLiteral("device_outputs"), device.outputs}});
}

QJsonObject sideSchema()
{
	return objectSchema({{QStringLiteral("channels"), integerProperty(1, audioio::MaxChannels)}, {QStringLiteral("names"), arrayProperty()},
		{QStringLiteral("buses"), arrayProperty()}});
}

} // namespace

void registerHardwareIoCommands(ControlRegistry& registry)
{
	defineCommand(registry, QStringLiteral("audio.io_list"), QStringLiteral("The audio interface's channels: how many outputs and inputs "
		"the running device opened ('granted') against what its I/O setup asks for ('requested'), every channel's name, the stereo and "
		"mono buses, and what is routed to each output (channels' hardware outputs, insert sends, cue mixes). 'multichannel' false: "
		"this backend opens a stereo pair only, and routes beyond outputs 1/2 play nowhere. Read only."), {}, {}, false, ioList);
	defineCommandWithSchema(registry, QStringLiteral("audio.io_setup"), QStringLiteral("Set up the running device's interface "
		"channels - machine settings, kept per device, not in the project. 'outputs' and 'inputs' each take 'names' (one per "
		"channel, from channel 1; an empty one is left alone) and 'buses' ([{name, channels: \"3/4\" or \"5\"}], replacing the "
		"buses; a bus is stereo or mono, buses may not overlap and their names must differ); 'outputs' also takes 'channels', how "
		"many outputs to open (1..128, 2 by default), which takes effect when the device is next opened (JACK and ALSA; "
		"PortAudio opens what its own setup names). The input count is the capture plan's (record.input_set). One undo step."),
		objectSchema({{QStringLiteral("outputs"), sideSchema()}, {QStringLiteral("inputs"), sideSchema()}}), true, ioSetup);
	defineCommand(registry, QStringLiteral("mixer.hardware_output"), QStringLiteral("Route a channel to the interface's outputs: "
		"'output' is 'none', an output ('3', mono), a pair ('3/4') or a bus name from audio.io_list; 'default' takes the route away. "
		"A channel's hardware output is a direct out after its fader, beside its sends (remove its master send for the hardware "
		"only). The master's is where the main mix plays: outputs 1/2 unless routed elsewhere or to 'none'. The reply's status "
		"says whether the device carries it (ok, beyond_device, stereo_backend). Saved with the project; one undo step."),
		{{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("output"), stringProperty()}},
		{QStringLiteral("channel"), QStringLiteral("output")}, true, hardwareOutput);
	defineCommand(registry, QStringLiteral("mixer.hardware_get"), QStringLiteral("Every channel's hardware side ('channel' for one): "
		"its hardware output, its cue sends and its hardware insert (send, return, mix, latency, ping state), with the cue masters "
		"and where listening goes ('listen_to' main or cue). Read only."), {{QStringLiteral("channel"), stringProperty()}}, {}, false,
		hardwareGet);
	registerCueMixCommands(registry);
	registerHardwareInsertCommands(registry);
	registerSurroundCommands(registry);  // 0.5.0 F7 (MX-060)
}

} // namespace lmms
