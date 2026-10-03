/*
 * ControlReversibilityTableW3Devices.cpp - the 0.5.0 W3 L10 device commands' rows of the SPEC A16 table
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

const ReversibilityRow kW3DeviceRows[] = {
	R("rack.split", RC::TrueInverse, true, "it sets what each chain of a parallel rack is fed (a band, mid / side, left / right)",
		"action checkpoint: the recorded undo step puts the previous split back", ""),
	R("plugin.param_search", RC::NotMutating, false, "it reads parameter names. Nothing is written", "no write", ""),
	R("plugin.param_rename", RC::TrueInverse, true, "it sets a parameter's alias, an option the device carries",
		"action checkpoint: the recorded undo step puts the device's previous options back", ""),
	R("plugin.randomise", RC::TrueInverse, true, "it sets a device's parameters",
		"journal checkpoints of every parameter model, merged into one step", ""),
	R("plugin.blend", RC::TrueInverse, true, "it sets an effect's parameters", "journal checkpoints of every parameter model, merged into one step", ""),
	R("plugin.fold", RC::TrueInverse, true, "it folds or opens a device's card, an option the device carries",
		"action checkpoint: the recorded undo step puts the device's previous options back", ""),
	R("plugin.options", RC::TrueInverse, true, "it sets a device's compatibility and editor options",
		"action checkpoint: the recorded undo step puts the device's previous options back", ""),
	R("modulator.formula_set", RC::TrueInverse, true, "it gives a modulator a formula (the modulation layer)",
		"action checkpoint: the recorded undo step puts the whole previous modulator back", ""),
	R("modulator.trigger_set", RC::TrueInverse, true, "it makes a modulator restart on a track's notes, or run free",
		"action checkpoint: the recorded undo step puts the whole previous modulator back", ""),
	R("plugin.impulse_load", RC::TrueInverse, true, "it loads an impulse response into a convolution reverb (device state)",
		"action checkpoint: the recorded undo step restores the device's previous settings document", ""),
	R("plugin.delta", RC::NotMutating, false, "it plays a device's delta instead of its output: a monitor switch, not the "
		"project (neither saved nor journalled)", "no write (plugin.delta on:false puts it back)", ""),
	R("plugin.record", RC::NotMutating, false, "it writes a take file from what passes a Recorder insert; the project only "
		"learns the take's path (device state)", "no write to the project (delete the file)", ""),
	R("plugin.recorder_land", RC::TrueInverse, true, "it adds an audio track holding a Recorder's last take",
		"action checkpoint: the recorded undo step removes the track", ""),
	R("clip.process", RC::TrueInverse, true, "it replaces an audio clip's source with the clip printed through a chain's devices",
		"Clip checkpoint", ""),
	R("mixer.console_apply", RC::TrueInverse, true, "it adds the Console device to every channel, changes the ones there, or removes them",
		"action checkpoints (each added device taken out, each removed one recreated) and model checkpoints, merged into one step", ""),
};

#undef R

constexpr int kW3DeviceRowCount = static_cast<int>(sizeof(kW3DeviceRows) / sizeof(kW3DeviceRows[0]));

} // namespace

const ReversibilityRow* reversibilityW3DevicesRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kW3DeviceRowCount; }
	return kW3DeviceRows;
}

} // namespace control
} // namespace lmms
