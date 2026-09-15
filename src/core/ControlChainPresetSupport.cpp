/*
 * ControlChainPresetSupport.cpp - the named chain store behind the chain.*
 *                                 command group (SPEC A11-A16).
 *
 * The design (a private, cross-project file store; the device document
 * plugin.state_save writes embedded verbatim; identity derived from that
 * document) is argued in the header. What matters here is that there is exactly
 * ONE definition of each of the three things a chain preset is made of:
 *
 *   the device's state   - controlDeviceStateBytes / controlRestoreDeviceState
 *                          (plugin.state_save / plugin.state_load's own pair),
 *                          never a second serialiser;
 *   the device's identity- controlChainPresetIdentity, read out of that state
 *                          document, never a parallel description;
 *   the chain's order    - the file's device order, and nothing else.
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

#include "ControlChainPresetSupport.h"

#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QFileInfo>

#include "ConfigManager.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"

namespace lmms
{

namespace
{

//! The file extension of a stored chain preset.
QString chainPresetSuffix()
{
	return QStringLiteral(".zcp");
}

//! The chain preset document's root element name.
QString chainPresetRootName()
{
	return QStringLiteral("zenechainpreset");
}

//! The one element name a device document's root must carry (the same name
//! controlRestoreEffectState refuses anything else by).
QString deviceStateRootName()
{
	return QStringLiteral("zenepluginstate");
}

/*! The document's own device identity, as the attribute values the state
 *  document carries.
 *
 *  A hosted plugin is identified by its key (LADSPA: file + label; LV2: the
 *  URI), because the descriptor name ("ladspaeffect"/"lv2effect") is shared by
 *  every device of the format - the same argument controlEffectStateXml and
 *  controlRestoreEffectState make.
 */
QString identityFormat(const QString& file, const QString& label, const QString& uri)
{
	if (!uri.isEmpty()) { return QStringLiteral("lv2"); }
	if (!label.isEmpty() || !file.isEmpty()) { return QStringLiteral("ladspa"); }
	return QStringLiteral("builtin");
}

/*! The device identity as a refusal names it, in the format's own terms: an LV2
 *  URI, a LADSPA label with the file it lives in, or a built-in name. A bare
 *  descriptor name would name no device at all ("ladspaeffect" is every LADSPA
 *  device), which is what makes a not_found worth reading.
 */
QString identityDescription(const ControlChainPresetDevice& identity)
{
	if (!identity.uri.isEmpty()) { return identity.uri; }
	if (!identity.label.isEmpty())
	{
		return identity.label + QLatin1String(" (") + identity.file + QLatin1Char(')');
	}
	return identity.plugin;
}

} // namespace

QString controlChainPresetDir()
{
	return ConfigManager::inst()->userPresetsDir() + QStringLiteral("chainpresets/");
}

bool controlChainPresetPath(const QString& name, QString* path, ControlResult* error)
{
	QString bare = name;
	if (bare.endsWith(chainPresetSuffix(), Qt::CaseInsensitive))
	{
		bare.chop(chainPresetSuffix().size());
	}
	// ONE name rule for the whole preset tree: plugin.preset_save's own check,
	// reused rather than restated (it refuses an empty name, a separator, ".."
	// and a leading dot).
	if (!controlSafePresetName(bare, error)) { return false; }
	*path = controlChainPresetDir() + bare + chainPresetSuffix();
	return true;
}

QString controlChainPresetName(const QString& path)
{
	return QFileInfo(path).completeBaseName();
}

QStringList controlChainPresetFiles()
{
	QStringList files;
	const QDir dir(controlChainPresetDir());
	for (const QFileInfo& entry : dir.entryInfoList({QStringLiteral("*") + chainPresetSuffix()},
			QDir::Files, QDir::Name))
	{
		files.append(entry.absoluteFilePath());
	}
	return files;
}

bool controlChainPresetIdentity(ControlChainPresetDevice* device, ControlResult* error)
{
	QDomDocument doc;
	if (!doc.setContent(device->state))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the device state is not XML, so the device cannot be identified"));
		return false;
	}
	const QDomElement root = doc.documentElement();
	if (root.tagName() != deviceStateRootName())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the device state is not a zenepluginstate document (root <%1>)")
				.arg(root.tagName()));
		return false;
	}
	device->plugin = root.attribute(QStringLiteral("plugin"));
	device->file = root.attribute(QStringLiteral("hosted_file"));
	device->label = root.attribute(QStringLiteral("hosted_id"));
	device->uri = root.attribute(QStringLiteral("hosted_uri"));
	device->format = identityFormat(device->file, device->label, device->uri);
	if (device->plugin.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the device state names no plugin, so the device it belongs to is "
				"not identified"));
		return false;
	}
	return true;
}

