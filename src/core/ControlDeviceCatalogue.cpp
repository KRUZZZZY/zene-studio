/*
 * ControlDeviceCatalogue.cpp - the build's device catalogue behind
 *                              plugin.list and the dev-<n> ids (SPEC A11-A14).
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

#include "ControlDeviceSupport.h"

#include <algorithm>

#include <QHash>
#include <QSet>

#include "Engine.h"
#include "Ladspa2LMMS.h"
#include "LadspaBase.h"
#include "Plugin.h"
#include "PluginFactory.h"

namespace lmms
{

namespace
{

QString ladspaKeyName(const ladspa_key_t& key)
{
	return key.first + QLatin1Char('|') + key.second;
}

void addLadspaCategory(const l_sortable_plugin_t& plugins, const QString& kind, bool loadable,
	QHash<QString, QString>* kinds, QSet<QString>* loadableKeys)
{
	for (const sortable_plugin_t& plugin : plugins)
	{
		const QString key = ladspaKeyName(plugin.second);
		kinds->insert(key, kind);
		if (loadable) { loadableKeys->insert(key); }
	}
}

//! Which LADSPA entries the engine can actually load, and as what.
void collectLadspaCategories(Ladspa2LMMS* manager, QHash<QString, QString>* kinds,
	QSet<QString>* loadableKeys)
{
	// Only plugins hosted by the 'ladspaeffect' descriptor are loadable: this
	// build has one LADSPA host descriptor and it is of Plugin::Type::Effect.
	// LADSPA Source (instrument) plugins therefore have no host and refuse,
	// which plugin.list reports as loadable=false rather than hiding them.
	addLadspaCategory(manager->getValidEffects(), QStringLiteral("effect"), true, kinds, loadableKeys);
	addLadspaCategory(manager->getInstruments(), QStringLiteral("instrument"), false, kinds, loadableKeys);
	addLadspaCategory(manager->getAnalysisTools(), QStringLiteral("tool"), false, kinds, loadableKeys);
	addLadspaCategory(manager->getOthers(), QStringLiteral("other"), false, kinds, loadableKeys);
	addLadspaCategory(manager->getInvalidEffects(), QStringLiteral("effect"), false, kinds, loadableKeys);
}

void appendBuiltinDevices(Plugin::Type type, const QString& kind, QList<ControlDeviceEntry>* out)
{
	QList<const Plugin::Descriptor*> descriptors;
	for (const Plugin::Descriptor* descriptor : getPluginFactory()->descriptors(type))
	{
		if (descriptor != nullptr) { descriptors.append(descriptor); }
	}
	// Sorted by plugin name so the catalogue order (and therefore every
	// dev-<n> id) is deterministic for one build.
	std::sort(descriptors.begin(), descriptors.end(),
		[](const Plugin::Descriptor* a, const Plugin::Descriptor* b) {
			return qstrcmp(a->name, b->name) < 0;
		});
	for (const Plugin::Descriptor* descriptor : descriptors)
	{
		ControlDeviceEntry entry;
		entry.name = QString::fromUtf8(descriptor->name);
		entry.displayName = QString::fromUtf8(descriptor->displayName);
		entry.format = QStringLiteral("builtin");
		entry.kind = kind;
		entry.loadable = true;
		out->append(entry);
	}
}

void appendLadspaDevices(QList<ControlDeviceEntry>* out)
{
	Ladspa2LMMS* manager = Engine::getLADSPAManager();
	if (manager == nullptr) { return; }

	QHash<QString, QString> kinds;
	QSet<QString> loadableKeys;
	collectLadspaCategories(manager, &kinds, &loadableKeys);

	for (const sortable_plugin_t& plugin : manager->getSortedPlugins())
	{
		const ladspa_key_t& key = plugin.second;
		ControlDeviceEntry entry;
		entry.name = key.second;          // the LADSPA label: the plugin's own id
		entry.displayName = plugin.first; // the descriptive name
		entry.format = QStringLiteral("ladspa");
		entry.file = key.first;
		entry.label = key.second;
		entry.kind = kinds.value(ladspaKeyName(key), QStringLiteral("other"));
		entry.loadable = loadableKeys.contains(ladspaKeyName(key));
		out->append(entry);
	}
}

} // namespace

QList<ControlDeviceEntry> controlDeviceCatalogue()
{
	QList<ControlDeviceEntry> out;
	appendBuiltinDevices(Plugin::Type::Effect, QStringLiteral("effect"), &out);
	appendBuiltinDevices(Plugin::Type::Instrument, QStringLiteral("instrument"), &out);
	appendLadspaDevices(&out);
	// The hosted formats are appended in a fixed order (LADSPA, then LV2) so a
	// build that gains a host keeps the dev-<n> ids of the ones already there.
	controlLv2DeviceEntries(&out);
	return out;
}

bool controlDeviceById(const QString& id, ControlDeviceEntry* entry, int* index, ControlResult* error)
{
	const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
	const int wanted = control::idToIndex(id, QStringLiteral("dev-"));
	if (wanted < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a device id of the form dev-<n>").arg(id));
		return false;
	}
	if (wanted >= catalogue.size())
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no device %1 (the build lists %2 devices)")
				.arg(id).arg(catalogue.size()));
		return false;
	}
	*entry = catalogue.at(wanted);
	*index = wanted;
	return true;
}

QJsonObject controlDeviceJson(const ControlDeviceEntry& entry, int index)
{
	QJsonObject out;
	out.insert(QStringLiteral("id"), control::deviceId(index));
	out.insert(QStringLiteral("name"), entry.name);
	out.insert(QStringLiteral("display_name"), entry.displayName);
	out.insert(QStringLiteral("format"), entry.format);
	out.insert(QStringLiteral("kind"), entry.kind);
	out.insert(QStringLiteral("loadable"), entry.loadable);
	if (entry.format == QLatin1String("ladspa"))
	{
		out.insert(QStringLiteral("file"), entry.file);
		out.insert(QStringLiteral("label"), entry.label);
	}
	if (entry.format == QLatin1String("lv2"))
	{
		out.insert(QStringLiteral("uri"), entry.uri);
	}
	return out;
}

} // namespace lmms
