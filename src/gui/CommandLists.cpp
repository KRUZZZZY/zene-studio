/*
 * CommandLists.cpp - the list surfaces: revisions, missing files, groups, markers, zones, plug-ins
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

#include "CommandLists.h"

namespace lmms::gui::CommandLists
{

namespace
{

QString tr(const char* text)
{
	return QObject::tr(text);
}

//! A row action: @a id with the row fields @a fromRow mapped onto its arguments.
RowAction act(const char* label, const char* id, QHash<QString, QString> fromRow, QJsonObject fixed = {}, bool ask = false)
{
	RowAction action;
	action.label = tr(label);
	action.id = QLatin1String(id);
	action.fromRow = std::move(fromRow);
	action.fixed = std::move(fixed);
	action.ask = ask;
	return action;
}

ListExtra extra(const char* label, const char* id, QJsonObject fixed = {}, bool ask = true)
{
	return ListExtra{tr(label), CommandCall{QLatin1String(id), std::move(fixed), {}, tr(label), ask}};
}

ListSpec spec(const char* title, const char* listId, const char* arrayKey, QStringList columns, QJsonObject args = {})
{
	ListSpec out;
	out.title = tr(title);
	out.listId = QLatin1String(listId);
	out.arrayKey = QLatin1String(arrayKey);
	out.columns = std::move(columns);
	out.listArgs = std::move(args);
	return out;
}

} // namespace

ListSpec revisions()
{
	ListSpec out = spec("Revisions", "revisions.list", "revisions",
		{QStringLiteral("id"), QStringLiteral("source"), QStringLiteral("timestamp"), QStringLiteral("bytes")},
		{{QStringLiteral("include_git"), true}});
	out.summaryKeys = {QStringLiteral("file"), QStringLiteral("count")};
	RowAction restore = act("Restore", "revisions.restore", {{QStringLiteral("id"), QStringLiteral("id")}});
	restore.confirm = true;
	RowAction compare = act("Compare With Now", "revisions.compare", {{QStringLiteral("a"), QStringLiteral("id")}});
	compare.showResult = true;
	out.actions = {restore, compare};
	return out;
}

ListSpec missingFiles(const QString& project)
{
	const QJsonObject where{{QStringLiteral("project"), project}};
	ListSpec out = spec("Project Files", "project.missing_assets", "references",
		{QStringLiteral("tag"), QStringLiteral("raw"), QStringLiteral("resolved"), QStringLiteral("exists"),
			QStringLiteral("missing")}, where);
	out.summaryKeys = {QStringLiteral("file"), QStringLiteral("missing_count"), QStringLiteral("present_count"),
		QStringLiteral("reference_count")};
	out.actions = {act("Relink...", "project.relink", {{QStringLiteral("from"), QStringLiteral("raw")}}, where, true)};
	out.extras = {extra("Collect Media...", "project.collect_media")};
	return out;
}

ListSpec vcaGroups()
{
	ListSpec out = spec("Groups", "vca.list", "groups", {QStringLiteral("id"), QStringLiteral("name"),
		QStringLiteral("gain"), QStringLiteral("muted"), QStringLiteral("soloed"), QStringLiteral("member_count")});
	const QHash<QString, QString> group{{QStringLiteral("group"), QStringLiteral("id")}};
	RowAction remove = act("Remove", "vca.remove", group);
	remove.confirm = true;
	out.actions = {act("Rename...", "vca.rename", group, {}, true), remove};
	out.extras = {extra("New Group...", "vca.create")};
	return out;
}

ListSpec warpMarkers(const QString& clip)
{
	const QJsonObject where{{QStringLiteral("clip"), clip}};
	ListSpec out = spec("Warp Markers", "warp.list", "markers",
		{QStringLiteral("index"), QStringLiteral("source_frame"), QStringLiteral("offset_ticks")}, where);
	out.summaryKeys = {QStringLiteral("tempo_mode"), QStringLiteral("stretch"), QStringLiteral("source_tempo"),
		QStringLiteral("marker_count")};
	const QHash<QString, QString> marker{{QStringLiteral("source_frame"), QStringLiteral("source_frame")}};
	out.actions = {act("Move...", "warp.move", marker, where, true), act("Remove", "warp.remove", marker, where)};
	out.extras = {extra("Add Marker...", "warp.add", where)};
	return out;
}

ListSpec rackZones(const QString& channel)
{
	const QJsonObject where{{QStringLiteral("channel"), channel}};
	ListSpec out = spec("Rack Zones", "rack.get_state", "zones", {QStringLiteral("zone"), QStringLiteral("low_key"),
		QStringLiteral("high_key"), QStringLiteral("low_velocity"), QStringLiteral("high_velocity"),
		QStringLiteral("chain"), QStringLiteral("sample")}, where);
	out.summaryKeys = {QStringLiteral("chain_count"), QStringLiteral("zone_count")};
	out.actions = {act("Remove", "rack.zone_remove", {{QStringLiteral("zone"), QStringLiteral("zone")}}, where)};
	out.extras = {extra("Add Zone...", "rack.zone_add", where)};
	return out;
}

ListSpec pluginsInProject()
{
	ListSpec out = spec("Plug-ins in Project", "oop.get_state", "chains", {QStringLiteral("label"),
		QStringLiteral("kind"), QStringLiteral("target"), QStringLiteral("device"), QStringLiteral("plugin")});
	out.nestedKey = QStringLiteral("devices");
	out.summaryKeys = {QStringLiteral("separate_process_count"), QStringLiteral("refused_count")};
	const QHash<QString, QString> device{{QStringLiteral("target"), QStringLiteral("target")},
		{QStringLiteral("plugin"), QStringLiteral("device")}};
	RowAction remove = act("Remove", "plugin.unload", device);
	remove.confirm = true;
	out.actions = {act("Bypass", "plugin.bypass", device, {{QStringLiteral("bypass"), true}}),
		act("Enable", "plugin.bypass", device, {{QStringLiteral("bypass"), false}}),
		act("Open Editor", "plugin.editor_open", device), act("Restart Host", "oop.restart", device), remove};
	return out;
}

ListSpec takeLanes(const QString& track)
{
	const QJsonObject where{{QStringLiteral("track"), track}};
	ListSpec out = spec("Take Lanes", "comp.lane_list", "tracks", {QStringLiteral("index"), QStringLiteral("name")}, where);
	out.nestedKey = QStringLiteral("lanes");
	out.actions = {act("Remove", "comp.lane_remove", {{QStringLiteral("lane"), QStringLiteral("index")}}, where)};
	out.extras = {extra("Add Lane...", "comp.lane_add", where), extra("Comp a Range...", "comp.select", where)};
	return out;
}

ListSpec grooves()
{
	ListSpec out = spec("Grooves", "groove.list", "templates", {QStringLiteral("name"), QStringLiteral("length_ticks"),
		QStringLiteral("step_ticks"), QStringLiteral("slot_count")});
	out.summaryKeys = {QStringLiteral("count"), QStringLiteral("max_grooves")};
	const QHash<QString, QString> name{{QStringLiteral("name"), QStringLiteral("name")}};
	out.actions = {act("Rename...", "groove.rename", name, {}, true), act("Remove", "groove.remove", name)};
	out.extras = {extra("New Groove...", "groove.set")};
	return out;
}

ListSpec safeStart()
{
	ListSpec out = spec("Safe Start", "safestart.get_state", "skipped",
		{QStringLiteral("plugin"), QStringLiteral("file"), QStringLiteral("reason")});
	out.summaryKeys = {QStringLiteral("previous_run_exited_cleanly"), QStringLiteral("safe_start"),
		QStringLiteral("skip_enabled"), QStringLiteral("skipped_count"), QStringLiteral("note")};
	out.extras = {extra("Acknowledge", "safestart.acknowledge", {}, false),
		extra("Skip Plug-ins Next Start", "safestart.set_skip", {{QStringLiteral("enabled"), true}}, false),
		extra("Load Plug-ins Next Start", "safestart.set_skip", {{QStringLiteral("enabled"), false}}, false),
		extra("Clear", "safestart.clear", {}, false)};
	return out;
}

} // namespace lmms::gui::CommandLists