QByteArray controlChainPresetDocument(const QString& name,
	const QList<ControlChainPresetDevice>& devices)
{
	QDomDocument doc;
	QDomElement root = doc.createElement(chainPresetRootName());
	root.setAttribute(QStringLiteral("version"), 1);
	root.setAttribute(QStringLiteral("name"), name);
	root.setAttribute(QStringLiteral("devices"), static_cast<int>(devices.size()));
	doc.appendChild(root);

	for (const ControlChainPresetDevice& device : devices)
	{
		QDomElement holder = doc.createElement(QStringLiteral("device"));
		// The state document's own ROOT element is nested verbatim, so reading
		// it back is a clone rather than a re-serialisation that could drop an
		// attribute the device reads.
		QDomDocument stateDoc;
		if (!stateDoc.setContent(device.state)) { continue; }
		holder.appendChild(doc.importNode(stateDoc.documentElement(), true));
		root.appendChild(holder);
	}

	return doc.toString().toUtf8();
}

bool controlReadChainPreset(const QString& path, ControlChainPreset* out, ControlResult* error)
{
	QByteArray bytes;
	if (!controlReadFileBytes(path, &bytes, error)) { return false; }

	QDomDocument doc;
	QString parseError;
	int line = 0;
	int column = 0;
	if (!doc.setContent(bytes, &parseError, &line, &column))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1 is not valid XML: %2 (line %3, column %4)")
				.arg(path, parseError).arg(line).arg(column));
		return false;
	}

	const QDomElement root = doc.documentElement();
	if (root.tagName() != chainPresetRootName())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1 is not a chain preset document (root <%2>)")
				.arg(path, root.tagName()));
		return false;
	}

	ControlChainPreset preset;
	preset.path = path;
	// The FILE NAME is the store's key: chain.rename renames the file and the
	// document's own "name" attribute is only the name it was captured under, so
	// letting the attribute win would report the OLD name after a rename - which is
	// exactly the state chain.list's caller cannot act on.
	preset.name = controlChainPresetName(path);
	if (preset.name.isEmpty()) { preset.name = root.attribute(QStringLiteral("name")); }

	for (QDomElement holder = root.firstChildElement(QStringLiteral("device"));
		!holder.isNull(); holder = holder.nextSiblingElement(QStringLiteral("device")))
	{
		const QDomElement state = holder.firstChildElement();
		if (state.isNull())
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("%1 carries a device with no state document; a preset whose "
					"device has no settings is refused rather than applied half-read")
					.arg(path));
			return false;
		}

		// The nested element becomes a document of its own again, byte-identical
		// to what plugin.state_save wrote (same element, same attributes).
		QDomDocument stateDoc;
		stateDoc.appendChild(stateDoc.importNode(state, true));
		ControlChainPresetDevice device;
		device.state = stateDoc.toString().toUtf8();
		if (!controlChainPresetIdentity(&device, error)) { return false; }
		preset.devices.append(device);
	}

	*out = preset;
	return true;
}

namespace
{

/*! True when \a candidate is the device \a identity names, by the identity the
 *  format itself defines: an LV2 device by its URI (every LV2 device shares the
 *  descriptor name), a LADSPA device by file + label, a built-in by name. One
 *  rule, in one place, so the capture and the lookup cannot disagree.
 */
bool entryMatchesIdentity(const ControlDeviceEntry& candidate,
	const ControlChainPresetDevice& identity)
{
	if (!identity.uri.isEmpty())
	{
		return candidate.format == QLatin1String("lv2") && candidate.uri == identity.uri;
	}
	if (!identity.label.isEmpty() || !identity.file.isEmpty())
	{
		return candidate.format == QLatin1String("ladspa")
			&& candidate.label == identity.label && candidate.file == identity.file;
	}
	return candidate.format == QLatin1String("builtin") && candidate.name == identity.plugin;
}

} // namespace

bool controlChainPresetEntry(const ControlChainPresetDevice& device, ControlDeviceEntry* entry,
	int* index, ControlResult* error)
{
	ControlChainPresetDevice identity = device;
	if (identity.plugin.isEmpty() && !controlChainPresetIdentity(&identity, error)) { return false; }

	const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
	for (int i = 0; i < catalogue.size(); ++i)
	{
		const ControlDeviceEntry& candidate = catalogue.at(i);
		if (!entryMatchesIdentity(candidate, identity)) { continue; }
		if (!candidate.loadable)
		{
			*error = ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("this build lists device '%1' (dev-%2) but cannot load it, so the "
					"preset device cannot be recreated").arg(candidate.name).arg(i));
			return false;
		}
		*entry = candidate;
		*index = i;
		return true;
	}

	*error = ControlResult::failure(ControlErrorKind::NotFound,
		QStringLiteral("this build has no device '%1': the preset was captured with a device "
			"this binary cannot instantiate (plugin.list names the ones it has)")
			.arg(identityDescription(identity)));
	return false;
}

