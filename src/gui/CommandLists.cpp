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

#include "CommandListBuild.h"

namespace lmms::gui::CommandLists
{

ListSpec revisions()
{
	ListSpec out = spec("Revisions", "revisions.list", "revisions",
		{QStringLiteral("id"), QStringLiteral("source"), QStringLiteral("timestamp"), QStringLiteral("bytes")},
		{{QStringLiteral("include_git"), true}});
	out.summaryKeys = QStringList{QStringLiteral("file"), QStringLiteral("count")};
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
	out.summaryKeys = QStringList{QStringLiteral("file"), QStringLiteral("missing_count"), QStringLiteral("present_count"),
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
	out.summaryKeys = QStringList{QStringLiteral("tempo_mode"), QStringLiteral("stretch"), QStringLiteral("source_tempo"),
		QStringLiteral("marker_count")};
	const QHash<QString, QString> marker{{QStringLiteral("source_frame"), QStringLiteral("source_frame")}};
	RowAction remove = act("Remove", "warp.remove", marker, where);
	remove.confirm = true;
	out.actions = {act("Move...", "warp.move", marker, where, true), remove};
	out.extras = {extra("Add Marker...", "warp.add", where)};
	return out;
}

ListSpec rackZones(const QString& channel)
{
	const QJsonObject where{{QStringLiteral("channel"), channel}};
	ListSpec out = spec("Rack Zones", "rack.get_state", "zones", {QStringLiteral("zone"), QStringLiteral("low_key"),
		QStringLiteral("high_key"), QStringLiteral("low_velocity"), QStringLiteral("high_velocity"),
		QStringLiteral("chain"), QStringLiteral("sample")}, where);
	out.summaryKeys = QStringList{QStringLiteral("chain_count"), QStringLiteral("zone_count")};
	RowAction remove = act("Remove", "rack.zone_remove", {{QStringLiteral("zone"), QStringLiteral("zone")}}, where);
	remove.confirm = true;
	out.actions = {remove};
	out.extras = {extra("Add Zone...", "rack.zone_add", where)};
	return out;
}

ListSpec pluginsInProject()
{
	ListSpec out = spec("Plug-ins in Project", "oop.get_state", "chains", {QStringLiteral("label"),
		QStringLiteral("kind"), QStringLiteral("target"), QStringLiteral("device"), QStringLiteral("plugin")});
	out.nestedKeys = QStringList{QStringLiteral("instrument"), QStringLiteral("devices")};
	out.summaryKeys = QStringList{QStringLiteral("separate_process_count"), QStringLiteral("refused_count")};
	const QHash<QString, QString> device{{QStringLiteral("target"), QStringLiteral("target")},
		{QStringLiteral("plugin"), QStringLiteral("device")}};
	RowAction remove = act("Remove", "plugin.unload", device);
	remove.confirm = true;
	out.actions = {act("Bypass", "plugin.bypass", device, {{QStringLiteral("bypass"), true}}),
		act("Switch On", "plugin.bypass", device, {{QStringLiteral("bypass"), false}}),
		act("Open Editor", "plugin.editor_open", device), act("Restart Host", "oop.restart", device), remove};
	return out;
}

ListSpec pluginManager()
{
	ListSpec out = spec("Plug-in Manager", "plugin.list", "devices", {QStringLiteral("display_name"), QStringLiteral("format"),
		QStringLiteral("kind"), QStringLiteral("vendor"), QStringLiteral("loadable"), QStringLiteral("id")});
	const QHash<QString, QString> device{{QStringLiteral("device"), QStringLiteral("id")}};
	out.actions = {act("Add to Favourites", "plugin.organise", device, {{QStringLiteral("favourite"), true}}),
		act("Remove from Favourites", "plugin.organise", device, {{QStringLiteral("favourite"), false}}),
		act("Hide", "plugin.organise", device, {{QStringLiteral("hidden"), true}}),
		act("Show", "plugin.organise", device, {{QStringLiteral("hidden"), false}}),
		act("Move to Folder...", "plugin.organise", device, {}, true)};
	out.extras = {extra("Rescan", "plugin.rescan", {}, false),
		extra("Rescan in Background", "plugin.scan_start", {}, false),
		extra("Scan Progress", "plugin.scan_status", {}, false),
		extra("Cancel Scan", "plugin.scan_cancel", {}, false),
		readExtra("Plug-in Folders", "plugin.paths_get"),
		extra("Set Folders...", "plugin.paths_set"), readExtra("Organised Plug-ins", "plugin.organisation")};
	return out;
}

ListSpec pluginModules()
{
	ListSpec out = spec("Plug-in Modules", "plugin.scan_cache_list", "entries", {QStringLiteral("path"), QStringLiteral("status"),
		QStringLiteral("name"), QStringLiteral("author"), QStringLiteral("error")});
	const QHash<QString, QString> path{{QStringLiteral("path"), QStringLiteral("path")}};
	out.actions = {act("Quarantine...", "plugin.scan_cache_quarantine_add", path, {}, true),
		act("Release", "plugin.scan_cache_quarantine_remove", path)};
	out.summaryKeys = QStringList{QStringLiteral("count"), QStringLiteral("load_failed_count")};
	out.extras = {extra("Rescan", "plugin.rescan", {}, false),
		extra("Rescan in Background", "plugin.scan_start", {}, false),
		extra("Scan Progress", "plugin.scan_status", {}, false),
		extra("Cancel Scan", "plugin.scan_cancel", {}, false)};
	return out;
}

ListSpec takeLanes(const QString& track)
{
	const QJsonObject where{{QStringLiteral("track"), track}};
	ListSpec out = spec("Take Lanes", "comp.lane_list", "tracks", {QStringLiteral("lane"), QStringLiteral("name"),
		QStringLiteral("take_count")}, where);
	out.nestedKeys = QStringList{QStringLiteral("lanes")};
	RowAction remove = act("Remove", "comp.lane_remove", {{QStringLiteral("lane"), QStringLiteral("lane")}}, where);
	remove.confirm = true;
	out.actions = {remove};
	out.extras = {extra("Add Lane...", "comp.lane_add", where), extra("Comp a Range...", "comp.select", where)};
	return out;
}

ListSpec grooves()
{
	ListSpec out = spec("Grooves", "groove.list", "templates", {QStringLiteral("name"), QStringLiteral("length_ticks"),
		QStringLiteral("step_ticks"), QStringLiteral("slot_count")});
	out.summaryKeys = QStringList{QStringLiteral("count"), QStringLiteral("max_grooves")};
	const QHash<QString, QString> name{{QStringLiteral("name"), QStringLiteral("name")}};
	RowAction remove = act("Remove", "groove.remove", name);
	remove.confirm = true;
	out.actions = {act("Rename...", "groove.rename", name, {}, true), remove};
	out.extras = {extra("New Groove...", "groove.set")};
	return out;
}

ListSpec safeStart()
{
	ListSpec out = spec("Safe Start", "safestart.get_state", "skipped",
		{QStringLiteral("plugin"), QStringLiteral("file"), QStringLiteral("reason")});
	out.summaryKeys = QStringList{QStringLiteral("previous_run_exited_cleanly"), QStringLiteral("safe_start"),
		QStringLiteral("skip_enabled"), QStringLiteral("skipped_count"), QStringLiteral("note")};
	ListExtra clear = extra("Clear", "safestart.clear", {}, false);
	clear.confirm = true;  // the crash marker is deleted for good (safestart.clear is irreversible)
	out.extras = {extra("Acknowledge", "safestart.acknowledge", {}, false),
		extra("Skip Plug-ins Next Start", "safestart.set_skip", {{QStringLiteral("enabled"), true}}, false),
		extra("Load Plug-ins Next Start", "safestart.set_skip", {{QStringLiteral("enabled"), false}}, false), clear};
	return out;
}

ListSpec shortcuts()
{
	// 0.5.0 L13 stage 1: a row carries its context (`scope`), because the same key may mean four
	// different things - a clash is only two actions of ONE context on one key. The row actions
	// pass the scope back, so "Set Keys..." on the note editor's Draw Tool edits THAT binding and
	// never the menu action or another panel's.
	ListSpec out = spec("Keyboard Shortcuts", "shortcuts.list", "bindings", {QStringLiteral("scope"), QStringLiteral("id"),
		QStringLiteral("keys"), QStringLiteral("default"), QStringLiteral("overridden"), QStringLiteral("command"),
		QStringLiteral("conflict")});
	out.summaryKeys = QStringList{QStringLiteral("count"), QStringLiteral("overridden_count"),
		QStringLiteral("conflict_count")};
	const QHash<QString, QString> id{{QStringLiteral("id"), QStringLiteral("id")},
		{QStringLiteral("scope"), QStringLiteral("scope")}};
	out.actions = {act("Set Keys...", "shortcuts.set", id, {}, true), act("Unbind", "shortcuts.set", id,
		{{QStringLiteral("keys"), QString()}}), act("Reset", "shortcuts.reset", id)};
	ListExtra resetAll = extra("Reset All", "shortcuts.reset", {}, false);
	resetAll.confirm = true;
	out.extras = {extra("Export...", "shortcuts.export"), extra("Import...", "shortcuts.import"), resetAll};
	return out;
}

ListSpec macros()
{
	ListSpec out = spec("Workflows", "workflow.list", "workflows", {QStringLiteral("name"), QStringLiteral("step_count"),
		QStringLiteral("steps")});
	out.summaryKeys = QStringList{QStringLiteral("count"), QStringLiteral("max_workflows"), QStringLiteral("scope")};
	const QHash<QString, QString> name{{QStringLiteral("name"), QStringLiteral("name")}};
	RowAction remove = act("Remove", "workflow.remove", name);
	remove.confirm = true;
	out.actions = {act("Run", "workflow.run", name), remove};
	out.extras = {extra("New Workflow...", "workflow.save")};
	return out;
}

ListSpec controllerMappings()
{
	ListSpec out = spec("MIDI Mappings", "controller.surface_state", "controls", {QStringLiteral("control"),
		QStringLiteral("channel"), QStringLiteral("controller"), QStringLiteral("soft_takeover"), QStringLiteral("feedback"),
		QStringLiteral("value")});
	out.summaryKeys = QStringList{QStringLiteral("count")};
	const QHash<QString, QString> control{{QStringLiteral("control"), QStringLiteral("control")}};
	out.actions = {act("Soft Takeover On", "controller.soft_takeover", control, {{QStringLiteral("enabled"), true}}),
		act("Soft Takeover Off", "controller.soft_takeover", control, {{QStringLiteral("enabled"), false}}),
		act("Feedback On", "controller.feedback", control, {{QStringLiteral("enabled"), true}}),
		act("Feedback Off", "controller.feedback", control, {{QStringLiteral("enabled"), false}}),
		act("Remove Mapping", "controller.unmap", {{QStringLiteral("target"), QStringLiteral("control")}})};
	out.extras = {extra("Map a Parameter...", "controller.map"), extra("Save as Template...", "controller.template_save")};
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
	RowAction remove = act("Remove", "marker.remove", marker);
	remove.confirm = true;
	out.actions = {act("Jump", "marker.jump", marker), act("Edit...", "marker.set", marker, {}, true), remove};
	out.extras = {extra("Add at Play Position", "marker.add", {}, false), extra("Add...", "marker.add")};
	return out;
}

ListSpec clips()
{
	ListSpec out = spec("Clip List", "clip.list", "clips", {QStringLiteral("name"), QStringLiteral("track_name"), QStringLiteral("start"),
		QStringLiteral("length"), QStringLiteral("kind")});
	out.summaryKeys = QStringList{QStringLiteral("count")};
	out.filterArg = QStringLiteral("name");  // clip.list keeps the clips whose name holds it
	out.filterLabel = tr("Find a clip by name");
	const QHash<QString, QString> clip{{QStringLiteral("clip"), QStringLiteral("clip")}};
	RowAction remove = act("Delete", "clip.delete", clip);
	remove.confirm = true;
	out.actions = {act("Select", "clip.select", clip), remove};
	return out;
}

ListSpec sections()
{
	ListSpec out = spec("Sections", "section.list", "sections", {QStringLiteral("name"), QStringLiteral("start"), QStringLiteral("end"),
		QStringLiteral("color")});
	const QHash<QString, QString> section{{QStringLiteral("section"), QStringLiteral("section")}};
	RowAction remove = act("Remove", "section.remove", section);
	remove.confirm = true;
	out.actions = {act("Select", "section.select", section), act("Edit...", "section.set", section, {}, true), remove};
	out.extras = {extra("Add...", "section.add")};
	return out;
}

ListSpec noteEvents(const QString& clip)
{
	const QJsonObject where{{QStringLiteral("clip"), clip}};
	ListSpec out = spec("MIDI Event List", "note.list", "notes", {QStringLiteral("key"), QStringLiteral("position"),
		QStringLiteral("length"), QStringLiteral("velocity"), QStringLiteral("pan"), QStringLiteral("probability"),
		QStringLiteral("muted"), QStringLiteral("release_velocity"), QStringLiteral("quantised")}, where);
	out.summaryKeys = QStringList{QStringLiteral("count")};
	const QHash<QString, QString> note{{QStringLiteral("note"), QStringLiteral("note")}};
	out.actions = {act("Velocity...", "note.velocity_set", note, where, true), act("Move...", "note.move", note, where, true),
		act("Length...", "note.resize", note, where, true), act("Probability...", "note.probability_set", note, where, true),
		act("Remove", "note.remove", note, where)};
	out.extras = {extra("Add Note...", "note.add", where)};
	return out;
}

ListSpec mixerSnapshots()
{
	ListSpec out = spec("Mixer Snapshots", "mixer.snapshot_list", "snapshots", {QStringLiteral("name"), QStringLiteral("channels")});
	out.summaryKeys = QStringList{QStringLiteral("count"), QStringLiteral("limit")};
	const QHash<QString, QString> named{{QStringLiteral("name"), QStringLiteral("name")}};
	RowAction remove = act("Remove", "mixer.snapshot_remove", named);
	remove.confirm = true;
	out.actions = {act("Recall", "mixer.snapshot_recall", named),
		act("Recall Faders Only", "mixer.snapshot_recall", named, {{QStringLiteral("parts"), QJsonArray{QStringLiteral("volume")}}}),
		act("Recall Parts...", "mixer.snapshot_recall", named, {}, true), remove};
	out.extras = {extra("Store...", "mixer.snapshot_store")};
	return out;
}

ListSpec lockedTracks()
{
	ListSpec out = spec("Locked Tracks", "project.locks", "tracks", {QStringLiteral("name"), QStringLiteral("track")});
	out.actions = {act("Unlock", "track.set_locked", {{QStringLiteral("track"), QStringLiteral("track")}}, {{QStringLiteral("locked"), false}})};
	return out;
}

ListSpec lockedClips()
{
	ListSpec out = spec("Locked Clips", "project.locks", "clips", {QStringLiteral("clip"), QStringLiteral("track")});
	out.actions = {act("Unlock", "clip.set_locked", {{QStringLiteral("clip"), QStringLiteral("clip")}}, {{QStringLiteral("locked"), false}})};
	return out;
}

ListSpec devicePresets(const QJsonObject& device)
{
	ListSpec out = spec("Device Presets", "plugin.preset_list", "presets", {QStringLiteral("name"), QStringLiteral("factory"),
		QStringLiteral("bytes")}, device);
	out.summaryKeys = QStringList{QStringLiteral("plugin"), QStringLiteral("count"), QStringLiteral("dir")};
	const QHash<QString, QString> named{{QStringLiteral("name"), QStringLiteral("name")}};
	RowAction remove = act("Remove", "plugin.preset_remove", named, device);
	remove.confirm = true;
	out.actions = {act("Load", "plugin.preset_load", named, device), act("Rename...", "plugin.preset_rename", named, device, true), remove};
	out.extras = {extra("Save Current as Preset...", "plugin.preset_save", device)};
	return out;
}

ListSpec renderQueue()
{
	ListSpec out = spec("Render Queue", "render.queue_list", "jobs", {QStringLiteral("job"), QStringLiteral("kind"), QStringLiteral("state"),
		QStringLiteral("out"), QStringLiteral("error")});
	out.summaryKeys = QStringList{QStringLiteral("open"), QStringLiteral("running"), QStringLiteral("limit")};
	const QHash<QString, QString> job{{QStringLiteral("job"), QStringLiteral("job")}};
	RowAction report = act("Report", "render.queue_report", job);
	report.showResult = true;
	out.actions = {act("Cancel", "render.queue_cancel", job), report};
	out.extras = {extra("Queue a Render...", "render.queue_add"), extra("Convert Files...", "render.convert"),
		extra("Clear Finished", "render.queue_clear", {}, false)};
	return out;
}

ListSpec backgroundTasks()
{
	ListSpec out = spec("Background Tasks", "app.tasks", "tasks", {QStringLiteral("kind"), QStringLiteral("state"), QStringLiteral("label"),
		QStringLiteral("progress")});
	out.summaryKeys = QStringList{QStringLiteral("count")};
	out.actions = {act("Cancel", "app.task_cancel", {{QStringLiteral("id"), QStringLiteral("id")}})};
	return out;
}

ListSpec subprojects()
{
	ListSpec out = spec("Subprojects", "project.subprojects", "subprojects", {QStringLiteral("track"), QStringLiteral("path"),
		QStringLiteral("stale"), QStringLiteral("exists")});
	out.summaryKeys = QStringList{QStringLiteral("count")};
	RowAction open = act("Open Its Project", "project.open", {{QStringLiteral("path"), QStringLiteral("path")}});
	open.confirm = true;
	open.caution = tr("It replaces the open project, and its unsaved changes are lost.");  // project.open keeps no snapshot
	out.actions = {act("Render Again", "project.subproject_refresh", {{QStringLiteral("track"), QStringLiteral("track")}}), open};
	out.extras = {extra("Add Subproject...", "project.subproject_add")};
	return out;
}

ListSpec alternatives()
{
	ListSpec out = spec("Alternatives", "project.alternatives", "alternatives", {QStringLiteral("name"), QStringLiteral("active"),
		QStringLiteral("exists"), QStringLiteral("file")});
	out.summaryKeys = QStringList{QStringLiteral("main"), QStringLiteral("active")};
	const QHash<QString, QString> named{{QStringLiteral("name"), QStringLiteral("name")}};
	RowAction switchTo = act("Switch To", "project.alternative_switch", named);
	switchTo.confirm = true;
	RowAction remove = act("Remove From List", "project.alternative_remove", named);
	remove.confirm = true;
	out.actions = {switchTo, remove};
	out.extras = {extra("Keep the Session as an Alternative...", "project.alternative_save"),
		extra("Back to the Main File", "project.alternative_switch", {{QStringLiteral("name"), QStringLiteral("main")}}, false)};
	return out;
}

} // namespace lmms::gui::CommandLists
