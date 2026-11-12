/*
 * ControlDeviceAu.cpp - the Audio Unit (AUv2) half of the device catalogue behind
 *                       plugin.list and the dev-<n> ids (Q050-491).
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
 * The same shape as ControlDeviceClap.cpp: the discovery is not a second scanner but the AU hosts'
 * own listSubPluginKeys() (plugins/AuEffect/AuSubPluginFeatures.cpp), which reads the system
 * component registry and records every component in the scan cache under the quarantine rules of
 * AuComponent.h. The hosts are built on macOS only; on every other build no descriptor exists, so
 * the catalogue has no "au" entry and a hand-made one is refused, typed, by controlAuDeviceModule().
 */

#include "ControlDeviceSupport.h"

#include "AuComponent.h"
#include "Plugin.h"
#include "PluginFactory.h"
#include "lmmsconfig.h"

namespace lmms
{

namespace
{

const QString AuEffectHostName = QStringLiteral("aueffect");
const QString AuInstrumentHostName = QStringLiteral("auinstrument");

const Plugin::Descriptor* findDescriptor(const QString& name, Plugin::Type type)
{
	for (const Plugin::Descriptor* descriptor : getPluginFactory()->descriptors(type))
	{
		if (descriptor != nullptr && name == QLatin1String(descriptor->name)) { return descriptor; }
	}
	return nullptr;
}

void appendAuHostClass(const Plugin::Descriptor* descriptor, const QString& kind, QList<ControlDeviceEntry>* out)
{
	if (descriptor == nullptr || descriptor->subPluginFeatures == nullptr) { return; }
	Plugin::Descriptor::SubPluginFeatures::KeyList keys;
	descriptor->subPluginFeatures->listSubPluginKeys(descriptor, keys);
	for (const Plugin::Descriptor::SubPluginFeatures::Key& key : keys)
	{
		const QString component = key.attributes.value(QStringLiteral("component"));
		au::ComponentId id;
		if (!au::parseComponentKey(component, &id)) { continue; }
		ControlDeviceEntry entry;
		entry.name = key.attributes.value(QStringLiteral("name"), component);
		entry.displayName = entry.name;
		entry.format = QStringLiteral("au");
		entry.kind = kind;
		entry.component = component;
		// The registry listed it and the quarantine list did not hide it; whether it instantiates is
		// plugin.load's question, answered with a typed error (au::instantiationError) when it does not.
		entry.loadable = true;
		out->append(entry);
	}
}

} // namespace

bool controlAuHostingBuilt()
{
#ifdef LMMS_BUILD_APPLE
	return true;
#else
	return false;
#endif
}

void controlAuDeviceEntries(QList<ControlDeviceEntry>* out)
{
	if (out == nullptr || !controlAuHostingBuilt()) { return; }
	// Effects first, then instruments: the order of every other hosted format.
	appendAuHostClass(findDescriptor(AuEffectHostName, Plugin::Type::Effect), QStringLiteral("effect"), out);
	appendAuHostClass(findDescriptor(AuInstrumentHostName, Plugin::Type::Instrument), QStringLiteral("instrument"), out);
}

bool controlAuDeviceModule(const ControlDeviceEntry& entry, QString* pluginName,
	Plugin::Descriptor::SubPluginFeatures::Key* key, bool* useKey, ControlResult* error)
{
	if (!controlAuHostingBuilt())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("device '%1' is an Audio Unit and this build has no Audio Unit host: AU hosting is "
				"built on macOS only").arg(entry.name));
		return false;
	}
	au::ComponentId id;
	if (!au::parseComponentKey(entry.component, &id))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the au device '%1' names no component (got '%2', expected type:subtype:manufacturer)")
				.arg(entry.name, entry.component));
		return false;
	}
	const bool instrument = entry.kind == QLatin1String("instrument");
	const QString hostName = instrument ? AuInstrumentHostName : AuEffectHostName;
	const Plugin::Descriptor* descriptor = findDescriptor(hostName,
		instrument ? Plugin::Type::Instrument : Plugin::Type::Effect);
	if (descriptor == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("the Audio Unit host plugin '%1' is not in this build").arg(hostName));
		return false;
	}
	Plugin::Descriptor::SubPluginFeatures::Key::AttributeMap attributes;
	attributes.insert(QStringLiteral("component"), au::componentKey(id));
	attributes.insert(QStringLiteral("name"), entry.displayName.isEmpty() ? entry.name : entry.displayName);
	*pluginName = hostName;
	*key = Plugin::Descriptor::SubPluginFeatures::Key(descriptor,
		attributes.value(QStringLiteral("name")), attributes);
	*useKey = true;
	return true;
}

} // namespace lmms
