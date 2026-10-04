/*
 * AudioIoSetup.cpp - the interface's channels: their names, how they pair, and how many outputs to open
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

#include "AudioIoSetup.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "Engine.h"

namespace lmms::audioio
{

namespace
{

constexpr auto kSection = "audioio";
constexpr auto kKey = "setups";

//! The backends that open a named device, and the config key that names it.
struct DeviceSource
{
	const char* prefix;
	const char* cls;
	const char* attribute;
};

constexpr DeviceSource kDeviceSources[] = {
	{"ALSA", "audioalsa", "device"},
	{"PortAudio", "audioportaudio", "outputdevice"},
	{"PulseAudio", "audiopa", "device"},
	{"sndio", "audiosndio", "device"},
	{"OSS", "audiooss", "device"},
};

QJsonObject allEntries()
{
	return QJsonDocument::fromJson(ConfigManager::inst()->value(kSection, kKey).toUtf8()).object();
}

void storeAll(const QJsonObject& all)
{
	ConfigManager::inst()->setValue(kSection, kKey, QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact)));
	ConfigManager::inst()->saveConfigFile();
}

QString pairLabel(int start, int width)
{
	return width == 1 ? QString::number(start + 1) : QStringLiteral("%1/%2").arg(start + 1).arg(start + 2);
}

//! Names up to @a channels (missing ones "<prefix> N"), and the default buses when none are kept:
//! consecutive stereo pairs, a last odd channel mono. Buses outside the channels are dropped.
void fill(Side& side, int channels, const QString& prefix)
{
	side.channels = std::clamp(channels, 1, MaxChannels);
	while (side.names.size() < side.channels) { side.names.append(QStringLiteral("%1 %2").arg(prefix).arg(side.names.size() + 1)); }
	std::erase_if(side.buses, [&side](const Bus& bus) { return bus.start + bus.width > side.channels; });
	if (!side.buses.empty()) { return; }
	for (int start = 0; start < side.channels; start += 2)
	{
		const int width = std::min(2, side.channels - start);
		side.buses.push_back({QStringLiteral("%1 %2").arg(prefix, pairLabel(start, width)), start, width});
	}
}

QJsonObject storedSide(const Side& side)
{
	QJsonArray buses;
	for (const Bus& bus : side.buses)
	{
		buses.append(QJsonObject{{QStringLiteral("name"), bus.name}, {QStringLiteral("start"), bus.start}, {QStringLiteral("width"), bus.width}});
	}
	return {{QStringLiteral("channels"), side.channels}, {QStringLiteral("names"), QJsonArray::fromStringList(side.names)},
		{QStringLiteral("buses"), buses}};
}

Side sideFromStorage(const QJsonObject& json)
{
	Side side;
	side.channels = json.value(QStringLiteral("channels")).toInt(2);
	for (const QJsonValue& name : json.value(QStringLiteral("names")).toArray()) { side.names.append(name.toString()); }
	for (const QJsonValue& value : json.value(QStringLiteral("buses")).toArray())
	{
		const QJsonObject bus = value.toObject();
		side.buses.push_back({bus.value(QStringLiteral("name")).toString(), bus.value(QStringLiteral("start")).toInt(),
			bus.value(QStringLiteral("width")).toInt(2)});
	}
	return side;
}

bool fail(QString* error, const QString& message)
{
	if (error != nullptr) { *error = message; }
	return false;
}

bool validBus(const Bus& bus, const Side& side, std::vector<bool>& taken, QString* error)
{
	if (bus.name.trimmed().isEmpty()) { return fail(error, QStringLiteral("a bus needs a name")); }
	if (bus.width != 1 && bus.width != 2) { return fail(error, QStringLiteral("bus '%1': a bus is mono (1) or stereo (2)").arg(bus.name)); }
	if (bus.start < 0 || bus.start + bus.width > side.channels)
	{
		return fail(error, QStringLiteral("bus '%1' leaves the %2 channels").arg(bus.name).arg(side.channels));
	}
	for (int c = bus.start; c < bus.start + bus.width; ++c)
	{
		if (taken[static_cast<std::size_t>(c)]) { return fail(error, QStringLiteral("bus '%1' overlaps another on channel %2").arg(bus.name).arg(c + 1)); }
		taken[static_cast<std::size_t>(c)] = true;
	}
	return true;
}

bool validSide(const Side& side, QString* error)
{
	if (side.channels < 1 || side.channels > MaxChannels)
	{
		return fail(error, QStringLiteral("a side has 1..%1 channels").arg(MaxChannels));
	}
	std::vector<bool> taken(static_cast<std::size_t>(side.channels), false);
	QStringList seen;
	for (const Bus& bus : side.buses)
	{
		if (!validBus(bus, side, taken, error)) { return false; }
		if (seen.contains(bus.name, Qt::CaseInsensitive)) { return fail(error, QStringLiteral("two buses are called '%1'").arg(bus.name)); }
		seen.append(bus.name);
	}
	return true;
}

//! "3" or "3/4" (1-based) into @a out; false when @a text is not that shape.
bool parseChannels(const QString& text, Destination* out, QString* error, bool* matched)
{
	static const QRegularExpression shape(QStringLiteral("^(\\d+)(?:/(\\d+))?$"));
	const QRegularExpressionMatch match = shape.match(text);
	*matched = match.hasMatch();
	if (!*matched) { return false; }
	const int first = match.captured(1).toInt();
	const bool pair = !match.captured(2).isEmpty();
	if (pair && match.captured(2).toInt() != first + 1)
	{
		return fail(error, QStringLiteral("'%1' is not a pair: a stereo pair is two consecutive outputs, like 3/4").arg(text));
	}
	const int width = pair ? 2 : 1;
	if (first < 1 || first - 1 + width > MaxChannels)
	{
		return fail(error, QStringLiteral("outputs are numbered 1..%1").arg(MaxChannels));
	}
	*out = Destination{first - 1, width};
	return true;
}

bool parseBus(const QJsonObject& json, Bus* bus, QString* error)
{
	bus->name = json.value(QStringLiteral("name")).toString();
	Destination at;
	bool matched = false;
	const QString channels = json.value(QStringLiteral("channels")).toString();
	const bool parsed = parseChannels(channels, &at, error, &matched);
	if (!matched) { return fail(error, QStringLiteral("bus '%1': 'channels' is \"3\" or \"3/4\"").arg(bus->name)); }
	if (!parsed) { return false; }
	bus->start = at.start;
	bus->width = at.width;
	return true;
}

} // namespace

QString deviceKey(const QString& backend)
{
	for (const DeviceSource& source : kDeviceSources)
	{
		if (!backend.startsWith(QLatin1String(source.prefix))) { continue; }
		const QString device = ConfigManager::inst()->value(source.cls, source.attribute);
		return device.isEmpty() ? backend : backend + QStringLiteral(" | ") + device;
	}
	return backend;
}

QString currentDeviceKey()
{
	return Engine::audioEngine() != nullptr ? deviceKey(Engine::audioEngine()->audioDevName()) : QString();
}

Setup setupFor(const QString& key, int inputChannels)
{
	const QJsonObject entry = allEntries().value(key).toObject();
	Setup setup;
	setup.outputs = sideFromStorage(entry.value(QStringLiteral("outputs")).toObject());
	setup.inputs = sideFromStorage(entry.value(QStringLiteral("inputs")).toObject());
	fill(setup.outputs, setup.outputs.channels, QStringLiteral("Out"));
	fill(setup.inputs, inputChannels, QStringLiteral("In"));
	return setup;
}

int requestedOutputs(const QString& key)
{
	const QJsonObject outputs = allEntries().value(key).toObject().value(QStringLiteral("outputs")).toObject();
	return std::clamp(outputs.value(QStringLiteral("channels")).toInt(2), 1, MaxChannels);
}

bool writeSetup(const QString& key, const Setup& setup, QString* error)
{
	if (!validSide(setup.outputs, error) || !validSide(setup.inputs, error)) { return false; }
	QJsonObject all = allEntries();
	all.insert(key, QJsonObject{{QStringLiteral("outputs"), storedSide(setup.outputs)}, {QStringLiteral("inputs"), storedSide(setup.inputs)}});
	storeAll(all);
	return true;
}

QJsonObject storedEntry(const QString& key)
{
	return allEntries().value(key).toObject();
}

void restoreEntry(const QString& key, const QJsonObject& entry)
{
	QJsonObject all = allEntries();
	if (entry.isEmpty()) { all.remove(key); }
	else { all.insert(key, entry); }
	storeAll(all);
}

QJsonObject sideJson(const Side& side, int granted)
{
	QJsonArray channels;
	for (int c = 0; c < side.names.size(); ++c)
	{
		channels.append(QJsonObject{{QStringLiteral("number"), c + 1}, {QStringLiteral("name"), side.names[c]},
			{QStringLiteral("available"), c < granted}});
	}
	QJsonArray buses;
	for (const Bus& bus : side.buses)
	{
		buses.append(QJsonObject{{QStringLiteral("name"), bus.name}, {QStringLiteral("channels"), pairLabel(bus.start, bus.width)},
			{QStringLiteral("kind"), bus.width == 1 ? QStringLiteral("mono") : QStringLiteral("stereo")},
			{QStringLiteral("available"), bus.start + bus.width <= granted}});
	}
	return {{QStringLiteral("requested"), side.channels}, {QStringLiteral("granted"), granted},
		{QStringLiteral("channels"), channels}, {QStringLiteral("buses"), buses}};
}

bool parseSide(const QJsonObject& json, Side* side, QString* error)
{
	if (json.contains(QStringLiteral("channels")))
	{
		// A new count keeps the buses that still fit; with none left, the default pairs come back on reading.
		side->channels = json.value(QStringLiteral("channels")).toInt();
		std::erase_if(side->buses, [side](const Bus& bus) { return bus.start + bus.width > side->channels; });
	}
	if (json.contains(QStringLiteral("names")))
	{
		const QJsonArray names = json.value(QStringLiteral("names")).toArray();
		for (int c = 0; c < names.size() && c < MaxChannels; ++c)
		{
			while (side->names.size() <= c) { side->names.append(QString()); }
			if (!names[c].toString().trimmed().isEmpty()) { side->names[c] = names[c].toString().trimmed(); }
		}
	}
	if (!json.contains(QStringLiteral("buses"))) { return true; }
	side->buses.clear();
	for (const QJsonValue& value : json.value(QStringLiteral("buses")).toArray())
	{
		Bus bus;
		if (!parseBus(value.toObject(), &bus, error)) { return false; }
		side->buses.push_back(bus);
	}
	return true;
}

bool parseDestination(const QString& text, const Side& outputs, Destination* out, QString* error)
{
	const QString trimmed = text.trimmed();
	if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0)
	{
		*out = Destination{};
		return true;
	}
	bool matched = false;
	const bool parsed = parseChannels(trimmed, out, error, &matched);
	if (matched) { return parsed; }
	for (const Bus& bus : outputs.buses)
	{
		if (bus.name.compare(trimmed, Qt::CaseInsensitive) == 0)
		{
			*out = Destination{bus.start, bus.width};
			return true;
		}
	}
	return fail(error, QStringLiteral("'%1' names no output: use 'none', an output ('3'), a pair ('3/4') or a bus of the "
		"interface (audio.io_list)").arg(trimmed));
}

QString destinationLabel(const Destination& destination, const Side& outputs)
{
	for (const Bus& bus : outputs.buses)
	{
		if (bus.start == destination.start && bus.width == destination.width) { return bus.name; }
	}
	return channelLabel(destination);
}

QString channelLabel(const Destination& destination)
{
	return destination.none() ? QStringLiteral("none") : pairLabel(destination.start, destination.width);
}

} // namespace lmms::audioio
