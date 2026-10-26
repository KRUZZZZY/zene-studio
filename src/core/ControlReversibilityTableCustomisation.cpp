/*
 * ControlReversibilityTableCustomisation.cpp - the 0.5.0 L13 customisation commands' rows of
 *     the SPEC A16 classification table (shortcuts.*, window.layout_* / detach / attach /
 *     header_*, window.layout_project_*, workflow.*), joined into reversibilityRowTable() beside the window.* block
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

#define CONFIG_WRITE_REASON(what) \
	"it writes " what " in the config file (ui/*). That is the user's interface, not the project: no " \
	"journal checkpoint holds it - but the previous value is one bounded JSON object"
#define CONFIG_WRITE_MECHANISM \
	"action checkpoint (UiConfigStore): the recorded undo step writes the previous object back, saves " \
	"the config file and re-applies it to the interface, exactly as the command does"
//! Design theme 19: a workflow write lands in the library in use - the config file, or the project when the user keeps
//! workflows with each project (ui/workflows.scope).
#define WORKFLOW_WRITE_REASON(what) \
	"it writes " what " in the library in use: the config file (ui/workflows, the user's own, not the project), or - " \
	"when workflows are kept with each project - the project's <workspace>. Either way the previous library is one " \
	"bounded JSON object"
#define WORKFLOW_WRITE_MECHANISM \
	"action checkpoint (UiConfigStore write / writeProject): the recorded undo step writes the previous library back " \
	"where it was (saving the config file, or marking the project modified) and refreshes the Workflows menu"
#define PROJECT_LAYOUT_MECHANISM \
	"action checkpoint (UiConfigStore::writeProject): the recorded undo step puts the project's previous layout back"

const ReversibilityRow kCustomisationRows[] = {
	R("controller.map", RC::TrueInverse, true, "it binds a parameter to a MIDI controller (saved in the project)",
		"action checkpoint: the recorded undo step puts the previous binding back", ""),
	R("controller.unmap", RC::TrueInverse, true, "it removes a parameter's MIDI binding",
		"action checkpoint: the recorded undo step binds the parameter again", ""),
	R("shortcuts.list", RC::NotMutating, false, "it reads the menu actions' shortcuts. Nothing is written", "no write", ""),
	R("shortcuts.set", RC::TrueInverse, true, CONFIG_WRITE_REASON("one shortcut override"), CONFIG_WRITE_MECHANISM, ""),
	R("shortcuts.reset", RC::TrueInverse, true, CONFIG_WRITE_REASON("the shortcut overrides"), CONFIG_WRITE_MECHANISM, ""),
	R("shortcuts.import", RC::TrueInverse, true, CONFIG_WRITE_REASON("the shortcut overrides"), CONFIG_WRITE_MECHANISM, ""),
	R("shortcuts.preset_list", RC::NotMutating, false, "it lists the shipped keyboard maps. Nothing is written", "no write", ""),
	R("shortcuts.preset_apply", RC::TrueInverse, true,
		"it replaces the shortcut overrides in the config file and, before that, writes a timestamped "
		"shortcuts-backup JSON file in the workspace",
		"action checkpoint: the recorded undo step restores the previous ui/shortcuts overrides. The "
		"backup file is a deliberate durable side effect and is not removed by the undo; shortcuts.import "
		"reads it back",
		"delete the backup file and resend shortcuts.import with the previous overrides"),
	R("shortcuts.export", RC::NotMutating, false,
		"it writes the named file and nothing else: no project, model or interface state changes",
		"no write to the project or the interface (delete the file to undo it)", ""),
	R("window.layout_save", RC::TrueInverse, true, CONFIG_WRITE_REASON("one saved layout"), CONFIG_WRITE_MECHANISM, ""),
	R("window.layout_remove", RC::TrueInverse, true, CONFIG_WRITE_REASON("the saved layouts"), CONFIG_WRITE_MECHANISM, ""),
	R("window.layout_list", RC::NotMutating, false, "it reads the saved layouts' names. Nothing is written", "no write", ""),
	R("window.layout_apply", RC::NotMutating, false,
		"it switches the shell to a saved layout - view, panels, sizes. Interface state, kept in the ui/shell.layout "
		"setting as window.set_view's is: no project data is written",
		"no write to the project (interface state only; apply the previous layout to restore)", ""),
	R("window.detach", RC::NotMutating, false,
		"it shows one shell panel in a window of its own. Interface state: no project data is written",
		"no write to the project (window.attach restores)", ""),
	R("window.attach", RC::NotMutating, false,
		"it puts a panel shown in its own window back in the shell. Interface state: no project data is written",
		"no write to the project (window.detach restores)", ""),
	R("window.header_get", RC::NotMutating, false, "it reads the header's buttons. Nothing is written", "no write", ""),
	R("window.header_set", RC::TrueInverse, true, CONFIG_WRITE_REASON("the header's buttons"), CONFIG_WRITE_MECHANISM, ""),
	// Design theme 22: the layout a project remembers (<workspace>), beside the personal library above.
	R("window.layout_project_save", RC::TrueInverse, true,
		"it remembers the window layout with the project (<workspace>): project state, one bounded JSON object",
		PROJECT_LAYOUT_MECHANISM, ""),
	R("window.layout_project_clear", RC::TrueInverse, true,
		"it forgets the layout the project remembers (<workspace>): project state, one bounded JSON object",
		PROJECT_LAYOUT_MECHANISM, ""),
	R("window.layout_project_apply", RC::NotMutating, false,
		"it switches the shell to the layout the project remembers - view, panels, sizes. Interface state, as "
		"window.layout_apply is: no project data is written",
		"no write to the project (interface state only; apply the previous layout to restore)", ""),
	// Design theme 19: the recorded command macros, renamed workflows (new in 0.5.0, never shipped as macro.*).
	R("workflow.save", RC::TrueInverse, true, WORKFLOW_WRITE_REASON("one workflow"), WORKFLOW_WRITE_MECHANISM, ""),
	R("workflow.remove", RC::TrueInverse, true, WORKFLOW_WRITE_REASON("the workflows"), WORKFLOW_WRITE_MECHANISM, ""),
	R("workflow.set_scope", RC::TrueInverse, true, CONFIG_WRITE_REASON("where workflows are kept (ui/workflows.scope)"),
		CONFIG_WRITE_MECHANISM, ""),
	R("workflow.list", RC::NotMutating, false, "it reads the workflows in use. Nothing is written", "no write", ""),
	R("workflow.status", RC::NotMutating, false, "it reads the last workflow run's outcome and the recording state. Nothing is written",
		"no write", ""),
	R("workflow.run", RC::NotMutating, false,
		"it queues a workflow's steps; each step is a registry command of its own and records - and undoes - as "
		"itself (the run is acknowledged first, the mastering.run precedent)",
		"nothing of its own to reverse: undo each step, as its own record says", ""),
	// Design theme 19: step recovery - a stopped run resumed from its failed step, or past it.
	R("workflow.resume", RC::NotMutating, false,
		"it re-queues the steps of a workflow run that stopped at a failing step (that step again, or the ones after it); "
		"each step is a registry command of its own and records - and undoes - as itself, as workflow.run's do",
		"nothing of its own to reverse: undo each step, as its own record says", ""),
	R("session.pattern_list", RC::NotMutating, false, "it reads the patterns' numbers and names. Nothing is written",
		"no write", ""),
	R("surface.mcu_ports", RC::NotMutating, false, "it reads the MIDI client's ports. Nothing is written", "no write", ""),
	R("surface.mcu_connect", RC::NotMutating, false,
		"it connects a control surface: device state, not project state. What the surface then does records as the "
		"commands it runs", "no write to the project (surface.mcu_disconnect)", ""),
	R("surface.mcu_disconnect", RC::NotMutating, false, "it disconnects the control surface: device state",
		"no write to the project (surface.mcu_connect)", ""),
	R("surface.mcu_state", RC::NotMutating, false, "it reads the surface's state. Nothing is written", "no write", ""),
	R("surface.mcu_bank", RC::NotMutating, false, "it chooses which tracks the surface's strips show: device state",
		"no write to the project (set the previous bank)", ""),
	R("arrangement.zoom", RC::NotMutating, false,
		"it zooms and scrolls the arranger. Interface state (the zoom is kept in the ui/songeditorzoom setting): "
		"no project data is written", "no write to the project (arrangement.zoom_back restores)", ""),
	R("arrangement.zoom_back", RC::NotMutating, false,
		"it steps the arranger's zoom back over the history of earlier extents it kept. Interface state",
		"no write to the project (arrangement.zoom_forward walks back)", ""),
	R("arrangement.zoom_forward", RC::NotMutating, false,
		"it steps the arranger's zoom forward over what zoom_back went past. Interface state",
		"no write to the project (arrangement.zoom_back walks back)", ""),
	R("arrangement.zoom_toggle", RC::NotMutating, false,
		"it swaps the arranger's zoom with the extent it last left. Interface state", "no write to the project", ""),
	R("arrangement.set_tool", RC::NotMutating, false,
		"it chooses the arranger's tool. Interface state", "no write to the project (set the previous tool)", ""),
	R("window.tool_menu", RC::NotMutating, false,
		"it opens a menu at the pointer; what it runs records as itself", "no write", ""),
	R("arrangement.fit_tracks", RC::TrueInverse, true,
		"track heights are saved with the project, so this writes project state - a bounded list of integers",
		"action checkpoint: the recorded undo step gives every track its previous height back", ""),
};

#undef PROJECT_LAYOUT_MECHANISM
#undef WORKFLOW_WRITE_MECHANISM
#undef WORKFLOW_WRITE_REASON
#undef CONFIG_WRITE_MECHANISM
#undef CONFIG_WRITE_REASON
#undef R

constexpr int kCustomisationRowCount = static_cast<int>(sizeof(kCustomisationRows) / sizeof(kCustomisationRows[0]));

} // namespace

const ReversibilityRow* reversibilityCustomisationRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kCustomisationRowCount; }
	return kCustomisationRows;
}

} // namespace control
} // namespace lmms