QString controlChainXml(EffectChain* chain)
{
	QDomDocument doc;
	QDomElement element = doc.createElement(chain->nodeName());
	doc.appendChild(element);
	// saveSettings, not saveState: the element itself is <fxchain>, which is
	// what loadSettings is handed back on the restore side.
	chain->saveSettings(doc, element);
	const QString xml = doc.toString();
	if (xml.size() > ControlSnapshotLimit) { return QString(); }
	return xml;
}

bool controlRestoreChainXml(EffectChain* chain, const QString& xml)
{
	QDomDocument doc;
	if (!doc.setContent(xml)) { return false; }
	const QDomElement root = doc.documentElement();
	if (root.tagName() != chain->nodeName()) { return false; }
	chain->restoreState(root);
	return true;
}

ControlResult controlApplyChainPreset(EffectChain* chain, const ControlChainPreset& preset)
{
	// EVERY device resolves to a loadable catalogue entry first: a preset with a
	// device this build cannot instantiate must refuse without destroying the
	// chain the caller already had.
	QList<ControlDeviceEntry> entries;
	QList<int> catalogueIndexes;
	ControlResult error;
	for (int i = 0; i < preset.devices.size(); ++i)
	{
		ControlDeviceEntry entry;
		int index = -1;
		if (!controlChainPresetEntry(preset.devices.at(i), &entry, &index, &error))
		{
			return ControlResult::failure(error.errorKind,
				QStringLiteral("device %1 of preset '%2': %3")
					.arg(i).arg(preset.name, error.errorMessage));
		}
		entries.append(entry);
		catalogueIndexes.append(index);
	}

	// Drop what the chain carries, the way plugin.unload drops one device.
	while (!chain->effects().empty())
	{
		Effect* existing = chain->effects().back();
		chain->removeEffect(existing);
		existing->deleteLater();
	}

	QJsonArray devices;
	for (int i = 0; i < entries.size(); ++i)
	{
		Effect* effect = controlInstantiateDevice(entries.at(i), chain, &error);
		if (effect == nullptr)
		{
			return ControlResult::failure(error.errorKind,
				QStringLiteral("device %1 of preset '%2' (%3) could not be instantiated: %4")
					.arg(i).arg(preset.name, entries.at(i).name, error.errorMessage));
		}
		// The device state goes back through plugin.state_load's own path, which
		// refuses a document written for a different device.
		ControlDeviceHandle handle;
		handle.effect = effect;
		handle.pluginName = QString::fromUtf8(effect->descriptor()->name);
		const ControlResult restored = controlRestoreDeviceState(handle, preset.devices.at(i).state);
		if (!restored.ok)
		{
			return ControlResult::failure(restored.errorKind,
				QStringLiteral("device %1 of preset '%2': %3")
					.arg(i).arg(preset.name, restored.errorMessage));
		}

		QJsonObject entry = controlChainPresetDeviceJson(preset.devices.at(i), i);
		entry.insert(QStringLiteral("id"), control::effectIdOf(target.chain->effects()[static_cast<std::size_t>(i)]));
		entry.insert(QStringLiteral("device"), control::deviceId(catalogueIndexes.at(i)));
		devices.append(entry);
	}

	QJsonObject result;
	result.insert(QStringLiteral("devices"), devices);
	result.insert(QStringLiteral("device_count"), devices.size());
	return ControlResult::success(result);
}

QJsonObject controlChainPresetDeviceJson(const ControlChainPresetDevice& device, int index)
{
	QJsonObject out;
	out.insert(QStringLiteral("index"), index);
	out.insert(QStringLiteral("plugin"), device.plugin);
	out.insert(QStringLiteral("format"), device.format);
	if (!device.file.isEmpty()) { out.insert(QStringLiteral("file"), device.file); }
	if (!device.label.isEmpty()) { out.insert(QStringLiteral("label"), device.label); }
	if (!device.uri.isEmpty()) { out.insert(QStringLiteral("uri"), device.uri); }
	out.insert(QStringLiteral("state_bytes"), device.state.size());
	out.insert(QStringLiteral("state_sha256"), controlSha256OfBytes(device.state));
	return out;
}

QJsonObject controlChainPresetJson(const ControlChainPreset& preset, bool detailed)
{
	QJsonArray devices;
	for (int i = 0; i < preset.devices.size(); ++i)
	{
		devices.append(controlChainPresetDeviceJson(preset.devices.at(i), i));
	}

	QJsonObject out;
	out.insert(QStringLiteral("name"), preset.name);
	out.insert(QStringLiteral("path"), preset.path);
	out.insert(QStringLiteral("device_count"), devices.size());
	out.insert(QStringLiteral("devices"), devices);
	if (detailed)
	{
		const QFileInfo info(preset.path);
		out.insert(QStringLiteral("bytes"), static_cast<qint64>(info.size()));
	}
	return out;
}

} // namespace lmms
