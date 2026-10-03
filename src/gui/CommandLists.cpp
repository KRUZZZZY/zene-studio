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
	out.extras = {extra("Collect Media...", "project.collect_media", where)};
	return out;
}

ListSpec vcaGroups()
{
	ListSpec out = spec("Groups", "vca.list", "groups", {QStringLiteral("id"), QStringLiteral("name"),
		QStringLiteral("gain"), QStringLiteral("muted"), QStringLiteral("soloed"), QStringLiteral("member_count")});
	const QHash<QString, QString> group{{QStringLiteral("group"), QStringLiteral("id")}};
	RowAction remove = act("Remove", "vca.remove", group);
	remove.confirm = true;
	// ED-034: the group's edit set - the tracks whose clips move together (vca.edit_move).
	out.actions = {act("Rename...", "vca.rename", group, {}, true), remove,
		act("Add Track to Edit Set...", "vca.track_add", group, {}, true),
		act("Remove Track from Edit Set...", "vca.track_remove", group, {}, true),
		act("Phase Lock On", "vca.set_phase_lock", group, {{QStringLiteral("locked"), true}}),
		act("Phase Lock Off", "vca.set_phase_lock", group, {{QStringLiteral("locked"), false}})};
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
	out.nestedKeys = {QStringLiteral("instrument"), QStringLiteral("devices")};
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
	ListSpec out = spec("Take Lanes", "comp.lane_list", "tracks", {QStringLiteral("lane"), QStringLiteral("name"),
		QStringLiteral("take_count")}, where);
	out.nestedKeys = {QStringLiteral("lanes")};
	out.actions = {act("Remove", "comp.lane_remove", {{QStringLiteral("lane"), QStringLiteral("lane")}}, where)};
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

ListSpec shortcuts()
{
	ListSpec out = spec("Keyboard Shortcuts", "shortcuts.list", "bindings", {QStringLiteral("id"), QStringLiteral("keys"),
		QStringLiteral("default"), QStringLiteral("overridden"), QStringLiteral("command")});
	out.summaryKeys = {QStringLiteral("count"), QStringLiteral("overridden_count")};
	const QHash<QString, QString> id{{QStringLiteral("id"), QStringLiteral("id")}};
	out.actions = {act("Set Keys...", "shortcuts.set", id, {}, true), act("Unbind", "shortcuts.set", id,
		{{QStringLiteral("keys"), QString()}}), act("Reset", "shortcuts.reset", id)};
	out.extras = {extra("Export...", "shortcuts.export"), extra("Import...", "shortcuts.import"),
		extra("Reset All", "shortcuts.reset", {}, false)};
	return out;
}

ListSpec macros()
{
	ListSpec out = spec("Macros", "macro.list", "macros", {QStringLiteral("name"), QStringLiteral("step_count"),
		QStringLiteral("steps")});
	out.summaryKeys = {QStringLiteral("count"), QStringLiteral("max_macros")};
	const QHash<QString, QString> name{{QStringLiteral("name"), QStringLiteral("name")}};
	RowAction remove = act("Remove", "macro.remove", name);
	remove.confirm = true;
	out.actions = {act("Run", "macro.run", name), remove};
	out.extras = {extra("New Macro...", "macro.save")};
	return out;
}

ListSpec controllerMappings()
{
	ListSpec out = spec("MIDI Mappings", "controller.surface_state", "controls", {QStringLiteral("control"),
		QStringLiteral("channel"), QStringLiteral("controller"), QStringLiteral("soft_takeover"), QStringLiteral("feedback"),
		QStringLiteral("value")});
	out.summaryKeys = {QStringLiteral("count")};
	const QHash<QString, QString> control{{QStringLiteral("control"), QStringLiteral("control")}};
	out.actions = {act("Soft Takeover On", "controller.soft_takeover", control, {{QStringLiteral("enabled"), true}}),
		act("Soft Takeover Off", "controller.soft_takeover", control, {{QStringLiteral("enabled"), false}}),
		act("Feedback On", "controller.feedback", control, {{QStringLiteral("enabled"), true}}),
		act("Feedback Off", "controller.feedback", control, {{QStringLiteral("enabled"), false}})};
	out.extras = {extra("Save as Template...", "controller.template_save")};
	return out;
}

ListSpec mappingTemplates()
{
	ListSpec out = spec("Mapping Templates", "controller.template_list", "templates", {QStringLiteral("name"),
		QStringLiteral("bindings")});
	const QHash<QString, QString> name{{QStringLiteral("name"), QStringLiteral("name")}};
	RowAction remove = act("Delete", "controller.template_delete", name);
	remove.confirm = true;
	out.actions = {act("Apply", "controller.template_apply", name), remove};
	out.extras = {extra("Save Current...", "controller.template_save")};
	return out;
}

ListSpec markers()
{
	ListSpec out = spec("Markers", "marker.list", "markers", {QStringLiteral("marker"), QStringLiteral("tick"),
		QStringLiteral("name"), QStringLiteral("color")});
	const QHash<QString, QString> marker{{QStringLiteral("marker"), QStringLiteral("marker")}};
	out.actions = {act("Jump", "marker.jump", marker), act("Edit...", "marker.set", marker, {}, true),
		act("Remove", "marker.remove", marker)};
	out.extras = {extra("Add at Play Position", "marker.add", {}, false), extra("Add...", "marker.add")};
	return out;
}

ListSpec noteEvents(const QString& clip)
{
	const QJsonObject where{{QStringLiteral("clip"), clip}};
	ListSpec out = spec("MIDI Event List", "note.list", "notes", {QStringLiteral("key"), QStringLiteral("position"),
		QStringLiteral("length"), QStringLiteral("velocity"), QStringLiteral("pan"), QStringLiteral("probability"),
		QStringLiteral("muted"), QStringLiteral("release_velocity"), QStringLiteral("quantised")}, where);
	out.summaryKeys = {QStringLiteral("count")};
	const QHash<QString, QString> note{{QStringLiteral("note"), QStringLiteral("note")}};
	out.actions = {act("Velocity...", "note.velocity_set", note, where, true), act("Move...", "note.move", note, where, true),
		act("Length...", "note.resize", note, where, true), act("Probability...", "note.probability_set", note, where, true),
		act("Remove", "note.remove", note, where)};
	out.extras = {extra("Add Note...", "note.add", where)};
	return out;
}

} // namespace lmms::gui::CommandLists
