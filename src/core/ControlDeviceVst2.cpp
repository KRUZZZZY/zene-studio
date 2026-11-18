/*
 * ControlDeviceVst2.cpp - the VST2 half of the device catalogue behind
 *                         plugin.list and the dev-<n> ids (Q050-481.1).
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

/*
 * VST2 plug-ins are hosted through Carla: instruments by plugins/CarlaVst2
 * (module "carlavst2", Q050-481.1), effects by plugins/CarlaVst2Effect (module
 * "carlavst2effect", Q050-481.2). As for VST3 and CLAP, the discovery is the
 * host modules' own - Plugin::Descriptor::SubPluginFeatures::listSubPluginKeys(),
 * which runs Carla's out-of-process discovery tools - so a VST2 device is in
 * plugin.list and the device picker exactly when its host offers it. The
 * entries are appended after every other format, instruments first and then
 * effects, so the dev-<n> ids a build already handed out do not move.
 */

#include "ControlDeviceSupport.h"

#include <QFileInfo>
#include <QString>

#include "Plugin.h"
#include "PluginFactory.h"
#include "PluginVendors.h"

namespace lmms
{

namespace
{

constexpr auto Vst2InstrumentHostName = "carlavst2";
constexpr auto Vst2EffectHostName = "carlavst2effect";  // Q050-481.2

//! The host module of a VST2 device of @a kind ("instrument" or "effect"), or nullptr.
const char* vst2HostName(const QString& kind)
{
	if (kind == QLatin1String("instrument")) { return Vst2InstrumentHostName; }
	return kind == QLatin1String("effect") ? Vst2EffectHostName : nullptr;
}

const Plugin::Descriptor* findVst2Host(Plugin::Type type, const char* name)
{
	for (const Plugin::Descriptor* descriptor : getPluginFactory()->descriptors(type))
	{
		if (descriptor != nullptr && QLatin1String(name) == QLatin1String(descriptor->name)) { return descriptor; }
	}
	return nullptr;
}

//! The VST2 devices one host offers, as catalogue entries of @a kind.
void appendVst2Entries(Plugin::Type type, const char* hostName, const QString& kind, QList<ControlDeviceEntry>* out)
{
	const Plugin::Descriptor* descriptor = findVst2Host(type, hostName);
	if (descriptor == nullptr || descriptor->subPluginFeatures == nullptr) { return; }
	Plugin::Descriptor::SubPluginFeatures::KeyList keys;
	descriptor->subPluginFeatures->listSubPluginKeys(descriptor, keys);
	for (const Plugin::Descriptor::SubPluginFeatures::Key& key : keys)
	{
		const QString file = key.attributes.value(QStringLiteral("file"));
		const QString name = key.attributes.value(QStringLiteral("name"));
		if (file.isEmpty() || name.isEmpty()) { continue; }
		ControlDeviceEntry entry;
		entry.name = name;
		entry.displayName = name;
		entry.format = QStringLiteral("vst2");
		entry.kind = kind;
		entry.file = file;
		entry.vst2Id = key.attributes.value(QStringLiteral("uniqueid"));
		entry.vst2Binary = key.attributes.value(QStringLiteral("binary"));
		// Carla's discovery loaded it (out of process) and reported what it is.
		entry.loadable = true;
		pluginvendors::remember(file, name, key.attributes.value(QStringLiteral("maker")));
		out->append(entry);
	}
}

} // namespace

void controlVst2DeviceEntries(QList<ControlDeviceEntry>* out)
{
	if (out == nullptr) { return; }
	// Instruments first, as Q050-481.1 listed them, then the effects: no instrument's dev-<n> moves.
	appendVst2Entries(Plugin::Type::Instrument, Vst2InstrumentHostName, QStringLiteral("instrument"), out);
	appendVst2Entries(Plugin::Type::Effect, Vst2EffectHostName, QStringLiteral("effect"), out);
}

bool controlVst2DeviceModule(const ControlDeviceEntry& entry, QString* pluginName,
	Plugin::Descriptor::SubPluginFeatures::Key* key, bool* useKey, ControlResult* error)
{
	const char* hostName = vst2HostName(entry.kind);
	if (hostName == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the vst2 device '%1' is a %2: VST2 instruments and effects are hosted").arg(entry.name, entry.kind));
		return false;
	}
	const bool instrument = entry.kind == QLatin1String("instrument");
	const Plugin::Descriptor* descriptor = findVst2Host(instrument ? Plugin::Type::Instrument : Plugin::Type::Effect, hostName);
	if (descriptor == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("the VST2 host plugin '%1' is not in this build").arg(QLatin1String(hostName)));
		return false;
	}
	if (entry.file.isEmpty() || !QFileInfo::exists(entry.file))
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("the vst2 binary '%1' is no longer there").arg(entry.file));
		return false;
	}
	Plugin::Descriptor::SubPluginFeatures::Key::AttributeMap attributes;
	attributes.insert(QStringLiteral("file"), entry.file);
	attributes.insert(QStringLiteral("name"), entry.name);
	attributes.insert(QStringLiteral("uniqueid"), entry.vst2Id);
	attributes.insert(QStringLiteral("binary"), entry.vst2Binary);
	attributes.insert(QStringLiteral("maker"), pluginvendors::vendorOf(entry.file, entry.name));
	*pluginName = QString::fromLatin1(hostName);
	*key = Plugin::Descriptor::SubPluginFeatures::Key(descriptor,
		entry.displayName.isEmpty() ? entry.name : entry.displayName, attributes);
	*useKey = true;
	return true;
}

} // namespace lmms
