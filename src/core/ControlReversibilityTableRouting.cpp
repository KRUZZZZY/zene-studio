/*
 * ControlReversibilityTableRouting.cpp - the routing surface's rows of the
 * SPEC A16 classification table: the pdc / routing / bus / port groups and the
 * mixer group's routing verbs.
 *
 * This file is data. It holds the GROUP's rows, whatever their class, and the
 * class comes from each row rather than from the file - the same arrangement
 * ControlReversibilityTableTrackFolder.cpp uses for the folder-tracks group, and
 * for the same reason: the live-checkpoint block and the snapshot block have both
 * crossed the 500-line file-length ratchet, so a group's rows land together in
 * their own translation unit and reversibilityRowTable() joins them into the one
 * block every consumer reads.
 *
 * What each row argues:
 *   * pdc.report / routing.get_state / bus.list / port.get_state write nothing.
 *   * bus.create's inverse is the OPERATION (a created channel has no
 *     before-state): the mixer.add_channel shape, one action step that deletes
 *     the bus this command created.
 *   * mixer.route_to / mixer.send_to / mixer.sidechain_to / mixer.route_remove
 *     are recorded-ACTION true inverses: a MixerRoute is not a JournallingObject
 *     and the mixer's send lists are not project-journalled state, so the
 *     recorded step deletes the route it created (or writes the amount and the
 *     pre-fader flag / tap point back), which is the route's whole state.
 *   * bus.remove is a SNAPSHOT with no automatic replay, exactly as
 *     mixer.remove_channel is: nothing in this engine creates a channel WITH
 *     state, so control.undo refuses, typed, and names the fallback.
 *   * port.set_pin is a SNAPSHOT whose inverse IS a command: one pin is one bool
 *     in the processor's own <pins> element, the recorded inverse is
 *     port.set_pin with the value captured before the write, and
 *     `control.undo` dispatches it (`applies: command`). There is no
 *     JournallingObject behind an AudioPortsModel, so a live checkpoint is not
 *     available and the bounded-record class is the correct one.
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

const ReversibilityRow kRoutingRows[] = {
	// ---- inspectors (write nothing) ----
	R("pdc.report", RC::NotMutating, false,
		"reads the mixer's published latency graph, the per-edge compensation and the send lists; "
		"it computes nothing and writes nothing - the numbers are the ones "
		"Mixer::updateLatencyCompensation already published",
		"no write",
		""),
	R("routing.get_state", RC::NotMutating, false,
		"reads a target's EffectChain / Rack routing graph - its nodes, connections and cached "
		"processing order; an inspector with no setter in this release, because the engine's "
		"threading contract forbids live topology edits (include/RoutingGraph.h)",
		"no write",
		""),
	R("bus.list", RC::NotMutating, false,
		"reads the mixer's bus channels and their routing; no write",
		"no write",
		""),
	R("port.get_state", RC::NotMutating, false,
		"reads a device's audio-ports model: the pin matrices and the engine's own used-channel "
		"caches; no write",
		"no write",
		""),

	// ---- true_inverse: the recorded ACTION is the inverse ----
	R("bus.create", RC::TrueInverse, true,
		"a created MixerChannel has no before-state, and the mixer's own checkpoint would destroy "
		"and recreate every channel - and a MixerView holds those pointers - so the inverse is the "
		"OPERATION, the mixer.add_channel shape",
		"action checkpoint: ONE recorded step deletes the bus this command created, through the "
		"same Mixer::deleteChannel path bus.remove uses. A fresh bus carries only defaults (its "
		"name, its fader at unity, no sends), so removing it restores the mixer exactly; the "
		"before-state records the channel count. The recorded redo re-creates a bus, so a GUI redo "
		"is faithful",
		""),
	R("mixer.route_to", RC::TrueInverse, true,
		"a MixerRoute is a QObject held by the sender's and the receiver's own lists; neither list "
		"is project-journalled state and the route has no jo_id, so no live checkpoint can carry "
		"one",
		"action checkpoint: a route that did not exist is undone by DELETING it (a new route is its "
		"endpoints plus the amount this command set); a route that did exist is undone by writing "
		"the captured amount and pre-fader flag back through the route's own models. Either way it "
		"is ONE step, so one agent command is one Ctrl+Z",
		""),
	R("mixer.send_to", RC::TrueInverse, true,
		"same object as mixer.route_to - the engine has ONE edge type and an auxiliary send is that "
		"edge with an amount; the send lists are not journalled",
		"action checkpoint: the recorded step deletes the send this command created, or writes the "
		"captured amount and pre-fader flag back when the send already existed",
		""),
	R("mixer.sidechain_to", RC::TrueInverse, true,
		"a MixerSidechainRoute is not a JournallingObject either, and its intermediate buffers "
		"belong to the audio path",
		"action checkpoint: the recorded step deletes the sidechain send this command created, or "
		"writes the captured amount and tap point back. The before-state also records whether the "
		"route was deferred, because a deferred route is one that closes a cycle through a regular "
		"send and the engine derives that flag, never the caller",
		""),
	R("mixer.route_remove", RC::TrueInverse, true,
		"a removed route has no live object behind it; the route's whole state is its endpoints plus "
		"two numbers (amount and pre-fader flag) or, for a sidechain send, the amount and the tap "
		"point",
		"action checkpoint: the recorded step re-creates the send through the same "
		"Mixer::createChannelSend / Mixer::createSidechainSend call with the captured parameters, "
		"as ONE step. LIMIT: a sidechain route that was DEFERRED is deferred again only while the "
		"cycle that made it deferred still exists",
		""),

	// ---- snapshot: a bounded recorded state ----
	R("bus.remove", RC::Snapshot, false,
		"the bus's full state is in the transaction's before-state, but no command recreates a "
		"channel WITH state: bus.create (like mixer.add_channel) always makes a default channel, so "
		"an automatic replay would restore the index and nothing else",
		"bounded container snapshot: before-state holds the removed bus's name, gain, mute/solo, "
		"incoming and outgoing sends and its effect chain, so the state is not lost - only the "
		"automatic replay is missing. The same class mixer.remove_channel has, for the same reason",
		"create a bus with bus.create and re-apply the name, gain and sends from before; the effects "
		"must be loaded again with plugin.load and their state XML re-applied"),
	R("port.set_pin", RC::Snapshot, true,
		"one pin is one bool in the processor's own <pins> element - a bounded recorded state - but "
		"there is no JournallingObject behind an AudioPortsModel (Model is a QObject and "
		"AudioPortsModel is a SerializingObject, not a JournallingObject), so no live checkpoint "
		"exists",
		"bounded recorded state replayed by an INVERSE COMMAND: the transaction records the pin's "
		"previous value and the recorded inverse is port.set_pin again with it (`applies: command`, "
		"which control.undo dispatches through the registry). The write itself is the engine's own "
		"pin path (AudioPortsModel::Matrix::setPin), the call the PinConnector view makes",
		""),
};

constexpr int kRoutingRowCount =
	static_cast<int>(sizeof(kRoutingRows) / sizeof(kRoutingRows[0]));

} // namespace

const ReversibilityRow* reversibilityRoutingRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kRoutingRowCount; }
	return kRoutingRows;
}

} // namespace control
} // namespace lmms
