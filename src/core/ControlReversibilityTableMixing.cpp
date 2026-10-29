/*
 * ControlReversibilityTableMixing.cpp - the 0.5.0 L08 / L09 mixing and routing commands' rows of the reversibility table
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
//! A row for a command whose consecutive calls on the same @a target coalesce into one undo step (docs/UNDO-BOUNDS.md).
#define RC(id, cls, rev, reason, mechanism, fallback, target) \
	{ id, cls, reason, mechanism, fallback, rev, target }

const ReversibilityRow kMixingRows[] = {
	R("mixer.send_many", RC::TrueInverse, true, "it makes the same send or routing from several channels",
		"each send's action step, merged by the registry into one", ""),
	R("vca.set_parent", RC::TrueInverse, true, "it nests a group inside another (saved with the project)",
		"action checkpoint: the recorded undo step puts the previous nesting and links back", ""),
	R("vca.set_links", RC::TrueInverse, true, "it chooses which of a group's attributes reach its members",
		"action checkpoint: the recorded undo step puts the previous nesting and links back", ""),
	R("mixer.send_pan", RC::TrueInverse, true, "it pans one send (saved with the project)",
		"action checkpoint: the recorded undo step puts the send's previous pan back", ""),
	R("window.mixer_view", RC::NotMutating, false, "the mixer view's modes and options are interface state (the options "
		"are kept in the ui/mixerview setting), not the project", "no project write (set the previous options to restore)", ""),
	R("window.phase_scope", RC::NotMutating, false, "it opens the stereo phase view on a channel: a display", "no write", ""),
	R("mixer.set_many", RC::TrueInverse, true, "it mutes or solos several channels",
		"action checkpoint: the recorded undo step puts every channel's mute and solo back", ""),
	R("mixer.set_strip", RC::TrueInverse, true, "it changes a channel's own settings (saved with the project)",
		"action checkpoint: the recorded undo step puts the channel's previous strip settings back", ""),
	R("mixer.strip_get", RC::NotMutating, false, "it reads a channel's settings and meters. Nothing is written", "no write", ""),
	// Q050-088: the strip's Console section; a knob dragged on the strip is one gesture, so a run on one channel coalesces.
	RC("mixer.console_strip", RC::TrueInverse, true, "it changes one channel's trim, polarity, EQ and its Console's drive and "
		"glue compressor (saved with the project)", "action checkpoint: the recorded undo step puts the channel's previous "
		"Console section back", "", "channel"),
	// Q050-340: the capture channels' and the armed tracks' input levels.
	R("meter.inputs", RC::NotMutating, false, "it reads the input levels (the agent's own maxima). Nothing is written",
		"no write", ""),
	R("window.input_meters", RC::NotMutating, false, "it opens the Input Meters window: a display", "no write", ""),
	// Q050-339: a track's or channel's manual delay.
	R("pdc.set_offset", RC::TrueInverse, true, "it changes a track's or a mixer channel's manual delay (saved with the project)",
		"action checkpoint: the recorded undo step puts the previous offset back", ""),
	R("mixer.rename_channel", RC::TrueInverse, true, "it renames a mixer channel (saved with the project)", "action checkpoint: the recorded undo step puts the previous name back", ""),
	R("mixer.solo_mode", RC::TrueInverse, true,
		"it writes the solo mode, a preference in the config file (ConfigManager is not a JournallingObject) - a bounded "
		"scalar", "action checkpoint: the recorded undo step writes the previous mode back, as settings.set does", ""),
	R("mixer.clear_clips", RC::NotMutating, false, "the clip lights are meter state, not the project", "no project write", ""),
	R("mixer.listen", RC::NotMutating, false, "the listen bus is a monitor switch: interface state, not the project",
		"no project write: mixer.listen with the opposite 'on' is the inverse", ""),
	R("mixer.listen_mode", RC::NotMutating, false, "the listen bus's mode is interface state, not the project",
		"no project write", ""),
	// 0.5.0 F1 (MX-040, MX-041, MX-036, MX-061): hardware I/O.
	R("audio.io_list", RC::NotMutating, false, "it reads the interface's channels and what is routed to them", "no write", ""),
	R("audio.io_setup", RC::TrueInverse, true, "it writes the device's I/O setup, a machine setting in the config file (ConfigManager "
		"is not a JournallingObject)", "action checkpoint: the recorded undo step puts the device's previous I/O setup back", ""),
	R("mixer.hardware_output", RC::TrueInverse, true, "it routes a channel to the hardware outputs (saved with the project)",
		"action checkpoint: the recorded undo step puts the channel's previous hardware output back", ""),
	R("mixer.hardware_get", RC::NotMutating, false, "it reads every channel's hardware side and the cue masters", "no write", ""),
	R("mixer.cue_send", RC::TrueInverse, true, "it sets a channel's send to a cue mix (saved with the project)",
		"action checkpoint: the recorded undo step puts the channel's previous cue send back", ""),
	R("mixer.cue_master", RC::TrueInverse, true, "it sets a cue mix's master or where listening goes (saved with the project)",
		"action checkpoint: the recorded undo step puts every cue master and the listen target back", ""),
	R("mixer.hardware_insert", RC::TrueInverse, true, "it sets up a channel's hardware insert (saved with the project)",
		"action checkpoint: the recorded undo step puts the channel's previous hardware insert settings back", ""),
	R("mixer.hardware_insert_ping", RC::TrueInverse, true, "it measures a hardware insert's latency, which delay compensation "
		"then uses (saved with the project)", "action checkpoint: the recorded undo step puts the previous measured latency back", ""),
	// 0.5.0 F7 (MX-060): surround channels.
	R("mixer.channel_format", RC::TrueInverse, true, "it sets a channel's speaker format (saved with the project)",
		"action checkpoint: the recorded undo step puts the channel's previous format back", ""),
	R("mixer.surround_pan", RC::TrueInverse, true, "it sets a channel's surround panner (saved with the project)",
		"action checkpoint: the recorded undo step puts the channel's previous surround panner back", ""),
	R("mixer.downmix_options", RC::TrueInverse, true, "it sets how surround folds into fewer speakers (saved with the project)",
		"action checkpoint: the recorded undo step puts the previous downmix options back", ""),
	R("mixer.surround_get", RC::NotMutating, false, "it reads every channel's speaker format, panner and outputs", "no write", ""),
};

#undef RC
#undef R

constexpr int kMixingRowCount = static_cast<int>(sizeof(kMixingRows) / sizeof(kMixingRows[0]));

} // namespace

const ReversibilityRow* reversibilityMixingRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kMixingRowCount; }
	return kMixingRows;
}

} // namespace control
} // namespace lmms
