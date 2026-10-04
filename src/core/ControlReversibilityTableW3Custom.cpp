/*
 * ControlReversibilityTableW3Custom.cpp - the 0.5.0 W3 L13 / L14 commands' rows of the SPEC A16 table
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
#include "ControlReversibility.h"

namespace lmms
{
namespace control
{

namespace
{

using RC = ReversibilityClass;

//! A literal row: R(id, class, reversible, reason, mechanism, fallback).
#define R(id, cls, rev, reason, mechanism, fallback) \
	{ id, cls, reason, mechanism, fallback, rev, nullptr }

const ReversibilityRow kW3CustomRows[] = {
	// WF-117: theme editing (config ui/theme through UiConfigStore).
	R("theme.get", RC::NotMutating, false, "it reads the theme's colours. Nothing is written", "no write", ""),
	R("theme.set_token", RC::TrueInverse, true, "it writes one colour override in the config file (ui/theme) - the user's "
		"interface, not the project", "action checkpoint: the recorded undo step writes the previous ui/theme object back", ""),
	R("theme.reset", RC::TrueInverse, true, "it drops the colour overrides (config file, ui/theme)",
		"action checkpoint: the recorded undo step writes the previous ui/theme object back", ""),
	R("theme.export", RC::NotMutating, false, "it writes a theme file and nothing else: the project and the config are unchanged",
		"no write to the project (delete the file)", ""),
	R("theme.import", RC::TrueInverse, true, "it writes colour overrides read from a file (config file, ui/theme)",
		"action checkpoint: the recorded undo step writes the previous ui/theme object back", ""),
	// WF-097 / WF-120 / WF-104: the track manager.
	R("track.overview", RC::NotMutating, false, "it lists the tracks. Nothing is written", "no write", ""),
	R("track.set_color", RC::TrueInverse, true, "it sets a track's colour (saved with the project)",
		"action checkpoint: the recorded undo step puts the previous colour back", ""),
	R("track.set_visible", RC::TrueInverse, true, "it shows or hides a track in the arranger (saved with the project; "
		"it mutes nothing)", "action checkpoint: the recorded undo step puts the previous flag back", ""),
	R("track.set_icon", RC::TrueInverse, true, "it sets a track's icon (the <track-looks> section of the project)",
		"action checkpoint: the recorded undo step puts the previous icon back", ""),
	R("mixer.cpu_load", RC::NotMutating, false, "it reads the channels' processing load. Nothing is written", "no write", ""),
	// WF-141 / WF-142: the release notes and the offline assistant.
	R("app.ask", RC::NotMutating, false, "it searches the manual and the command descriptions. Nothing is written or sent",
		"no write", ""),
	R("app.whats_new", RC::NotMutating, false, "it reads the release notes the program ships. Nothing is written or fetched",
		"no write", ""),
	// WF-101 / WF-105 / WF-132 and the windows of WF-141 / WF-142: interface state.
	R("window.stay_on_top", RC::NotMutating, false, "it keeps the main window above others or not: interface state", "no write", ""),
	R("window.tidy", RC::NotMutating, false, "it moves the floating windows: interface state", "no write", ""),
	R("window.large_clock", RC::NotMutating, false, "it opens a clock: a display", "no write", ""),
	R("window.ask", RC::NotMutating, false, "it opens the assistant: interface state", "no write", ""),
	R("window.whats_new", RC::NotMutating, false, "it shows the release notes and remembers they were shown (an interface "
		"marker in the config file, as the window layout is kept)", "no project write", ""),
	R("window.focus_panel", RC::NotMutating, false, "it moves the keyboard focus: interface state", "no write", ""),
	// WF-098 / WF-106: memory locations and saved selections (<view-memory>), WF-102: the history walk.
	R("location.save", RC::TrueInverse, true, "it saves a memory location with the project",
		"action checkpoint: the recorded undo step puts the previous list back", ""),
	R("location.recall", RC::NotMutating, false, "it moves the play position through transport.seek and the range through "
		"range.set, which record themselves; the zoom is interface state", "no write of its own", ""),
	R("location.list", RC::NotMutating, false, "it lists the memory locations. Nothing is written", "no write", ""),
	R("location.remove", RC::TrueInverse, true, "it removes a memory location from the project",
		"action checkpoint: the recorded undo step puts the previous list back", ""),
	R("selection.save", RC::TrueInverse, true, "it saves a selection with the project",
		"action checkpoint: the recorded undo step puts the previous list back", ""),
	R("selection.apply", RC::NotMutating, false, "it sets the range through range.set and selects clips: interface state",
		"no write of its own", ""),
	R("selection.list", RC::NotMutating, false, "it lists the saved selections. Nothing is written", "no write", ""),
	R("selection.remove", RC::TrueInverse, true, "it removes a saved selection from the project",
		"action checkpoint: the recorded undo step puts the previous list back", ""),
	R("control.history_jump", RC::NotMutating, false, "it runs control.undo or control.redo several times; each step is "
		"theirs and it records nothing of its own", "no write of its own (control.history_jump the other way)", ""),
	// WF-122: the settings as a whole.
	R("settings.search", RC::NotMutating, false, "it reads the settings. Nothing is written", "no write", ""),
	R("settings.export", RC::NotMutating, false, "it writes a backup file and nothing else: the config and the project are "
		"unchanged", "no write to the project (delete the file)", ""),
	R("settings.import", RC::TrueInverse, true, "it writes the settings a backup holds (config file) - a bounded set of keys",
		"action checkpoint: the recorded undo step writes each key's previous value back, or deletes it, and saves the config", ""),
	R("settings.preset_save", RC::TrueInverse, true, "it writes one settings preset in the config file",
		"action checkpoint: the recorded undo step writes the previous presets back", ""),
	R("settings.preset_apply", RC::TrueInverse, true, "it writes the settings a preset holds (config file)",
		"action checkpoint: the recorded undo step writes each key's previous value back, or deletes it, and saves the config", ""),
	R("settings.preset_list", RC::NotMutating, false, "it lists the settings presets. Nothing is written", "no write", ""),
	R("settings.preset_remove", RC::TrueInverse, true, "it removes one settings preset from the config file",
		"action checkpoint: the recorded undo step writes the previous presets back", ""),
	// WF-147: the tuner.
	R("tuner.set", RC::NotMutating, false, "it chooses what the tuner listens to and its reference: session state, neither "
		"recorded nor saved", "no write", ""),
	R("tuner.get_state", RC::NotMutating, false, "it reads the pitch of the newest audio. Nothing is written", "no write", ""),
	R("window.tuner", RC::NotMutating, false, "it opens the tuner: a display", "no write", ""),
	// WF-129 / WF-109.
	R("window.accessibility_report", RC::NotMutating, false, "it reads the controls' accessible names and, with 'fill', "
		"gives nameless ones the name they carry: interface state", "no write", ""),
	// 0.5.0 themes 10 and 17: the editable item list beside a canvas, and the announcement level.
	R("window.item_list", RC::NotMutating, false, "it opens or closes a canvas's item list: interface state (the list's "
		"edits are the canvases' own commands, each recorded by itself)", "no write", ""),
	R("view.announcements", RC::TrueInverse, true, "it writes the announcement level in the config file (ui/announcements)",
		"action checkpoint: the recorded undo step writes the previous ui/announcements object back", ""),
	R("view.crosshair", RC::TrueInverse, true, "it writes the cross-hair preference in the config file (ui/crosshair)",
		"action checkpoint: the recorded undo step writes the previous ui/crosshair object back", ""),
};

#undef R

constexpr int kW3CustomRowCount = static_cast<int>(sizeof(kW3CustomRows) / sizeof(kW3CustomRows[0]));

} // namespace

const ReversibilityRow* reversibilityW3CustomRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kW3CustomRowCount; }
	return kW3CustomRows;
}

} // namespace control
} // namespace lmms
