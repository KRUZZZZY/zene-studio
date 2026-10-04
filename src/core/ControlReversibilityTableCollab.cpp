/*
 * ControlReversibilityTableCollab.cpp - the collab.* group's rows of the
 *                                       reversibility contract (0.5.0 F6)
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

/*! Every collab.* command is session state, classed the way remote.* and transport.low_latency are: it opens
 *  or closes a connection and writes nothing to the project. What a session then does to the project is
 *  recorded by the commands that do it - a peer's snapshot loads through project.open (its own irreversible
 *  row), and a streamed edit is the edit's own command with its own row. Keeping the group non-mutating also
 *  keeps an invite (which carries the session key) out of the transaction log and the workflow recorder. */

#include "CollabCommands.h"
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

const ReversibilityRow kCollabRows[] = {
	R("collab.host", RC::NotMutating, false, "it starts listening for peers and asks the router to open the port: session "
		"state, not the project", "no project write (collab.stop closes the port and removes the mapping)", ""),
	R("collab.stop", RC::NotMutating, false, "it disconnects the peers and closes the port: session state",
		"no project write (collab.host starts a new session)", ""),
	R("collab.invite", RC::NotMutating, false, "it reads the session's invite for a role. Nothing is written", "no write", ""),
	R("collab.join", RC::NotMutating, false, "it connects to a host: session state; the host's project then loads through "
		"project.open, which records itself (irreversible - save first)", "no write of its own (collab.leave)", ""),
	R("collab.leave", RC::NotMutating, false, "it disconnects from the host; the project stays open as it was",
		"no write (collab.join again with the invite)", ""),
	R("collab.get_state", RC::NotMutating, false, "it reads the session, its peers and the port mapping. Nothing is written",
		"no write", ""),
	R("collab.set_role", RC::NotMutating, false, "it changes what a connected peer may do: session state",
		"no project write (set the previous role)", ""),
	R("collab.resync", RC::NotMutating, false, "it asks for the host's project to be sent again; a peer loads it through "
		"project.open, which records itself", "no write of its own", ""),
	R("collab.show_panel", RC::NotMutating, false, "it shows the collaboration panel: interface state", "no write", ""),
};

#undef R

constexpr int kCollabRowCount = static_cast<int>(sizeof(kCollabRows) / sizeof(kCollabRows[0]));

} // namespace

const ReversibilityRow* reversibilityCollabRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kCollabRowCount; }
	return kCollabRows;
}

} // namespace control
} // namespace lmms
