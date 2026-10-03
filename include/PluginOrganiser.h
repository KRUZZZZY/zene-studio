/*
 * PluginOrganiser.h - favourites, folders and hidden entries for the plug-in menus
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
#ifndef LMMS_PLUGIN_ORGANISER_H
#define LMMS_PLUGIN_ORGANISER_H

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms
{

struct ControlDeviceEntry;

/*! 0.5.0 L10 (MX-166): how the user sorts the plug-ins the pickers list - a favourite, a folder of
 *  their own, or hidden. Kept in the config file (plugins/organiser, a JSON object), the user's
 *  interface rather than any project. A device is named by a key that survives a rescan: the plugin
 *  module and its sub-plugin key's attributes (LADSPA file and label, LV2 URI, VST3 file and class,
 *  CLAP file and id) - the identity the effect picker and a saved project use - not its dev-<n>
 *  position in the catalogue, which moves when a plug-in is installed. UI / control thread only. */
namespace pluginorganiser
{

struct Entry
{
	bool favourite = false;
	bool hidden = false;
	QString folder;
	QString name;  //!< the display name when it was organised, for a readable list

	bool isDefault() const { return !favourite && !hidden && folder.isEmpty(); }
};

//! The key of a plugin module @a pluginName with sub-plugin attributes @a attributes (empty for a built-in).
LMMS_EXPORT QString keyOf(const QString& pluginName, const QMap<QString, QString>& attributes);
//! The key of a catalogue entry (plugin.list's dev-<n>); empty when this build cannot host it.
LMMS_EXPORT QString keyOf(const ControlDeviceEntry& entry);

LMMS_EXPORT Entry entry(const QString& key);
//! Store @a value for @a key (a default entry removes it); saved with the config file.
LMMS_EXPORT void setEntry(const QString& key, const Entry& value);
//! Every organised key and its entry.
LMMS_EXPORT QMap<QString, Entry> entries();
//! The folders in use, sorted.
LMMS_EXPORT QStringList folders();

LMMS_EXPORT QJsonObject toJson(const QString& key, const Entry& value);

/*! 0.5.0 L10 (MX-167): the format to list when one plug-in is installed in several ("clap", "vst3", "lv2",
 *  "ladspa"; empty lists them all). The pickers leave out a plug-in's other formats. Config file. */
LMMS_EXPORT QString preferredFormat();
LMMS_EXPORT void setPreferredFormat(const QString& format);
LMMS_EXPORT const QStringList& formatNames();

} // namespace pluginorganiser

} // namespace lmms

#endif // LMMS_PLUGIN_ORGANISER_H
