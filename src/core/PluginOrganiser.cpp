/*
 * PluginOrganiser.cpp - favourites, folders and hidden entries for the plug-in menus
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
#include "PluginOrganiser.h"

#include <QJsonDocument>

#include "ConfigManager.h"
#include "ControlDeviceSupport.h"

namespace lmms::pluginorganiser
{

namespace
{

const QString kSection = QStringLiteral("plugins");
const QString kKey = QStringLiteral("organiser");

QJsonObject stored()
{
	return QJsonDocument::fromJson(ConfigManager::inst()->value(kSection, kKey).toUtf8()).object();
}

Entry fromJson(const QJsonObject& object)
{
	Entry out;
	out.favourite = object.value(QStringLiteral("favourite")).toBool();
	out.hidden = object.value(QStringLiteral("hidden")).toBool();
	out.folder = object.value(QStringLiteral("folder")).toString();
	out.name = object.value(QStringLiteral("name")).toString();
	return out;
}

//! The module and sub-plugin key a catalogue entry instantiates, by its format's own resolver.
bool moduleOf(const ControlDeviceEntry& device, QString* pluginName, Plugin::Descriptor::SubPluginFeatures::Key* key, bool* useKey)
{
	ControlResult ignored;
	if (device.format == QLatin1String("vst3")) { return controlVst3DeviceModule(device, pluginName, key, useKey, &ignored); }
	if (device.format == QLatin1String("clap")) { return controlClapDeviceModule(device, pluginName, key, useKey, &ignored); }
	return controlDeviceModule(device, pluginName, key, useKey, &ignored);
}

} // namespace

QString keyOf(const QString& pluginName, const QMap<QString, QString>& attributes)
{
	QStringList parts{pluginName};
	for (auto it = attributes.begin(); it != attributes.end(); ++it) { parts << it.key() + QLatin1Char('=') + it.value(); }
	return parts.join(QLatin1Char('|'));
}

QString keyOf(const ControlDeviceEntry& device)
{
	QString pluginName;
	Plugin::Descriptor::SubPluginFeatures::Key key;
	bool useKey = false;
	if (!moduleOf(device, &pluginName, &key, &useKey)) { return {}; }
	return keyOf(pluginName, useKey ? key.attributes : QMap<QString, QString>{});
}

Entry entry(const QString& key) { return fromJson(stored().value(key).toObject()); }

void setEntry(const QString& key, const Entry& value)
{
	QJsonObject all = stored();
	if (value.isDefault()) { all.remove(key); }
	else
	{
		QJsonObject object = toJson(key, value);
		object.remove(QStringLiteral("key"));
		all.insert(key, object);
	}
	// Written to the file with the rest of the config when the application saves it (not here: a
	// unit test driving this must not write the developer's own config file).
	ConfigManager::inst()->setValue(kSection, kKey, QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact)));
}

QMap<QString, Entry> entries()
{
	QMap<QString, Entry> out;
	const QJsonObject all = stored();
	for (auto it = all.begin(); it != all.end(); ++it) { out.insert(it.key(), fromJson(it.value().toObject())); }
	return out;
}

QString preferredFormat()
{
	return ConfigManager::inst()->value(QStringLiteral("plugins"), QStringLiteral("preferredformat"));
}

void setPreferredFormat(const QString& format)
{
	ConfigManager::inst()->setValue(QStringLiteral("plugins"), QStringLiteral("preferredformat"), format);
}

const QStringList& formatNames()
{
	static const QStringList names{QString(), QStringLiteral("clap"), QStringLiteral("vst3"), QStringLiteral("lv2"), QStringLiteral("ladspa")};
	return names;
}

QStringList folders()
{
	QStringList out;
	for (const Entry& value : entries())
	{
		if (!value.folder.isEmpty() && !out.contains(value.folder)) { out << value.folder; }
	}
	out.sort(Qt::CaseInsensitive);
	return out;
}

QJsonObject toJson(const QString& key, const Entry& value)
{
	return {{QStringLiteral("key"), key}, {QStringLiteral("favourite"), value.favourite}, {QStringLiteral("hidden"), value.hidden},
		{QStringLiteral("folder"), value.folder}, {QStringLiteral("name"), value.name}};
}

} // namespace lmms::pluginorganiser
