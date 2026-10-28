/*
 * AuComponent.h - what the Audio Unit (AUv2) host knows about a component without
 *                 touching Apple's API: its identity, its scan-cache record and
 *                 the words for a status it failed with (Q050-491).
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

#ifndef LMMS_AU_COMPONENT_H
#define LMMS_AU_COMPONENT_H

#include <cstdint>

#include <QList>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms
{

class PluginScanCache;

/*! Q050-491: the format-neutral half of Audio Unit hosting. The host modules (plugins/AuEffect,
 *  plugins/AuInstrument) are built on macOS only; this half is built everywhere, so the rules it
 *  holds - how a component is named, recorded in the scan cache and quarantined, and how a failed
 *  status is reported - are the same code on every platform and are tested on every job. */
namespace au
{

//! The three four-character codes that identify an AUv2 component in the system registry
//! (AudioComponentDescription's componentType / componentSubType / componentManufacturer).
struct ComponentId
{
	std::uint32_t type = 0;
	std::uint32_t subtype = 0;
	std::uint32_t manufacturer = 0;

	bool isValid() const { return type != 0 && subtype != 0 && manufacturer != 0; }
	bool operator==(const ComponentId& other) const
	{
		return type == other.type && subtype == other.subtype && manufacturer == other.manufacturer;
	}
};

//! One component the registry listed: its identity, its name and maker as AudioComponentCopyName
//! splits them ("Apple: AUBandpass"), and its version (AudioComponentGetVersion).
struct Component
{
	ComponentId id;
	QString name;
	QString vendor;
	std::uint32_t version = 0;
};

//! What a component is to the device chain: aumu is an instrument, aufx and aumf are effects;
//! the other types (generators, mixers, panners, format converters, outputs) are not hosted.
enum class Kind
{
	Effect,
	Instrument,
	Unsupported
};

LMMS_EXPORT Kind kindOf(std::uint32_t type);

//! 'aufx' as "aufx". A code with a byte outside printable ASCII is written as eight hex digits.
LMMS_EXPORT QString fourCC(std::uint32_t code);
//! "aufx" (or eight hex digits) back to the code; false for anything else.
LMMS_EXPORT bool fromFourCC(const QString& text, std::uint32_t* code);

//! The device's identity as a project and plugin.list store it: "aufx:bpas:appl".
LMMS_EXPORT QString componentKey(const ComponentId& id);
LMMS_EXPORT bool parseComponentKey(const QString& key, ComponentId* id);

/*! The component's entry in the plug-in scan cache and its quarantine list: "/AudioUnits/aufx/bpas/appl".
 *  An AUv2 component has no file of its own (Apple's built-in units all live in one bundle), so the
 *  cache names it by an absolute path built from its identity - absolute because the quarantine list
 *  only takes absolute paths (plugin.scan_cache_quarantine_add), and one per component so hiding one
 *  unit never hides the others in its bundle. */
LMMS_EXPORT QString scanCachePath(const ComponentId& id);

//! "Apple: AUBandpass" as {"Apple", "AUBandpass"}; a name without the separator has no vendor.
LMMS_EXPORT Component splitComponentName(const ComponentId& id, const QString& registryName, std::uint32_t version);

//! What one scan of the registry kept and what the quarantine list hid.
struct ScanOutcome
{
	QList<Component> listed;
	QStringList quarantined; //!< the hidden components' scan-cache paths
};

/*! The scan-cache half of discovery, the same rules as a plug-in file's: a component whose path is on
 *  the quarantine list is left out and counted; every other one is recorded in @a cache (its name,
 *  maker, version and kind) and listed when its kind is hosted. An unsupported type is recorded as
 *  "not a plug-in" and not listed. GUI thread, like every scan. @a cache may be null (nothing recorded). */
LMMS_EXPORT ScanOutcome recordScan(const QList<Component>& found, PluginScanCache* cache);

//! The name of an Audio Unit / Core Audio OSStatus ("kAudioUnitErr_FailedInitialization"); for a
//! status this table does not know, the four-character code when it is one, else empty.
LMMS_EXPORT QString statusName(std::int32_t status);

/*! The typed refusal of a component that would not instantiate: what failed, the component's key and
 *  name, and the status with its name, e.g. "the Audio Unit aufx:xxxx:appl (Missing) could not be
 *  instantiated: AudioComponentFindNext found no such component (status -1, ...)". */
LMMS_EXPORT QString instantiationError(const ComponentId& id, const QString& name, const QString& step,
	std::int32_t status);

} // namespace au

} // namespace lmms

#endif // LMMS_AU_COMPONENT_H
