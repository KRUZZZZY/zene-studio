/*
 * CommandListsCustom.cpp - the 0.5.0 W3 customisation and access list surfaces
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

ListSpec themeColors()
{
	ListSpec out = spec("Theme Colours", "theme.get", "colors", {QStringLiteral("name"), QStringLiteral("color"),
		QStringLiteral("overridden")});
	out.summaryKeys = QStringList{QStringLiteral("palette"), QStringLiteral("applies")};
	const QHash<QString, QString> role{{QStringLiteral("name"), QStringLiteral("name")}};
	out.actions = {act("Change...", "theme.set_token", role, {}, true),
		act("Use the Palette's Colour", "theme.set_token", role, {{QStringLiteral("color"), QString()}})};
	ListExtra resetAll = extra("Reset All", "theme.reset", {}, false);
	resetAll.confirm = true;  // as the shortcuts' Reset All asks
	out.extras = {resetAll, extra("Export...", "theme.export"), extra("Import...", "theme.import"),
		readExtra("Contrast Check", "theme.get")};
	return out;
}

ListSpec trackManager()
{
	ListSpec out = spec("Track Manager", "track.overview", "tracks", {QStringLiteral("name"), QStringLiteral("type"),
		QStringLiteral("icon"), QStringLiteral("color"), QStringLiteral("muted"), QStringLiteral("soloed"), QStringLiteral("visible"),
		QStringLiteral("channel"), QStringLiteral("devices"), QStringLiteral("cpu_percent")});
	out.summaryKeys = QStringList{QStringLiteral("count")};
	const QHash<QString, QString> track{{QStringLiteral("track"), QStringLiteral("id")}};
	RowAction remove = act("Remove", "track.remove", track);
	remove.confirm = true;
	out.actions = {act("Mute", "track.set_mute", track, {{QStringLiteral("muted"), true}}),
		act("Unmute", "track.set_mute", track, {{QStringLiteral("muted"), false}}),
		act("Solo", "track.set_solo", track, {{QStringLiteral("solo"), true}}),
		act("Unsolo", "track.set_solo", track, {{QStringLiteral("solo"), false}}),
		act("Hide", "track.set_visible", track, {{QStringLiteral("visible"), false}}),
		act("Show", "track.set_visible", track, {{QStringLiteral("visible"), true}}),
		act("Rename...", "track.rename", track, {}, true), act("Colour...", "track.set_color", track, {}, true),
		act("Icon...", "track.set_icon", track, {}, true), act("Move...", "track.move", track, {}, true), remove};
	out.extras = {extra("Show All", "track.visibility_show_all", {}, false), readExtra("Channel Load", "mixer.cpu_load")};
	return out;
}

ListSpec memoryLocations()
{
	ListSpec out = spec("Memory Locations", "location.list", "locations", {QStringLiteral("name"), QStringLiteral("tick"),
		QStringLiteral("zoom")});
	const QHash<QString, QString> named{{QStringLiteral("name"), QStringLiteral("name")}};
	out.actions = {act("Go", "location.recall", named), act("Remove", "location.remove", named)};
	out.extras = {extra("Save Here...", "location.save", {{QStringLiteral("replace"), false}})};  // a name in use is refused, never replaced silently
	return out;
}

ListSpec savedSelections()
{
	ListSpec out = spec("Saved Selections", "selection.list", "selections", {QStringLiteral("name"), QStringLiteral("clips"),
		QStringLiteral("range")});
	const QHash<QString, QString> named{{QStringLiteral("name"), QStringLiteral("name")}};
	out.actions = {act("Select", "selection.apply", named), act("Remove", "selection.remove", named)};
	out.extras = {extra("Save the Selection...", "selection.save", {{QStringLiteral("replace"), false}})};
	return out;
}

ListSpec settingsCatalogue()
{
	ListSpec out = spec("Settings", "settings.search", "settings", {QStringLiteral("page"), QStringLiteral("title"),
		QStringLiteral("value"), QStringLiteral("key")}, {{QStringLiteral("text"), QString()}});
	out.summaryKeys = QStringList{QStringLiteral("count")};
	// The list command is settings.search: the find field narrows the list itself (it was a Search... button floating raw JSON).
	out.filterArg = QStringLiteral("text");
	out.filterLabel = tr("Find a setting by a word");
	out.actions = {act("Change...", "settings.set", {{QStringLiteral("key"), QStringLiteral("key")}}, {}, true)};
	out.extras = {extra("Back Up to a File...", "settings.export"), extra("Restore from a File...", "settings.import"),
		extra("Save as Preset...", "settings.preset_save")};
	return out;
}

ListSpec settingsPresets()
{
	ListSpec out = spec("Settings Presets", "settings.preset_list", "presets", {QStringLiteral("name"), QStringLiteral("settings")});
	const QHash<QString, QString> named{{QStringLiteral("name"), QStringLiteral("name")}};
	RowAction apply = act("Apply", "settings.preset_apply", named);
	apply.confirm = true;
	out.actions = {apply, act("Remove", "settings.preset_remove", named)};
	out.extras = {extra("Save the Current Settings...", "settings.preset_save")};
	return out;
}

} // namespace lmms::gui::CommandLists
