/*
 * ControlReversibilityTableArranger.cpp - the 0.5.0 L04 arranger commands' rows of the SPEC A16
 *     classification table (marker.*, range.*, clip edits), joined into reversibilityRowTable()
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

const ReversibilityRow kArrangerRows[] = {
	R("marker.list", RC::NotMutating, false, "it reads the project's markers. Nothing is written", "no write", ""),
	R("marker.jump", RC::NotMutating, false,
		"it moves the play position to a marker (transport.seek): engine run state, not project state", "no write", ""),
	R("marker.add", RC::TrueInverse, true,
		"markers are saved with the project; the list is not a JournallingObject but it is bounded (512 markers)",
		"action checkpoint: the recorded undo step puts the previous marker list back", ""),
	R("marker.set", RC::TrueInverse, true, "the same bounded marker list",
		"action checkpoint: the recorded undo step puts the previous marker list back", ""),
	R("marker.remove", RC::TrueInverse, true, "the same bounded marker list",
		"action checkpoint: the recorded undo step puts the previous marker list back", ""),
	R("range.set", RC::NotMutating, false, "it selects a time range: interface state, not saved", "no write", ""),
	R("range.get", RC::NotMutating, false, "it reads the range. Nothing is written", "no write", ""),
	R("range.clear", RC::NotMutating, false, "it clears the range: interface state", "no write", ""),
	R("range.copy", RC::NotMutating, false, "it copies clips to the range clipboard; the project does not change", "no write", ""),
	R("range.split", RC::TrueInverse, true, "it splits clips on every track the range covers",
		"Track checkpoints: every covered track was checkpointed before the edit, as one step", ""),
	R("range.delete", RC::TrueInverse, true, "it removes clips (and with ripple moves later ones) on every covered track",
		"Track checkpoints: every covered track was checkpointed before the edit, as one step", ""),
	R("range.crop", RC::TrueInverse, true, "it removes clips outside the range on every covered track",
		"Track checkpoints: every covered track was checkpointed before the edit, as one step", ""),
	R("range.insert_time", RC::TrueInverse, true, "it moves clips later on every covered track",
		"Track checkpoints: every covered track was checkpointed before the edit, as one step", ""),
	R("range.paste", RC::TrueInverse, true, "it creates clips on the covered tracks",
		"Track checkpoints: every covered track was checkpointed before the edit, as one step", ""),
	R("range.duplicate", RC::TrueInverse, true, "it creates clips after the range on the covered tracks",
		"Track checkpoints: every covered track was checkpointed before the edit, as one step", ""),
	R("clip.gain_envelope_get", RC::NotMutating, false, "it reads a clip's gain envelope. Nothing is written", "no write", ""),
	R("clip.controllers_get", RC::NotMutating, false, "it reads a clip's controller lanes. Nothing is written", "no write", ""),
	R("clip.controllers_set", RC::TrueInverse, true, "it replaces one controller lane of a MIDI clip", "MidiClip journal checkpoint", ""),
	R("clip.gain_envelope_set", RC::TrueInverse, true,
		"the envelope is part of the clip's edits, which Clip::saveClipEdits writes onto the clip's element",
		"Clip journal checkpoint: undo restores the clip's previous edits", ""),
	R("clip.set_transpose", RC::TrueInverse, true,
		"the transpose is part of the clip's edits (and, for a sample clip, its stretch mode), both on the clip's element",
		"Clip journal checkpoint: undo restores the clip's previous edits and stretch mode", ""),
	R("clip.repeat", RC::TrueInverse, true, "it adds copies of a clip on its track",
		"Track journal checkpoint: undo removes every copy", ""),
	R("clip.time_scale", RC::TrueInverse, true, "it rewrites a clip's notes (or its warp) and its length",
		"Clip journal checkpoint: undo restores the clip's notes (or warp) and length", ""),
	R("range.consolidate", RC::NotMutating, false,
		"it runs clip.consolidate over the range on each covered sample track; each of those records - and undoes - "
		"as itself (the macro.run precedent), so this command records nothing of its own",
		"nothing of its own: undo each track's clip.consolidate", ""),
	R("arrangement.nudge", RC::NotMutating, false,
		"it runs clip.move on the selected clip, which records itself; nothing of its own is written",
		"nothing of its own: clip.move's Clip checkpoint", ""),
	R("arrangement.select_clip", RC::NotMutating, false, "it selects the next or previous clip: interface state", "no write", ""),
	R("arrangement.modes", RC::NotMutating, false,
		"it sets the arranger's editing preferences (ripple, overlap, snap to edges), kept in the config file - not "
		"project state", "no write to the project (set the previous modes)", ""),
	R("transport.audition_start", RC::NotMutating, false,
		"it plays from a point with the other tracks silenced; the mutes are set with journalling off and are put back "
		"by audition_stop - engine run state, not project state", "no write to the project (transport.audition_stop)", ""),
	R("transport.audition_stop", RC::NotMutating, false, "it ends an audition, restoring what it changed", "no write", ""),
	R("automation.lane_add", RC::TrueInverse, true, "it creates an automation track under a track",
		"action step: undo removes the lane", ""),
	R("automation.add_trim", RC::TrueInverse, true, "it creates a relative automation lane under a track",
		"action step: undo removes the lane", ""),
	R("automation.draw_shape", RC::TrueInverse, true, "it rewrites a parameter's points over a range",
		"AutomationClip journal checkpoint (or, for a clip the call created, an action step removing its track)", ""),
	R("automation.transform", RC::TrueInverse, true, "it moves and rescales a parameter's points",
		"AutomationClip journal checkpoint (or an action step removing a created track)", ""),
	R("automation.thin", RC::TrueInverse, true, "it removes a parameter's redundant points",
		"AutomationClip journal checkpoint (or an action step removing a created track)", ""),
	R("automation.set_curve", RC::TrueInverse, true, "it sets one point's curve, saved on the point's element",
		"AutomationClip journal checkpoint", ""),
	R("automation.last_touched", RC::NotMutating, false, "it reads which parameter was last changed. Nothing is written",
		"no write", ""),
	R("note.scale", RC::TrueInverse, true, "it moves and resizes notes", "MidiClip journal checkpoint", ""),
	R("note.ratchet", RC::TrueInverse, true, "it splits notes into repeats", "MidiClip journal checkpoint", ""),
	R("note.add_many", RC::TrueInverse, true, "it adds notes to a clip", "MidiClip journal checkpoint", ""),
	R("note.restore_timing", RC::TrueInverse, true, "it puts back or forgets the notes' pre-quantise timing", "MidiClip journal checkpoint", ""),
	R("note.list", RC::NotMutating, false, "it reads a clip's notes. Nothing is written", "no write", ""),
	R("note.select_where", RC::NotMutating, false, "selection is view state, as note.select",
		"no transaction: the previous selection is reported in the result", ""),
	R("note.transform_where", RC::TrueInverse, true, "it changes, mutes or removes the matching notes",
		"MidiClip journal checkpoint", ""),
	R("key_names.get", RC::NotMutating, false, "it reads a track's key names. Nothing is written", "no write", ""),
	R("track.input_transform_get", RC::NotMutating, false, "it reads a track's MIDI input transform. Nothing is written", "no write", ""),
	R("track.input_transform_set", RC::TrueInverse, true, "the transform is saved with the track; it is eight integers",
		"action checkpoint: the recorded undo step puts the track's previous transform back", ""),
	R("key_names.set", RC::TrueInverse, true, "key names are saved with the project; the map is bounded (128 keys a track)",
		"action checkpoint: the recorded undo step puts the track's previous key names back", ""),
	R("key_names.preset", RC::TrueInverse, true, "the same per-track key-name map",
		"action checkpoint: the recorded undo step puts the track's previous key names back", ""),
};

#undef R

constexpr int kArrangerRowCount = static_cast<int>(sizeof(kArrangerRows) / sizeof(kArrangerRows[0]));

} // namespace

const ReversibilityRow* reversibilityArrangerRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kArrangerRowCount; }
	return kArrangerRows;
}

} // namespace control
} // namespace lmms
