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
