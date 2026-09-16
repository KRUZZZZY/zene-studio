/*
 * ControlDeviceClap.cpp - the CLAP half of the device catalogue behind
 *                         plugin.list and the dev-<n> ids (SPEC A11-A14,
 *                         feature row 79 / board task #669).
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
 * The CLAP hosts (plugins/ClapEffect and, since feature row 79,
 * plugins/ClapInstrument) were reachable from the interface and unreachable
 * from the control surface: `plugin.list` enumerated built-in, LADSPA, LV2 and
 * VST3 devices, so no dev-<n> ever named a CLAP class and `plugin.load` could
 * not load one onto a track. The instrument half is what row 79 adds: before
 * it, CLAP hosting was effects-only and a CLAP generator could not be reached
 * at all - not from the interface either, because there was no instrument host
 * module.
 *
 * The discovery is NOT a second scanner. It is the same call the plug-in
 * browser makes - Plugin::Descriptor::SubPluginFeatures::listSubPluginKeys() -
 * dispatched through the host descriptor, so a CLAP class appears here exactly
 * when the browser would offer it, and it disappears when the module does.
 * ClapSubPluginFeatures is the implementation that walks
 * ConfigManager::vstDir() for *.clap modules and filters the classes each one's
 * factory declares by plugin type, so "effect" and "instrument" below are the
 * two halves of one scan rather than two formats.
 *
 * Kept in its own translation unit rather than appended to
 * ControlDeviceHosted.cpp (the LADSPA/LV2 counterpart) or to
 * ControlDeviceVst3.cpp (the VST3 one), so that no file grows for another
 * format's feature; the join is one call in controlDeviceCatalogue().
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

//! The two module names a CLAP class can be hosted by, and the catalogue kind
//! each one gives its classes. Spelled out here for the same reason
//! ControlDeviceVst3.cpp spells out "vst3effect"/"vst3instrument": the keys a
//! descriptor produces are what identifies the format, and the module name is
//! the only thing stable to dispatch on.
const QString ClapEffectHostName = QStringLiteral("clapeffect");
const QString ClapInstrumentHostName = QStringLiteral("clapinstrument");

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

//! The catalogue entries one CLAP host descriptor contributes: one per class
//! its own discovery enumerates for this build, in the host's own key order.
void appendClapHostClass(const Plugin::Descriptor* descriptor, const QString& kind,
	QList<ControlDeviceEntry>* out)
{
	if (descriptor == nullptr || descriptor->subPluginFeatures == nullptr) { return; }

	Plugin::Descriptor::SubPluginFeatures::KeyList keys;
	descriptor->subPluginFeatures->listSubPluginKeys(descriptor, keys);

	for (const Plugin::Descriptor::SubPluginFeatures::Key& key : keys)
	{
		// A CLAP key is (file, id) - the module and the plug-in id its own
		// factory declared (plugins/ClapEffect/ClapSubPluginFeatures.cpp). A
		// key missing either one names nothing loadable, so it is not
		// offered: the LV2 host's keys carry "uri" instead and the LADSPA
		// host's "file"+"plugin", so neither can be mistaken for one of
		// these.
		const QString file = key.attributes.value(QStringLiteral("file"));
		const QString clapId = key.attributes.value(QStringLiteral("id"));
		if (file.isEmpty() || clapId.isEmpty()) { continue; }

		ControlDeviceEntry entry;
		// The plug-in's own id is the device's id - it is what the key
		// carries, what the host's load() matches on and what a saved project
		// stores - exactly as the LV2 entry uses the URI.
		entry.name = clapId;
		entry.displayName = key.attributes.value(QStringLiteral("name"), clapId);
		entry.format = QStringLiteral("clap");
		entry.kind = kind;
		entry.file = file;
		// Every enumerated class is one this build's own host accepted: the
		// enumeration IS the host's load path (clap::listClasses() opens the
		// module and reads its factory). A module deleted between the listing
		// and the load is caught by controlClapDeviceModule().
		entry.loadable = true;
		out->append(entry);
	}
}

} // namespace

void controlClapDeviceEntries(QList<ControlDeviceEntry>* out)
{
	if (out == nullptr) { return; }

	// Effects first, then instruments - the same order (and the same reason)
	// as the LV2 and VST3 blocks: a build keeps the dev-<n> ids it already
	// handed out when a format gains a host. ClapSubPluginFeatures filters the
	// classes of one module by plugin type, so an instrument class cannot
	// appear in the effect half and vice versa.
	appendClapHostClass(findDescriptor(ClapEffectHostName, Plugin::Type::Effect),
		QStringLiteral("effect"), out);
	appendClapHostClass(findDescriptor(ClapInstrumentHostName, Plugin::Type::Instrument),
		QStringLiteral("instrument"), out);
}

bool controlClapDeviceModule(const ControlDeviceEntry& entry, QString* pluginName,
	Plugin::Descriptor::SubPluginFeatures::Key* key, bool* useKey, ControlResult* error)
{
	const bool instrument = entry.kind == QLatin1String("instrument");
	const QString hostName = instrument ? ClapInstrumentHostName : ClapEffectHostName;
	const Plugin::Descriptor* descriptor = findDescriptor(
		hostName, instrument ? Plugin::Type::Instrument : Plugin::Type::Effect);
	if (descriptor == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("the CLAP host plugin '%1' is not in this build").arg(hostName));
		return false;
	}
	if (entry.file.isEmpty() || entry.name.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the clap device '%1' carries no %2, so it names no class")
				.arg(entry.name, entry.file.isEmpty() ? QStringLiteral("module path")
													  : QStringLiteral("plug-in id")));
		return false;
	}
	// A catalogue entry can outlive the module it named (a plug-in uninstalled
	// while the product runs). Refuse it here, typed, rather than let the host
	// fail with a message about a library it could not open.
	if (!QFileInfo::exists(entry.file))
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("the CLAP module '%1' is no longer there").arg(entry.file));
		return false;
	}

	// The key shape ClapSubPluginFeatures builds and the host's load() reads:
	// the module path and the plug-in's own id. "name" travels with it so the
	// instrument views and the preset folders have a display name.
	Plugin::Descriptor::SubPluginFeatures::Key::AttributeMap attributes;
	attributes.insert(QStringLiteral("file"), entry.file);
	attributes.insert(QStringLiteral("id"), entry.name);
	attributes.insert(QStringLiteral("name"),
		entry.displayName.isEmpty() ? entry.name : entry.displayName);
	*pluginName = hostName;
	*key = Plugin::Descriptor::SubPluginFeatures::Key(descriptor,
		entry.displayName.isEmpty() ? entry.name : entry.displayName, attributes);
	*useKey = true;
	return true;
}

} // namespace lmms
