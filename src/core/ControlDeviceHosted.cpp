/*
 * ControlDeviceHosted.cpp - the hosted formats (LADSPA, LV2) on the agent
 *                           control surface: their catalogue entries and the
 *                           plugin module + key that instantiate one.
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

#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "LadspaBase.h"
#include "lmmsconfig.h"
#include "Plugin.h"
#include "PluginFactory.h"

#ifdef LMMS_HAVE_LV2
#include "Lv2Manager.h"
#endif

namespace lmms
{

namespace
{

#if defined(LMMS_BUILD_WIN32) || defined(LMMS_BUILD_CYGWIN)
const QString LadspaSuffix = QStringLiteral(".dll");
#else
const QString LadspaSuffix = QStringLiteral(".so");
#endif

//! Look \a name up among the build's loaded plugin descriptors.
const Plugin::Descriptor* findDescriptor(const QString& name, Plugin::Type type)
{
	for (const Plugin::Descriptor* descriptor : getPluginFactory()->descriptors(type))
	{
		if (descriptor != nullptr && name == QLatin1String(descriptor->name))
		{
			return descriptor;
		}
	}
	return nullptr;
}

//! The LADSPA file key is the library file name, extension included.
QString ladspaFileKey(const QString& file)
{
	return file.endsWith(LadspaSuffix) ? file : file + LadspaSuffix;
}

} // namespace

//! The module + key that instantiate a LADSPA device: the whole format is
//! hosted by one module, and the device is selected by its key (file + label).
bool ladspaDeviceModule(const ControlDeviceEntry& entry, QString* pluginName,
	Plugin::Descriptor::SubPluginFeatures::Key* key, bool* useKey, ControlResult* error)
{
	const QString host = QStringLiteral("ladspaeffect");
	const Plugin::Descriptor* descriptor = findDescriptor(host, Plugin::Type::Effect);
	if (descriptor == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("the LADSPA host plugin 'ladspaeffect' is not in this build"));
		return false;
	}
	*pluginName = host;
	*key = ladspaKeyToSubPluginKey(descriptor, entry.displayName,
		ladspa_key_t(ladspaFileKey(entry.file), entry.label));
	*useKey = true;
	return true;
}

#ifdef LMMS_HAVE_LV2
//! The module + key that instantiate an LV2 device: one host module per plugin
//! type, and the device is selected by its URI key - the same key shape
//! Lv2SubPluginFeatures::listSubPluginKeys() produced for the catalogue entry,
//! so plugin.load addresses exactly the device plugin.list named.
bool lv2DeviceModule(const ControlDeviceEntry& entry, QString* pluginName,
	Plugin::Descriptor::SubPluginFeatures::Key* key, bool* useKey, ControlResult* error)
{
	if (entry.uri.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the lv2 device '%1' carries no URI, so it names no plugin")
				.arg(entry.name));
		return false;
	}
	const bool instrument = entry.kind == QLatin1String("instrument");
	const QString host = instrument ? QStringLiteral("lv2instrument")
									: QStringLiteral("lv2effect");
	const Plugin::Descriptor* descriptor = findDescriptor(
		host, instrument ? Plugin::Type::Instrument : Plugin::Type::Effect);
	if (descriptor == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("the LV2 host plugin '%1' is not in this build").arg(host));
		return false;
	}
	Lv2Manager* manager = Engine::getLv2Manager();
	if (manager == nullptr || manager->getPlugin(entry.uri) == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no LV2 plugin with URI '%1' is installed in this build's LV2 "
				"world").arg(entry.uri));
		return false;
	}
	Plugin::Descriptor::SubPluginFeatures::Key::AttributeMap attributes;
	attributes.insert(QStringLiteral("uri"), entry.uri);
	*pluginName = host;
	*key = Plugin::Descriptor::SubPluginFeatures::Key(descriptor, entry.displayName,
		attributes);
	*useKey = true;
	return true;
}
#endif // LMMS_HAVE_LV2

bool controlDeviceModule(const ControlDeviceEntry& entry, QString* pluginName,
	Plugin::Descriptor::SubPluginFeatures::Key* key, bool* useKey, ControlResult* error)
{
	*pluginName = entry.name;
	*useKey = false;

	if (entry.format == QLatin1String("ladspa"))
	{
		return ladspaDeviceModule(entry, pluginName, key, useKey, error);
	}
	if (entry.format == QLatin1String("lv2"))
	{
#ifdef LMMS_HAVE_LV2
		return lv2DeviceModule(entry, pluginName, key, useKey, error);
#else
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("device '%1' is an LV2 plugin and this build has no LV2 host "
				"(LMMS_HAVE_LV2 is off)").arg(entry.name));
		return false;
#endif
	}
	return true; // a built-in module is addressed by its plugin name alone
}

Effect* controlInstantiateDevice(const ControlDeviceEntry& entry, EffectChain* chain,
	ControlResult* error)
{
	if (!entry.loadable)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("device '%1' is a %2/%3 and this build has no host for it: only "
				"built-in effect and instrument modules and the hosted effects this build "
				"ships a host for (LADSPA, LV2) load into a chain")
				.arg(entry.name, entry.format, entry.kind));
		return nullptr;
	}

	QString pluginName;
	Plugin::Descriptor::SubPluginFeatures::Key key;
	bool useKey = false;
	if (!controlDeviceModule(entry, &pluginName, &key, &useKey, error)) { return nullptr; }
	if (!controlPluginIsInstantiable(pluginName, error)) { return nullptr; }

	Effect* effect = Effect::instantiate(pluginName, chain, useKey ? &key : nullptr);
	if (effect == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the engine could not instantiate '%1'").arg(pluginName));
		return nullptr;
	}
	chain->appendEffect(effect);
	return effect;
}

#ifdef LMMS_HAVE_LV2
//! One catalogue entry for an LV2 plugin the host accepted.
ControlDeviceEntry lv2Entry(const QString& uri, const QString& displayName, Plugin::Type type)
{
	ControlDeviceEntry entry;
	// The URI is the LV2 device's own id - the thing the host resolves and the
	// thing a project file stores - so it is what a dev-<n> entry names, and it
	// is also reported as 'uri'.
	entry.name = uri;
	entry.displayName = displayName;
	entry.format = QStringLiteral("lv2");
	entry.kind = type == Plugin::Type::Instrument ? QStringLiteral("instrument")
												  : QStringLiteral("effect");
	entry.uri = uri;
	// Every key that survives listSubPluginKeys() names a plugin whose Lv2Info
	// is valid, i.e. one Lv2ControlBase::check() accepted: this build's LV2 host
	// can instantiate it.
	entry.loadable = true;
	return entry;
}

//! The catalogue entries the LV2 keys of \a descriptor produce.
void appendLv2Descriptor(const Plugin::Descriptor* descriptor, Plugin::Type type,
	Lv2Manager* manager, QList<ControlDeviceEntry>* out)
{
	Plugin::Descriptor::SubPluginFeatures::KeyList keys;
	descriptor->subPluginFeatures->listSubPluginKeys(descriptor, keys);
	for (const Plugin::Descriptor::SubPluginFeatures::Key& key : keys)
	{
		// A key is an LV2 key iff it carries the "uri" attribute; the LADSPA
		// host puts "file"+"plugin" in its keys instead
		// (include/LadspaBase.h:72), so this cannot pick up a LADSPA device.
		const QString uri = key.attributes.value(QStringLiteral("uri"));
		if (uri.isEmpty() || manager->getPlugin(uri) == nullptr) { continue; }
		out->append(lv2Entry(uri, key.name, type));
	}
}

//! Every LV2 device of one plugin type, in the host's own key order.
void appendLv2Type(Plugin::Type type, Lv2Manager* manager, QList<ControlDeviceEntry>* out)
{
	for (const Plugin::Descriptor* descriptor : getPluginFactory()->descriptors(type))
	{
		if (descriptor == nullptr || descriptor->subPluginFeatures == nullptr)
		{
			continue;
		}
		appendLv2Descriptor(descriptor, type, manager, out);
	}
}
#endif // LMMS_HAVE_LV2

void controlLv2DeviceEntries(QList<ControlDeviceEntry>* out)
{
	if (out == nullptr) { return; }
#ifdef LMMS_HAVE_LV2
	Lv2Manager* manager = Engine::getLv2Manager();
	if (manager == nullptr) { return; }

	// The effect and instrument select dialogs do not walk the LV2 bundles
	// themselves: they ask the plugin factory for the descriptors of a type and
	// call Plugin::Descriptor::SubPluginFeatures::listSubPluginKeys() on each
	// (src/gui/modals/EffectSelectDialog.cpp:71, src/gui/PluginBrowser.cpp:184).
	// This is that same call: Lv2SubPluginFeatures::listSubPluginKeys() is the
	// implementation that walks Lv2Manager's own plugin map and hands back one
	// key per plugin the manager accepted (Lv2Info::isValid()). There is
	// deliberately no second scanner here - no lilv call and no bundle walk.
	//
	// Order: listSubPluginKeys() iterates a std::map keyed by URI, and this
	// walks effects then instruments, so the LV2 block is effects-by-URI then
	// instruments-by-URI - deterministic for a binary.
	appendLv2Type(Plugin::Type::Effect, manager, out);
	appendLv2Type(Plugin::Type::Instrument, manager, out);
#endif
}

} // namespace lmms
