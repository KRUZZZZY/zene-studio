/*
 * AuComponent.cpp - the format-neutral half of Audio Unit (AUv2) hosting (Q050-491).
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

#include "AuComponent.h"

#include <QHash>

#include "Plugin.h"
#include "PluginScanCache.h"

namespace lmms::au
{

namespace
{

constexpr std::uint32_t code(char a, char b, char c, char d)
{
	return (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24)
		| (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16)
		| (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8)
		| static_cast<std::uint32_t>(static_cast<unsigned char>(d));
}

// The component types of AUComponent.h, spelled here so this file needs no Apple header.
constexpr std::uint32_t TypeMusicDevice = code('a', 'u', 'm', 'u');
constexpr std::uint32_t TypeEffect = code('a', 'u', 'f', 'x');
constexpr std::uint32_t TypeMusicEffect = code('a', 'u', 'm', 'f');

bool printable(std::uint32_t value)
{
	for (int shift = 24; shift >= 0; shift -= 8)
	{
		const auto byte = (value >> shift) & 0xffu;
		if (byte < 0x20u || byte > 0x7eu) { return false; }
	}
	return true;
}

//! The OSStatus values an Audio Unit host meets (AUComponent.h, AudioComponent.h, CoreAudioTypes.h).
const QHash<std::int32_t, QString>& statusNames()
{
	static const QHash<std::int32_t, QString> names{
		{-50, QStringLiteral("kAudio_ParamError")},
		{-108, QStringLiteral("kAudio_MemFullError")},
		{-10879, QStringLiteral("kAudioUnitErr_InvalidProperty")},
		{-10878, QStringLiteral("kAudioUnitErr_InvalidParameter")},
		{-10877, QStringLiteral("kAudioUnitErr_InvalidElement")},
		{-10876, QStringLiteral("kAudioUnitErr_NoConnection")},
		{-10875, QStringLiteral("kAudioUnitErr_FailedInitialization")},
		{-10874, QStringLiteral("kAudioUnitErr_TooManyFramesToProcess")},
		{-10871, QStringLiteral("kAudioUnitErr_InvalidFile")},
		{-10870, QStringLiteral("kAudioUnitErr_UnknownFileType")},
		{-10869, QStringLiteral("kAudioUnitErr_FileNotSpecified")},
		{-10868, QStringLiteral("kAudioUnitErr_FormatNotSupported")},
		{-10867, QStringLiteral("kAudioUnitErr_Uninitialized")},
		{-10866, QStringLiteral("kAudioUnitErr_InvalidScope")},
		{-10865, QStringLiteral("kAudioUnitErr_PropertyNotWritable")},
		{-10863, QStringLiteral("kAudioUnitErr_CannotDoInCurrentContext")},
		{-10851, QStringLiteral("kAudioUnitErr_InvalidPropertyValue")},
		{-10850, QStringLiteral("kAudioUnitErr_PropertyNotInUse")},
		{-10849, QStringLiteral("kAudioUnitErr_Initialized")},
		{-10848, QStringLiteral("kAudioUnitErr_InvalidOfflineRender")},
		{-10847, QStringLiteral("kAudioUnitErr_Unauthorized")},
		{-66745, QStringLiteral("kAudioUnitErr_RenderTimeout")},
		{-66746, QStringLiteral("kAudioComponentErr_InvalidFormat")},
		{-66747, QStringLiteral("kAudioComponentErr_InitializationTimedOut")},
		{-66748, QStringLiteral("kAudioComponentErr_NotPermitted")},
		{-66749, QStringLiteral("kAudioComponentErr_InstanceInvalidated")},
		{-66750, QStringLiteral("kAudioComponentErr_TooManyInstances")},
		{-66751, QStringLiteral("kAudioComponentErr_UnsupportedType")},
		{-66752, QStringLiteral("kAudioComponentErr_DuplicateDescription")},
		{-66754, QStringLiteral("kAudioComponentErr_InstanceTimedOut")},
	};
	return names;
}

PluginScanRecord recordOf(const Component& component, Kind kind)
{
	PluginScanRecord record;
	record.filePath = scanCachePath(component.id);
	// The record is never served by PluginScanCache::lookup() (no file carries this path): the size
	// field holds the component's version, so a scan that sees a new version rewrites the record.
	record.size = static_cast<qint64>(component.version);
	record.mtimeMs = 0;
	record.status = kind == Kind::Unsupported ? PluginScanRecord::Status::NotAPlugin
		: PluginScanRecord::Status::HasDescriptor;
	record.name = componentKey(component.id);
	record.displayName = component.name;
	record.author = component.vendor;
	record.description = QStringLiteral("Audio Unit (AUv2)");
	record.version = static_cast<int>(component.version);
	record.type = static_cast<int>(kind == Kind::Instrument ? Plugin::Type::Instrument
		: kind == Kind::Effect ? Plugin::Type::Effect : Plugin::Type::Undefined);
	// Rebuilding a descriptor needs the registry, never this record.
	record.hasSubPluginFeatures = true;
	return record;
}

//! The fields the cache file keeps: the metadata only for a record with a descriptor (PluginScanCache.cpp).
bool sameRecord(const PluginScanRecord& a, const PluginScanRecord& b)
{
	if (a.filePath != b.filePath || a.size != b.size || a.status != b.status) { return false; }
	return a.status != PluginScanRecord::Status::HasDescriptor
		|| (a.name == b.name && a.displayName == b.displayName && a.author == b.author && a.type == b.type
			&& a.version == b.version);
}

} // namespace

Kind kindOf(std::uint32_t type)
{
	if (type == TypeMusicDevice) { return Kind::Instrument; }
	if (type == TypeEffect || type == TypeMusicEffect) { return Kind::Effect; }
	return Kind::Unsupported;
}

QString fourCC(std::uint32_t value)
{
	if (!printable(value)) { return QStringLiteral("%1").arg(value, 8, 16, QLatin1Char('0')); }
	QString out;
	for (int shift = 24; shift >= 0; shift -= 8) { out.append(QLatin1Char(static_cast<char>((value >> shift) & 0xffu))); }
	return out;
}

bool fromFourCC(const QString& text, std::uint32_t* value)
{
	if (text.size() == 8)
	{
		bool ok = false;
		const auto parsed = text.toUInt(&ok, 16);
		if (!ok || printable(parsed)) { return false; }  // a printable code is always written as four characters
		*value = parsed;
		return true;
	}
	if (text.size() != 4) { return false; }
	std::uint32_t out = 0;
	for (const QChar ch : text)
	{
		const auto unicode = ch.unicode();
		if (unicode < 0x20 || unicode > 0x7e) { return false; }
		out = (out << 8) | unicode;
	}
	*value = out;
	return true;
}

QString componentKey(const ComponentId& id)
{
	return QStringLiteral("%1:%2:%3").arg(fourCC(id.type), fourCC(id.subtype), fourCC(id.manufacturer));
}

bool parseComponentKey(const QString& key, ComponentId* id)
{
	// Split by hand: a four-character code may itself contain ':' only when written as hex, which it never
	// is for a printable code, so exactly three fields are expected.
	const QStringList parts = key.split(QLatin1Char(':'));
	if (parts.size() != 3) { return false; }
	ComponentId out;
	if (!fromFourCC(parts.at(0), &out.type) || !fromFourCC(parts.at(1), &out.subtype)
		|| !fromFourCC(parts.at(2), &out.manufacturer) || !out.isValid())
	{
		return false;
	}
	*id = out;
	return true;
}

QString scanCachePath(const ComponentId& id)
{
	return QStringLiteral("/AudioUnits/%1/%2/%3").arg(fourCC(id.type), fourCC(id.subtype), fourCC(id.manufacturer));
}

Component splitComponentName(const ComponentId& id, const QString& registryName, std::uint32_t version)
{
	Component out;
	out.id = id;
	out.version = version;
	const int colon = registryName.indexOf(QStringLiteral(": "));
	if (colon < 0)
	{
		out.name = registryName.trimmed();
		return out;
	}
	out.vendor = registryName.left(colon).trimmed();
	out.name = registryName.mid(colon + 2).trimmed();
	return out;
}

ScanOutcome recordScan(const QList<Component>& found, PluginScanCache* cache)
{
	ScanOutcome outcome;
	for (const Component& component : found)
	{
		const QString path = scanCachePath(component.id);
		if (cache != nullptr && cache->isQuarantined(path))
		{
			outcome.quarantined.append(path);
			continue;
		}
		const Kind kind = kindOf(component.id.type);
		if (cache != nullptr)
		{
			const PluginScanRecord record = recordOf(component, kind);
			const PluginScanRecord* known = cache->record(path);
			// Unchanged records are not rewritten: the cache stays clean, so a rescan writes no file.
			if (known == nullptr || !sameRecord(*known, record)) { cache->store(record); }
		}
		if (kind != Kind::Unsupported) { outcome.listed.append(component); }
	}
	return outcome;
}

QString statusName(std::int32_t status)
{
	const auto it = statusNames().constFind(status);
	if (it != statusNames().constEnd()) { return it.value(); }
	const auto value = static_cast<std::uint32_t>(status);
	return status != 0 && printable(value) ? QStringLiteral("'%1'").arg(fourCC(value)) : QString();
}

QString instantiationError(const ComponentId& id, const QString& name, const QString& step, std::int32_t status)
{
	const QString who = name.isEmpty() ? componentKey(id) : QStringLiteral("%1 (%2)").arg(componentKey(id), name);
	QString out = QStringLiteral("the Audio Unit %1 could not be instantiated: %2").arg(who, step);
	if (status != 0)
	{
		const QString named = statusName(status);
		out += named.isEmpty() ? QStringLiteral(" (status %1)").arg(status)
			: QStringLiteral(" (status %1, %2)").arg(status).arg(named);
	}
	return out;
}

} // namespace lmms::au
