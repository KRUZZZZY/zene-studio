/*
 * ControlCommandsPluginPaths.cpp - plugin.paths_get / plugin.paths_set: the folders the plug-in hosts scan
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
/*! 0.5.0 L10 (MX-163): the plug-in folders on the spine (PluginPaths.h). plugin.paths_get answers,
 *  per format, the user's folders, the usual places and what a scan reads; plugin.paths_set replaces
 *  one format's folders as one undo step - plugin.rescan then reads them (LV2 from the next start). */

#include <QJsonArray>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "PluginPaths.h"

namespace lmms
{

using namespace control;

namespace
{

QJsonObject formatJson(pluginpaths::Format format)
{
	return {{QStringLiteral("format"), pluginpaths::formatNames().at(static_cast<int>(format))},
		{QStringLiteral("dirs"), QJsonArray::fromStringList(pluginpaths::configured(format))},
		{QStringLiteral("defaults"), QJsonArray::fromStringList(pluginpaths::defaults(format))},
		{QStringLiteral("searched"), QJsonArray::fromStringList(pluginpaths::searchDirs(format))},
		{QStringLiteral("applies"), format == pluginpaths::Format::Lv2 ? QStringLiteral("next_start") : QStringLiteral("rescan")}};
}

ControlResult pathsGet(const QJsonObject&)
{
	QJsonArray formats;
	for (int i = 0; i < pluginpaths::formatNames().size(); ++i) { formats.append(formatJson(static_cast<pluginpaths::Format>(i))); }
	return ControlResult::success({{QStringLiteral("formats"), formats}});
}

ControlResult pathsSet(const QJsonObject& args)
{
	pluginpaths::Format format;
	if (!pluginpaths::formatFromName(args.value(QStringLiteral("format")).toString(), &format))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'format' is vst3, clap or lv2"));
	}
	QStringList dirs;
	for (const QJsonValue& dir : args.value(QStringLiteral("dirs")).toArray()) { dirs << dir.toString(); }
	const QStringList before = pluginpaths::configured(format);
	pluginpaths::setConfigured(format, dirs);
	const QStringList after = pluginpaths::configured(format);
	QJsonObject result = formatJson(format);
	if (after == before) { return ControlResult::success(result); }  // the folders it had: no undo step to shadow the one under it
	addUndoStep([format, before]() { pluginpaths::setConfigured(format, before); }, [format, after]() { pluginpaths::setConfigured(format, after); });
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("dirs"), QJsonArray::fromStringList(before)}},
		QStringLiteral("control.undo"), {}, true, QStringLiteral("action checkpoint: the recorded undo step writes the previous folders back")));
	return ControlResult::success(result);
}

} // namespace

void registerPluginPathsCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.paths_get");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("paths_get");
	cmd.description = QStringLiteral("The folders each hosted format (vst3, clap, lv2) scans: 'dirs' - your own, searched first - "
		"'defaults' - the format's usual places - and 'searched', the folders a scan reads now (each once, only those that "
		"exist). 'applies' says when a change is read: rescan (plugin.rescan) or next_start (LV2).");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectProperty();
	cmd.mutating = false;
	cmd.requiresEngine = false;
	cmd.handler = pathsGet;
	registry.registerCommand(cmd);

	cmd.id = QStringLiteral("plugin.paths_set");
	cmd.verb = QStringLiteral("paths_set");
	cmd.description = QStringLiteral("Replace your own folders for one format ('format' vst3 / clap / lv2, 'dirs' a list of "
		"paths; empty clears them). Kept in the config file, not the project; one undo step. Run plugin.rescan to read them "
		"(LV2: from the next start).");
	cmd.argsSchema = objectSchema({{QStringLiteral("format"), enumProperty(pluginpaths::formatNames())},
		{QStringLiteral("dirs"), arrayProperty()}}, {QStringLiteral("format"), QStringLiteral("dirs")});
	cmd.mutating = true;
	cmd.handler = pathsSet;
	registry.registerCommand(cmd);
}

} // namespace lmms
