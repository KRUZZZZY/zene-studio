/*
 * CarlaVst2SubPluginFeatures.h - the VST2 plug-ins Carla's discovery found, as
 *                                sub-plugin keys (Q050-481.1, .2).
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

#ifndef LMMS_CARLA_VST2_SUB_PLUGIN_FEATURES_H
#define LMMS_CARLA_VST2_SUB_PLUGIN_FEATURES_H

#include "CarlaVst2Discovery.h"
#include "Plugin.h"
#include "PluginVendors.h"

namespace lmms::carlavst2
{

/*!
 * The sub-plugin keys of a VST2 host descriptor: Carla's discovery (shared by
 * both hosts, plugins/CarlaBase/CarlaVst2Discovery) filtered to what the host
 * runs - the synths for carlavst2 (Plugin::Type::Instrument), everything else
 * for carlavst2effect (Plugin::Type::Effect). The key's attributes are what
 * plugin.list publishes and a saved project stores.
 */
class SubPluginFeatures : public Plugin::Descriptor::SubPluginFeatures
{
public:
	explicit SubPluginFeatures(Plugin::Type type) : Plugin::Descriptor::SubPluginFeatures(type) {}

	void listSubPluginKeys(const Plugin::Descriptor* descriptor, KeyList& keys) const override
	{
		const bool synths = m_type == Plugin::Type::Instrument;
		for (const Vst2Record& record : discoverAll())
		{
			if (record.isSynth != synths) { continue; }
			Key::AttributeMap attributes;
			attributes.insert(fileAttribute(), record.file);
			attributes.insert(nameAttribute(), record.name);
			attributes.insert(uniqueIdAttribute(), QString::number(record.uniqueId));
			attributes.insert(binaryAttribute(), binaryName(record.binary));
			attributes.insert(makerAttribute(), record.maker);
			pluginvendors::remember(record.file, record.name, record.maker);
			keys.append(Key(descriptor, record.name, attributes));
		}
	}

protected:
	QString displayName(const Key& key) const override
	{
		return key.attributes.value(nameAttribute(), key.name);
	}

	QString description(const Key& key) const override
	{
		const bool bridged = binaryFromName(key.attributes.value(binaryAttribute())) != Binary::Native;
		if (m_type == Plugin::Type::Instrument)
		{
			return bridged ? QStringLiteral("VST2 instrument (Windows, through Carla's Wine bridge)")
				: QStringLiteral("VST2 instrument (through Carla)");
		}
		return bridged ? QStringLiteral("VST2 effect (Windows, through Carla's Wine bridge)")
			: QStringLiteral("VST2 effect (through Carla)");
	}
};

} // namespace lmms::carlavst2

#endif // LMMS_CARLA_VST2_SUB_PLUGIN_FEATURES_H
