/*
 * ControlDeviceVst3.cpp - the VST3 half of the device catalogue behind
 *                         plugin.list and the dev-<n> ids (SPEC A11-A14,
 *                         feature row 78 / board task #668).
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
 * WHY THIS FILE EXISTS
 * --------------------
 * The VST3 hosts (plugins/Vst3Effect, plugins/Vst3Instrument) were reachable
 * from the interface and unreachable from the control surface: `plugin.list`
 * enumerated built-in, LADSPA and LV2 devices, so no dev-<n> ever named a VST3
 * class and `plugin.load` could not load one onto a track. For an AGENT-FIRST
 * product that is the whole difference between "the host exists" and "the host
 * is in the release" (CHARTER 3.1: if it cannot be driven through the socket it
 * is not in this release).
 *
 * The discovery is NOT a second scanner. It is the same call the plug-in
 * browser makes - Plugin::Descriptor::SubPluginFeatures::listSubPluginKeys() -
 * dispatched through the host descriptor, so a VST3 class appears here exactly
 * when the browser would offer it, and it disappears when the bundle does.
 *
 * Kept in its own translation unit rather than appended to
 * ControlDeviceHosted.cpp (the LADSPA/LV2 counterpart) so that neither file
 * grows across the file-length ratchet for the other's feature; the join is one
 * call in controlDeviceCatalogue().
 */

#include "ControlDeviceSupport.h"

#include <QFileInfo>
#include <QString>
#include <QStringList>

#include "Plugin.h"
#include "PluginFactory.h"

namespace lmms
{

namespace
{

//! The two module names a VST3 class can be hosted by, and the catalogue kind
//! each one gives its classes. Spelled out here for the same reason
//! ControlDeviceHosted.cpp spells out "ladspaeffect"/"lv2effect": the keys a
//! descriptor produces are what identifies the format, and the module name is
//! the only thing stable to dispatch on.
constexpr auto Vst3EffectHostName = "vst3effect";
constexpr auto Vst3InstrumentHostName = "vst3instrument";

//! Look \a name up among the build's loaded plugin descriptors.
const Plugin::Descriptor* findDescriptor(const char* name, Plugin::Type type)
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

//! The catalogue entries one VST3 host descriptor contributes: one per class
//! its own discovery enumerates for this build, in the host's own key order.
void appendVst3HostClass(const Plugin::Descriptor* descriptor, const QString& kind,
	QList<ControlDeviceEntry>* out)
{
	if (descriptor == nullptr || descriptor->subPluginFeatures == nullptr) { return; }

	Plugin::Descriptor::SubPluginFeatures::KeyList keys;
	descriptor->subPluginFeatures->listSubPluginKeys(descriptor, keys);

	for (const Plugin::Descriptor::SubPluginFeatures::Key& key : keys)
	{
		// A VST3 key is (file, class) - the bundle and the class inside it
		// (plugins/Vst3Effect/Vst3SubPluginFeatures.cpp). A key missing
		// either one names nothing loadable, so it is not offered: the
		// LADSPA host's keys carry (file, plugin) instead and can never be
		// mistaken for one of these.
		const QString file = key.attributes.value(QStringLiteral("file"));
		const QString className = key.attributes.value(QStringLiteral("class"));
		if (file.isEmpty() || className.isEmpty()) { continue; }

		ControlDeviceEntry entry;
		// The class name is the device's own id - it is what the key
		// carries and what a saved project stores.
		entry.name = className;
		entry.displayName = className;
		entry.format = QStringLiteral("vst3");
		entry.kind = kind;
		entry.file = file;
		entry.className = className;
		// Every enumerated class is one this build's own host accepted -
		// the enumeration IS the host's load path (vst3::listClasses() opens
		// the module and reads its factory), which is what LV2 gets from
		// Lv2Info::isValid(). A bundle deleted between the listing and the
		// load is caught by controlVst3DeviceModule().
		entry.loadable = true;
		out->append(entry);
	}
}

} // namespace

void controlVst3DeviceEntries(QList<ControlDeviceEntry>* out)
{
	if (out == nullptr) { return; }

	// Effects first, then instruments - the same order (and the same reason)
	// as the LV2 block: a build keeps the dev-<n> ids it already handed out
	// when a format gains a host.
	appendVst3HostClass(findDescriptor(Vst3EffectHostName, Plugin::Type::Effect),
		QStringLiteral("effect"), out);
	appendVst3HostClass(findDescriptor(Vst3InstrumentHostName, Plugin::Type::Instrument),
		QStringLiteral("instrument"), out);
}

bool controlVst3DeviceModule(const ControlDeviceEntry& entry, QString* pluginName,
	Plugin::Descriptor::SubPluginFeatures::Key* key, bool* useKey, ControlResult* error)
{
	const bool instrument = entry.kind == QLatin1String("instrument");
	const char* hostName = instrument ? Vst3InstrumentHostName : Vst3EffectHostName;
	const Plugin::Descriptor* descriptor = findDescriptor(
		hostName, instrument ? Plugin::Type::Instrument : Plugin::Type::Effect);
	if (descriptor == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("the VST3 host plugin '%1' is not in this build")
				.arg(QLatin1String(hostName)));
		return false;
	}
	if (entry.file.isEmpty() || entry.className.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the vst3 device '%1' carries no %2, so it names no class")
				.arg(entry.name, entry.file.isEmpty() ? QStringLiteral("bundle path")
													  : QStringLiteral("class name")));
		return false;
	}
	// A catalogue entry can outlive the bundle it named (a plug-in uninstalled
	// while the product runs). Refuse it here, typed, rather than let the host
	// fail with a message about a module that "does not export the required
	// ModuleEntry function".
	if (!QFileInfo::exists(entry.file))
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("the vst3 bundle '%1' is no longer there").arg(entry.file));
		return false;
	}

	Plugin::Descriptor::SubPluginFeatures::Key::AttributeMap attributes;
	attributes.insert(QStringLiteral("file"), entry.file);
	attributes.insert(QStringLiteral("class"), entry.className);
	*pluginName = QString::fromLatin1(hostName);
	*key = Plugin::Descriptor::SubPluginFeatures::Key(descriptor,
		entry.displayName.isEmpty() ? entry.name : entry.displayName, attributes);
	*useKey = true;
	return true;
}

} // namespace lmms
