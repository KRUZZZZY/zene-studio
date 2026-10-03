/*
 * ControlCommandsPluginOrganise.cpp - plugin.organise / plugin.organisation: favourites, folders, hidden plug-ins
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
/*! 0.5.0 L10 (MX-166): the plug-in pickers' favourites, folders and hidden entries on the spine
 *  (PluginOrganiser.h). plugin.organise marks one device - by its dev-<n> or by the organiser key
 *  the picker knows it by - and is one undo step; plugin.organisation lists what is organised. */

#include <QJsonArray>

#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "PluginOrganiser.h"

namespace lmms
{

using namespace control;

namespace
{

//! The organiser key a call names ('key', or the 'device' dev-<n> resolved), with the device's name.
bool namedKey(const QJsonObject& args, QString* key, QString* name, ControlResult* error)
{
	if (!args.contains(QStringLiteral("key")) && !args.contains(QStringLiteral("device")))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("name the device: 'device' (dev-<n>) or 'key'"));
		return false;
	}
	if (args.contains(QStringLiteral("key")))
	{
		*key = args.value(QStringLiteral("key")).toString();
		*name = pluginorganiser::entry(*key).name;
	}
	else
	{
		ControlDeviceEntry device;
		int index = -1;
		if (!controlDeviceById(args.value(QStringLiteral("device")).toString(), &device, &index, error)) { return false; }
		*key = pluginorganiser::keyOf(device);
		*name = device.displayName;
	}
	if (key->isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("name the device: 'device' (dev-<n>) or 'key'"));
	}
	return !key->isEmpty();
}

ControlResult organise(const QJsonObject& args)
{
	QString key;
	QString name;
	ControlResult error;
	if (!namedKey(args, &key, &name, &error)) { return error; }
	const pluginorganiser::Entry before = pluginorganiser::entry(key);
	pluginorganiser::Entry after = before;
	after.favourite = args.value(QStringLiteral("favourite")).toBool(after.favourite);
	after.hidden = args.value(QStringLiteral("hidden")).toBool(after.hidden);
	after.folder = args.value(QStringLiteral("folder")).toString(after.folder).trimmed();
	if (!name.isEmpty()) { after.name = name; }
	const bool changed = after.favourite != before.favourite || after.hidden != before.hidden || after.folder != before.folder;
	pluginorganiser::setEntry(key, after);
	// Only a real change is an undo step (review L10-L03-L02-1 #10): a no-op must not shadow the edit under it.
	if (changed) { addUndoStep([key, before]() { pluginorganiser::setEntry(key, before); }, [key, after]() { pluginorganiser::setEntry(key, after); }); }
	QJsonObject result = pluginorganiser::toJson(key, after);
	result.insert(QStringLiteral("__transaction"), transactionPayload(pluginorganiser::toJson(key, before), QStringLiteral("control.undo"), {},
		true, QStringLiteral("action checkpoint: the recorded undo step writes the device's previous entry back")));
	return ControlResult::success(result);
}

ControlResult organisation(const QJsonObject& args)
{
	const QString folder = args.value(QStringLiteral("folder")).toString();
	QJsonArray entries;
	const QMap<QString, pluginorganiser::Entry> all = pluginorganiser::entries();
	for (auto it = all.begin(); it != all.end(); ++it)
	{
		if (!folder.isEmpty() && it.value().folder != folder) { continue; }
		entries.append(pluginorganiser::toJson(it.key(), it.value()));
	}
	return ControlResult::success({{QStringLiteral("entries"), entries},
		{QStringLiteral("folders"), QJsonArray::fromStringList(pluginorganiser::folders())}});
}

} // namespace

void registerPluginOrganiseCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.organise");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("organise");
	cmd.description = QStringLiteral("Sort a plug-in in the pickers: 'favourite' (listed under Favourites), 'hidden' (left out "
		"unless hidden entries are shown) and 'folder' (a folder of your own; empty takes it out). Name it by 'device' "
		"(plugin.list's dev-<n>) or by its 'key' (plugin.organisation answers keys). A field left out keeps its value. Kept "
		"in the config file, not the project; one undo step.");
	cmd.argsSchema = objectSchema({{QStringLiteral("device"), stringProperty()}, {QStringLiteral("key"), stringProperty()},
		{QStringLiteral("favourite"), booleanProperty()}, {QStringLiteral("hidden"), booleanProperty()},
		{QStringLiteral("folder"), stringProperty()}});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.requiresEngine = false;
	cmd.handler = organise;
	registry.registerCommand(cmd);

	cmd.id = QStringLiteral("plugin.organisation");
	cmd.verb = QStringLiteral("organisation");
	cmd.description = QStringLiteral("The plug-ins sorted with plugin.organise - each with its key, favourite, hidden, folder "
		"and name - and the folders in use; 'folder' lists one folder's.");
	cmd.argsSchema = objectSchema({{QStringLiteral("folder"), stringProperty()}});
	cmd.mutating = false;
	cmd.handler = organisation;
	registry.registerCommand(cmd);
}

} // namespace lmms
