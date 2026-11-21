/*
 * ControlReversibilityTableOsc.cpp - the osc.* rows of the reversibility contract (Q050-205)
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

/*! The OSC server is device state, classed the way surface.mcu_connect and collab.host are: it opens or
 *  closes a socket and writes nothing to the project. What an OSC request then does records as the command
 *  it runs, with that command's own row (mixer.set_volume stays true_inverse, transport.play not_mutating). */

#include "ControlReversibility.h"
#include "OscCommands.h"

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

const ReversibilityRow kOscRows[] = {
	R("osc.start", RC::NotMutating, false, "it starts listening for OSC requests on a UDP port: device state, not the "
		"project; each request then records as the registered command it runs", "no project write (osc.stop)", ""),
	R("osc.stop", RC::NotMutating, false, "it closes the OSC port: device state", "no project write (osc.start)", ""),
	R("osc.get_state", RC::NotMutating, false, "it reads whether the OSC server runs, where, and its counters. "
		"Nothing is written", "no write", ""),
};

#undef R

constexpr int kOscRowCount = static_cast<int>(sizeof(kOscRows) / sizeof(kOscRows[0]));

} // namespace

const ReversibilityRow* reversibilityOscRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kOscRowCount; }
	return kOscRows;
}

} // namespace control
} // namespace lmms
