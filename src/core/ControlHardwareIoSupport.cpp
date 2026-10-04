/*
 * ControlHardwareIoSupport.cpp - what the hardware I/O commands share: the device's channels and routing replies
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

#include "ControlHardwareIoSupport.h"

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AudioInputPath.h"
#include "ControlMixerSupport.h"
#include "Engine.h"
#include "HardwareIo.h"

namespace lmms::control::hardware
{

namespace
{

QString outputStatus(const audioio::Destination& destination, const DeviceChannels& device)
{
	if (destination.none()) { return QStringLiteral("none"); }
	if (destination.start + destination.width <= device.outputs) { return QStringLiteral("ok"); }
	return device.multichannel ? QStringLiteral("beyond_device") : QStringLiteral("stereo_backend");
}

QString inputStatus(const audioio::Destination& destination, const DeviceChannels& device)
{
	if (destination.none()) { return QStringLiteral("none"); }
	if (destination.start + destination.width > device.inputs) { return QStringLiteral("beyond_device"); }
	// Without an open capture device the return reads the engine's stereo input bus, which only a backend
	// that feeds it (the Dummy's loopback, say) fills.
	return device.capture ? QStringLiteral("ok") : QStringLiteral("stereo_bus");
}

} // namespace

DeviceChannels deviceChannels()
{
	DeviceChannels out;
	out.key = audioio::currentDeviceKey();
	AudioEngine* engine = Engine::audioEngine();
	AudioDevice* device = engine != nullptr ? engine->audioDev() : nullptr;
	if (device != nullptr)
	{
		out.backend = engine->audioDevName();
		out.outputs = device->outputChannels();
		out.multichannel = device->supportsMultichannelOutput();
	}
	const AudioInputPath::Live live = AudioInputPath::live();
	out.capture = live.open && live.channels > 0;
	out.inputs = out.capture ? live.channels : AudioInputPath::DefaultChannels;
	return out;
}

audioio::Setup currentSetup()
{
	return audioio::setupFor(audioio::currentDeviceKey(), AudioInputPath::configuredChannelCount());
}

QJsonObject destinationJson(const audioio::Destination& destination, const audioio::Setup& setup,
	const DeviceChannels& device, bool inputs)
{
	const audioio::Side& side = inputs ? setup.inputs : setup.outputs;
	const QString status = inputs ? inputStatus(destination, device) : outputStatus(destination, device);
	return {{QStringLiteral("label"), audioio::destinationLabel(destination, side)},
		{QStringLiteral("channels"), audioio::channelLabel(destination)}, {QStringLiteral("status"), status},
		{QStringLiteral("available"), status == QStringLiteral("ok") || status == QStringLiteral("stereo_bus")}};
}

QJsonObject insertJson(const HardwareInsert& insert, const audioio::Setup& setup, const DeviceChannels& device)
{
	static const char* const states[] = {"idle", "measuring", "measuring", "done", "failed"};
	const HardwareInsert::Settings s = insert.settings();
	return {{QStringLiteral("enabled"), s.enabled}, {QStringLiteral("send"), destinationJson(s.send, setup, device)},
		{QStringLiteral("return"), destinationJson(s.ret, setup, device, true)}, {QStringLiteral("mix"), static_cast<double>(s.mix)},
		{QStringLiteral("latency_override"), s.latencyOverride}, {QStringLiteral("measured"), s.measured},
		{QStringLiteral("latency"), insert.latencyFrames()}, {QStringLiteral("ping"), QLatin1String(states[static_cast<int>(insert.pingState())])}};
}

QJsonArray cueMastersJson(const std::array<hardwareio::CueMaster, hardwareio::MaxCues>& masters, const audioio::Setup& setup,
	const DeviceChannels& device)
{
	QJsonArray cues;
	for (int cue = 0; cue < hardwareio::MaxCues; ++cue)
	{
		const hardwareio::CueMaster& master = masters[static_cast<std::size_t>(cue)];
		cues.append(QJsonObject{{QStringLiteral("cue"), QString(QChar('A' + cue))},
			{QStringLiteral("name"), master.name.isEmpty() ? QStringLiteral("Cue %1").arg(QChar('A' + cue)) : master.name},
			{QStringLiteral("level"), static_cast<double>(master.level)}, {QStringLiteral("output"), destinationJson(master.output, setup, device)},
			{QStringLiteral("follow_listen"), master.followListen}});
	}
	return cues;
}

bool readDestination(const QJsonObject& args, const QString& key, bool inputs, audioio::Destination* out,
	ControlResult* error)
{
	const audioio::Setup setup = currentSetup();
	QString message;
	if (audioio::parseDestination(args.value(key).toString(), inputs ? setup.inputs : setup.outputs, out, &message)) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1': %2").arg(key, message));
	return false;
}

bool readCue(const QJsonObject& args, int* cue, ControlResult* error)
{
	const QJsonValue value = args.value(QStringLiteral("cue"));
	const QString text = value.isDouble() ? QString::number(value.toInt()) : value.toString().trimmed().toUpper();
	int index = -1;
	if (text.size() == 1 && text[0] >= QLatin1Char('A') && text[0] < QLatin1Char('A' + hardwareio::MaxCues)) { index = text[0].unicode() - 'A'; }
	else if (text.size() == 1 && text[0] >= QLatin1Char('1') && text[0] < QLatin1Char('1' + hardwareio::MaxCues)) { index = text[0].unicode() - '1'; }
	if (index < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'cue' is A..D (or 1..4)"));
		return false;
	}
	*cue = index;
	return true;
}

void underChangeLock(const std::function<void()>& change)
{
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	change();
}

MixerChannel* channelById(const QString& id)
{
	ControlResult ignored;
	return resolveMixerChannel(id, &ignored);
}

} // namespace lmms::control::hardware
