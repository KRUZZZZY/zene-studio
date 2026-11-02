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
 * VST2 plug-ins are hosted through Carla (plugins/CarlaVst2, module name
 * "carlavst2"). As for VST3 and CLAP, the discovery is the host module's own -
 * Plugin::Descriptor::SubPluginFeatures::listSubPluginKeys(), which runs Carla's
 * out-of-process discovery tools - so a VST2 instrument is in plugin.list and
 * the device picker exactly when the instrument browser offers it. Stage 1 of
 * Q050-481 hosts instruments only. The entries are appended after every other
 * format, so the dev-<n> ids a build already handed out do not move.
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

const Plugin::Descriptor* findVst2Host()
{
	for (const Plugin::Descriptor* descriptor : getPluginFactory()->descriptors(Plugin::Type::Instrument))
	{
		if (descriptor != nullptr && QLatin1String(Vst2InstrumentHostName) == QLatin1String(descriptor->name))
		{
			return descriptor;
		}
	}
	return nullptr;
}

} // namespace

void controlVst2DeviceEntries(QList<ControlDeviceEntry>* out)
{
	if (out == nullptr) { return; }
	const Plugin::Descriptor* descriptor = findVst2Host();
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
		entry.kind = QStringLiteral("instrument");
		entry.file = file;
		entry.vst2Id = key.attributes.value(QStringLiteral("uniqueid"));
		entry.vst2Binary = key.attributes.value(QStringLiteral("binary"));
		// Carla's discovery loaded it (out of process) and reported an instrument.
		entry.loadable = true;
		pluginvendors::remember(file, name, key.attributes.value(QStringLiteral("maker")));
		out->append(entry);
	}
}

bool controlVst2DeviceModule(const ControlDeviceEntry& entry, QString* pluginName,
	Plugin::Descriptor::SubPluginFeatures::Key* key, bool* useKey, ControlResult* error)
{
	const Plugin::Descriptor* descriptor = findVst2Host();
	if (descriptor == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("the VST2 host plugin '%1' is not in this build").arg(QLatin1String(Vst2InstrumentHostName)));
		return false;
	}
	if (entry.kind != QLatin1String("instrument"))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the vst2 device '%1' is a %2: this build hosts VST2 instruments only").arg(entry.name, entry.kind));
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
	*pluginName = QString::fromLatin1(Vst2InstrumentHostName);
	*key = Plugin::Descriptor::SubPluginFeatures::Key(descriptor,
		entry.displayName.isEmpty() ? entry.name : entry.displayName, attributes);
	*useKey = true;
	return true;
}

} // namespace lmms
