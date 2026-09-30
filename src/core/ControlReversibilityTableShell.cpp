/*
 * ControlReversibilityTableShell.cpp - the A16 rows of M3.2's registry-first action verbs
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
 */

/*! One row per verb the File/Edit/View/Help menus and the main toolbar gained a command
 *  for (src/core/ControlCommandsProjectLifecycle.cpp, src/gui/ControlCommandsShell.cpp).
 *  The four that write are IRREVERSIBLE and say what a caller can still do; the rest write
 *  no project state. Joined by reversibilityRowTable() (ControlReversibilityTable.cpp).
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

const ReversibilityRow kShellRows[] = {
	R("project.new", RC::Irreversible, false,
		"a new project replaces the whole session, exactly as project.open does, and the engine "
		"keeps no snapshot of what it replaced (unsaved edits included)",
		"none",
		"save first (project.save), then reopen that file with project.open"),
	R("project.save_as_template", RC::Irreversible, false,
		"it REPLACES the user's default template file (<user template dir>/default.mpt) and keeps "
		"no copy; the session itself is not modified",
		"none",
		"copy default.mpt aside before the call; the reply's `replaced` says whether one existed"),
	R("project.import", RC::Irreversible, false,
		"the import filters add tracks with the project journal paused (ImportFilter::import), so "
		"there is no checkpoint for control.undo to apply",
		"none",
		"save before importing, or remove the added tracks (the reply counts them) with "
		"track.remove"),
	R("project.save_version", RC::Irreversible, false,
		"it writes a NEW file - the next free <name>-NN version - and makes it the project's file; "
		"no existing file is overwritten, so there is nothing an inverse would restore",
		"none",
		"the previous version is untouched on disk (the reply names it); delete the new file by hand "
		"to undo the save"),
	R("project.export_midi", RC::NotMutating, false,
		"it writes a MIDI file at the caller's own path and changes nothing inside the session",
		"no write to the session; the file is the caller's own artefact",
		""),
	R("transport.set_metronome", RC::NotMutating, false,
		"the metronome is monitoring state: the project file does not carry it and no model is "
		"journalled",
		"no project write (set it back to `previous` to restore)",
		""),
	R("window.fullscreen", RC::NotMutating, false,
		"the main window's fullscreen state is interface state; nothing in the project changes",
		"no write (call again to restore)",
		""),
	R("window.detach_all", RC::NotMutating, false,
		"window placement is interface state; nothing in the project changes",
		"no write (window.attach_all restores)",
		""),
	R("window.attach_all", RC::NotMutating, false,
		"window placement is interface state; nothing in the project changes",
		"no write (window.detach_all restores)",
		""),
	R("window.settings", RC::NotMutating, false,
		"it only SHOWS the Settings dialog; what the dialog writes on OK is settings.set's own "
		"recorded write",
		"no write",
		""),
	R("window.start_hub", RC::NotMutating, false,
		"opens the start hub: interface state; its own buttons run the project verbs, each with its "
		"own row",
		"nothing to reverse: the hub is closed like any window",
		""),
	R("window.undo_history", RC::NotMutating, false,
		"opens the undo history panel: interface state; its Undo/Redo buttons run control.undo and "
		"control.redo, each with its own row",
		"nothing to reverse: the panel is closed like any window",
		""),
	R("window.shortcuts", RC::NotMutating, false,
		"opens the keyboard shortcuts page: interface state, reads the menus",
		"nothing to reverse: the page is closed like any window",
		""),
	R("window.command_palette", RC::NotMutating, false,
		"it shows the palette; what the palette then runs is that action's or command's own write",
		"no write",
		""),
	R("window.screenshot", RC::NotMutating, false,
		"it renders a window into an image file at the caller's path; nothing in the session or the "
		"interface changes",
		"no write to the session; the PNG is the caller's own artefact",
		""),
	R("app.about", RC::NotMutating, false,
		"it shows the About dialog",
		"no write",
		""),
	R("app.online_help", RC::NotMutating, false,
		"it hands the documentation URL to the desktop's browser; nothing in this process changes",
		"no write",
		""),
};

#undef R

constexpr int kShellRowCount = static_cast<int>(sizeof(kShellRows) / sizeof(kShellRows[0]));

} // namespace

const ReversibilityRow* reversibilityShellRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kShellRowCount; }
	return kShellRows;
}

} // namespace control
} // namespace lmms
