/*
 * ControlReversibilityTableMidiReconnect.cpp - the MIDI controller
 *   auto-reconnection group's recorded-action rows (0.3.0 feature-list row 18,
 *   OWNER-31 item 7).
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

#include "ControlReversibility.h"

/*!
 * The midi.reconnect_set row (OWNER-31 item 7: the engine remembers a
 * controller assignment by identity and re-establishes it when the device comes
 * back). It is a true_inverse whose inverse is a RECORDED ACTION, which is why
 * it lives beside the chain-preset rows in the action half rather than in the
 * live-checkpoint file:
 *
 * The binding set is a property of the MidiPort (its readablePorts /
 * writablePorts map, serialized into the project as the <midiport> element's
 * inports/outports attribute), and MidiPort::subscribeReadablePort is the one
 * call that maintains it. A checkpoint of the surrounding Track could restore
 * the ATTRIBUTE - the track's XML carries the element - but not the live
 * subscription, because MidiPort::loadSettings only ever SUBSCRIBES the ports a
 * restored element names and never detaches one it does not. So the command
 * records a step that goes through the same call the write uses, which moves
 * both halves together; that is the whole argument for this class.
 *
 * The three read-only ids of the group (midi.reconnect_status, midi.clients_list,
 * midi.reconnect_arm) are not_mutating rows in
 * ControlReversibilityTablePassive.cpp.
 */

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

const ReversibilityRow kMidiReconnectRows[] = {
	R("midi.reconnect_set", RC::TrueInverse, true,
		"the binding set is a property of the engine's MIDI port (the map the "
		"project serializes as <midiport inports>/<midiport outports>), and this "
		"command adds or removes ONE entry of it",
		"action checkpoint: the recorded undo step binds or detaches through "
		"MidiPort::subscribeReadablePort/subscribeWritablePort - the same call the "
		"write uses, and the call that maintains both the live subscription and the "
		"attribute the project serializes - one step for one command, with the "
		"direction and the name the transaction's before-state holds",
		""),
};

constexpr int kMidiReconnectRowCount =
	static_cast<int>(sizeof(kMidiReconnectRows) / sizeof(kMidiReconnectRows[0]));

} // namespace

const ReversibilityRow* reversibilityMidiReconnectRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kMidiReconnectRowCount; }
	return kMidiReconnectRows;
}

} // namespace control
} // namespace lmms
