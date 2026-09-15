/*
 * ControlReversibilityTableController.cpp - the A16 rows of the controller
 *                                          surface (feature row 19:
 *                                          controller.* soft-takeover,
 *                                          LED feedback and mapping templates)
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

/*! The snapshot block's rows for the controller surface. Every writing row
 *  here is `snapshot` with `reversible = false`, and that is a measurement, not
 *  a convenience: a MidiController lives inside the AutomatableModel's
 *  ControllerConnection, which is serialized through the MODEL's own
 *  saveSettings (the `<connection>` element MidiLearnTest.cpp round-trips) and
 *  is not a JournallingObject of its own. There is therefore no object whose
 *  checkpoint holds the previous soft-takeover flag, the previous feedback flag
 *  or the previous binding set, and an undo attempt must FAIL, typed, naming
 *  the inverse command instead of pretending to have one - exactly the
 *  `reversible = false` contract ControlReversibilityTableSnapshot.cpp
 *  documents. The template FILES are outside the project entirely (the user
 *  config dir), the settings.set precedent.
 */
const ReversibilityRow kControllerRows[] = {
	// =====================================================================
	// Feature row 19: the controller surface - soft-takeover, LED/feedback
	// output and saveable mapping templates (docs/CONTROLLER-SURFACES.md).
	// =====================================================================
	R("controller.surface_state", RC::NotMutating, false,
		"reads this project's bound MIDI controls: each one's target, its "
		"channel and controller number, the soft-takeover flags, the "
		"soft-takeover target and the port's output-event counters",
		"no write: a walk of the live ControllerConnection list and two "
		"relaxed-atomic counter reads per port. Nothing is stored, no "
		"project state and no journal checkpoint is touched",
		""),
	R("controller.template_list", RC::NotMutating, false,
		"reads the names of the mapping-template files in the user config "
		"dir (ControllerSurface::templateDirectory())",
		"no write: QDir::entryList over one directory. The template store is "
		"files outside the project, so no journal checkpoint is involved",
		""),
	R("controller.soft_takeover", RC::Snapshot, false,
		"the flag is serialized in the project (MidiController::saveSettings "
		"writes the 'softtakeover' attribute into the model's own <connection> "
		"element), but the ControllerConnection is not a JournallingObject: no "
		"checkpoint in the ProjectJournal holds the previous flag, so there is "
		"no live object to restore and control.undo is REFUSED, typed",
		"the same command with the previous 'enabled' value restores it: "
		"controller.surface_state reports the current flag before the write, "
		"which is how a caller records the bounded state it can replay",
		"controller.soft_takeover with 'enabled': false (or 'true'), reading "
		"the current value from controller.surface_state first: the flag is a "
		"scalar outside every journal checkpoint, so the inverse is a manual "
		"command rather than an automatic undo"),
	R("controller.feedback", RC::Snapshot, false,
		"the flag is serialized in the project (the 'feedback' attribute of "
		"MidiController::saveSettings), and enabling it also moves the port to "
		"Duplex output mode, which MidiPort::saveSettings serializes. No "
		"ProjectJournal checkpoint holds the previous flag or the previous "
		"port mode, so control.undo is REFUSED, typed",
		"the same command with the previous 'enabled' value restores the flag; "
		"controller.surface_state reports the current flag and the port's "
		"output counters before and after",
		"controller.feedback with 'enabled': false (or 'true'), reading the "
		"current value from controller.surface_state first: the flag and the "
		"port mode are scalars outside every journal checkpoint"),
	R("controller.template_save", RC::Snapshot, false,
		"writes a JSON file under the user config dir "
		"(ControllerSurface::templateDirectory()), outside the project and "
		"outside every journal checkpoint. An existing file of the same name "
		"is overwritten and its previous revision is not kept",
		"the file is the recorded state; the inverse is controller.template_"
		"delete, which removes it, and controller.template_apply, which "
		"re-binds the named set on another project",
		"controller.template_delete with the same 'name' (or, to put an "
		"earlier mapping set back, re-run controller.template_save from a "
		"project that still has it): a file outside the project has no undo"),
	R("controller.template_apply", RC::Snapshot, false,
		"creates a MidiController and a ControllerConnection per resolved "
		"binding and installs each on its target model - project state. The "
		"connections are serialized through their models' own saveSettings, "
		"which no ProjectJournal checkpoint owns, so there is no inverse "
		"command the engine can replay and control.undo is REFUSED, typed",
		"the bindings are bounded and named: the result reports them and "
		"controller.surface_state reports the whole resulting surface, which "
		"is the recorded state a caller replays by hand",
		"unbind each created target by re-applying the previous template, or "
		"by loading a project saved before the apply: the connection list is "
		"model-owned state outside the journal"),
	R("controller.template_delete", RC::Snapshot, false,
		"removes a template file from the user config dir. The file it "
		"removes is not recoverable from the project - it is outside every "
		"journal checkpoint",
		"the deletion is bounded to one named file; no other template is "
		"touched",
		"re-create the template with controller.template_save from a project "
		"that still has those bindings: a deleted file outside the project "
		"has no undo"),
};

constexpr int kControllerRowCount =
	static_cast<int>(sizeof(kControllerRows) / sizeof(kControllerRows[0]));

} // namespace

const ReversibilityRow* reversibilityControllerRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kControllerRowCount; }
	return kControllerRows;
}

} // namespace control
} // namespace lmms
