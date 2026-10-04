/*
 * ControlReversibilityTableClapInstrument.cpp - the A16 rows of the CLAP
 *                                              instrument path (feature row 79)
 *                                              and of the 0.5.0 instrument verbs
 *                                              (MX-112, MX-114, MX-120)
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

const ReversibilityRow kClapInstrumentRows[] = {
	// =====================================================================
	// Feature row 79, board task #669: the CLAP note path and the CLAP
	// instrument's audio-output configuration, observable through
	// `plugin.host_notes`. The two ids the feature makes drivable are
	// plugin.list (format=clap lists the CLAP classes, instruments included)
	// and plugin.load (loads one onto an instrument track); both already have
	// A16 rows of their own - plugin.list's not_mutating row in
	// ControlReversibilityTableLive.cpp and plugin.load's snapshot row in
	// ControlReversibilityTableSnapshot.cpp - so this file carries the one
	// id the feature ADDS, and it does not duplicate either.
	// =====================================================================
	R("plugin.host_notes", RC::NotMutating, false,
		"reads the CLAP host path's note route as data: the note input ports "
		"(clap.note-ports) and the audio layout of the CLAP plug-in loaded last, and the "
		"note events the MIDI route pushed, the ones the bounded queue (or a plug-in "
		"with no note input port) refused, the ones put into a plug-in's input event "
		"list and the note-ONs among them",
		"no write: two relaxed-atomic snapshots of counters the audio path increments, "
		"plus the port facts load() recorded in the same counters. Nothing is queued, "
		"nothing is stored, no project state and no journal step is touched, and the "
		"counters are process-wide and reset only when the process exits",
		""),

	// =====================================================================
	// 0.5.0 F2: multi-output and multitimbral instruments (MX-114), instrument
	// layers (MX-112) and per-voice modulation (MX-120). Every mutating verb
	// records ONE action step (control::addUndoStep) that re-resolves its track
	// by trk- id, so a track rebuilt by an earlier undo is found again.
	// =====================================================================
	R("plugin.output_buses", RC::NotMutating, false,
		"reads an instrument's output buses (InstrumentOutputBuses): the host's layout and each extra bus's route",
		"no write: the layout the host reported and the routes' channel ids are read and reported", ""),
	R("plugin.output_bus_route", RC::TrueInverse, true,
		"a route is one channel id per bus, held by the instrument's InstrumentOutputBuses and saved with the track",
		"action checkpoint: the recorded undo step routes the bus back to the channel it had (or nowhere) through "
		"InstrumentOutputBuses::route; a VST3 aux bus the route activated stays active (no audible effect while "
		"unrouted)",
		""),
	R("plugin.output_buses_create_channels", RC::TrueInverse, true,
		"the command creates mixer channels and routes buses to them; neither existed before",
		"action checkpoint: the recorded undo step un-routes every bus it routed and deletes the channels it created "
		"(Mixer::deleteChannel, found by persistent id); the redo creates and routes them again",
		""),
	R("plugin.midi_parts", RC::NotMutating, false,
		"reads an instrument track's MIDI parts (InstrumentMidiParts, a RackZones list)",
		"no write: the part zones are read and reported", ""),
	R("plugin.midi_part_add", RC::TrueInverse, true,
		"a created part has no before-state; the part list is the track's own data",
		"action checkpoint: the recorded undo step drops the part the command appended (RackZones::removeZone), "
		"the shape rack.zone_add uses",
		""),
	R("plugin.midi_part_remove", RC::TrueInverse, true,
		"a removed part is pure data (a zone and a channel)",
		"action checkpoint: the recorded undo step re-inserts the captured part at its own index, so the "
		"first-match order comes back exactly",
		""),
	R("rack.layers", RC::NotMutating, false,
		"reads an instrument track's layer rack (InstrumentLayers): its layers and zones",
		"no write: the layers' instruments and the zone list are read and reported", ""),
	R("rack.layer_add", RC::TrueInverse, true,
		"an added layer is a new private instrument track; it had no before-state",
		"action checkpoint: the recorded undo step removes the layer (and the zone it was given) through "
		"InstrumentLayers::removeLayer; the redo re-creates it from the track state captured after the add",
		""),
	R("rack.layer_remove", RC::TrueInverse, true,
		"a removed layer's whole state is its private track's XML (instrument and plug-in state) plus the zones",
		"action checkpoint: the recorded undo step re-creates the layer from its captured track XML at its index "
		"(InstrumentLayers::insertLayer) and restores the zone list it had (RackZones::assign)",
		""),
	R("rack.layer_zone_add", RC::TrueInverse, true,
		"a created zone has no before-state; the zone list is the layer rack's own data",
		"action checkpoint: the recorded undo step drops the zone the command appended (RackZones::removeZone)", ""),
	R("rack.layer_zone_remove", RC::TrueInverse, true,
		"a removed zone is pure data",
		"action checkpoint: the recorded undo step re-inserts the captured zone at its own index", ""),
	R("modulator.voice_set", RC::TrueInverse, true,
		"the per-voice flag and trigger track are fields of the modulator's source in the modulation layer",
		"action checkpoint: the recorded undo step writes the previous source back (ModulationLayer::setSource) "
		"and re-resolves the modulator's targets from their unmodulated values",
		""),
	R("modulator.instrument_target_set", RC::TrueInverse, true,
		"a created route has no before-state; the parameter keeps the base value it had",
		"action checkpoint: the recorded undo step removes the route the command appended (ModulationLayer::"
		"removeRoute at the same index) and hands the parameter back to its base value",
		""),
};

constexpr int kClapInstrumentRowCount =
	static_cast<int>(sizeof(kClapInstrumentRows) / sizeof(kClapInstrumentRows[0]));

} // namespace

const ReversibilityRow* reversibilityClapInstrumentRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kClapInstrumentRowCount; }
	return kClapInstrumentRows;
}

} // namespace control
} // namespace lmms
