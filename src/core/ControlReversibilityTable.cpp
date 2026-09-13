/*
 * ControlReversibilityTable.cpp - THE SPEC A16 classification table: one row
 *                                  for every command the control surface
 *                                  registers, and the reason for its class.
 *
 * This file is data. It is deliberately separate from
 * ControlReversibility.cpp (the machinery) so that the whole contract can be
 * read and reviewed as a table in one place, and so the anti-drift test has
 * exactly one thing to compare against the registry.
 *
 * It holds the FIRST of the table's two literal blocks - the rows that have an
 * inverse (true_inverse, then snapshot). The rows that have none (irreversible,
 * then not_mutating) are the second block, in
 * ControlReversibilityTablePassive.cpp, and ReversibilityTable's constructor
 * reads both: the file split exists because this fork's file-length ratchet
 * measures a file as a unit, not because the contract is two contracts.
 *
 * Reconciled against ableton-gap/A16-STATUS-MEASURED.md (the parent's measured
 * baseline of 36 exercised mutating commands, 17 reversible:true). The rows
 * where this table DISAGREES with that measurement are marked "DISAGREEMENT"
 * and the doc that ships beside this code (docs/A16-REVERSIBILITY.md) records
 * the argument for each.
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

// lmmsconfig.h carries the ZENE_TELEMETRY_ENABLED packager-kill-switch define.
// ControlReversibility.h does not pull it in, and this file's rows are guarded
// by that switch, so it has to be included explicitly - without it the #ifdef
// below reads "off" even in a telemetry-enabled build and silently drops the
// two telemetry.* rows the registry does declare.
#include "lmmsconfig.h"

namespace lmms
{
namespace control
{

namespace
{

using RC = ReversibilityClass;

//! A literal row: R(id, class, reversible, reason, mechanism, fallback).
#define R(id, cls, rev, reason, mechanism, fallback) \
	{ id, cls, reason, mechanism, fallback, rev }

/*! The contract table. Order: the three classes of SPEC A16 first (true
 * inverse, snapshot, irreversible), then the commands that write nothing. */
const ReversibilityRow kRows[] = {

	// =====================================================================
	// true_inverse - a live checkpoint on the engine's own ProjectJournal
	// restores it. Either a plain object checkpoint, a COMPOSITE checkpoint
	// (several objects, one undo step) or an ACTION checkpoint (a recorded
	// operation, for a created or deleted object that has no live state to
	// restore). undo() pops all of these through one ProjectJournal.
	// =====================================================================
	R("transport.set_tempo", RC::TrueInverse, true,
		"one scalar on the Song, which is a JournallingObject",
		"ProjectJournal (Song checkpoint): Song::saveState carries the tempo",
		""),
	R("transport.seek", RC::TrueInverse, true,
		"the play head is engine state, not project state, and is not a "
		"JournallingObject - so no object checkpoint exists for it",
		"action checkpoint: the recorded undo step calls Song::setPlayPos with "
		"the tick the transaction's before-state holds",
		""),
	R("track.rename", RC::TrueInverse, true,
		"the name is part of the Track's own serialized state",
		"ProjectJournal (Track checkpoint)",
		""),
	R("track.set_mute", RC::TrueInverse, true,
		"the muted flag is a BoolModel, i.e. a JournallingObject of its own",
		"ProjectJournal (Track mute BoolModel checkpoint)",
		""),
	R("track.set_solo", RC::TrueInverse, true,
		"the action is NOT one object: the solo flag rides the solo BoolModel "
		"and Track::toggleSolo (driven from the solo model's dataChanged) "
		"writes EVERY other track's mute. DISAGREEMENT with "
		"A16-STATUS-MEASURED.md, which records this as reversible:false "
		"(\"partial: one checkpoint covers one object\")",
		"COMPOSITE checkpoint: every track of the song container is snapshotted "
		"as ONE checkpoint (ProjectJournal::addJournalCheckPoint(QVector)), so "
		"one control.undo and one Ctrl+Z restore the solo flag and every mute "
		"together. This is SPEC A16 deliverable 3",
		""),
	R("track.add", RC::TrueInverse, true,
		"a created Track has no before-state to restore, so the inverse is the "
		"operation, not a snapshot. DISAGREEMENT with A16-STATUS-MEASURED.md, "
		"which records this as reversible:false (no checkpoint exists)",
		"action checkpoint: the recorded undo step removes the created track "
		"through the product's own TrackContainerView::deleteTrackView path "
		"(the path whose checkpoint upstream left commented out); a fresh "
		"instrument track carries only defaults, so removing it restores the "
		"container exactly. The project-scoped next-id counter is monotonic and "
		"is NOT rewound: a re-add gets a fresh trk-<n>, which is the documented "
		"limit of this inverse",
		""),
	R("track.remove", RC::TrueInverse, true,
		"a deleted JournallingObject's journal id resolves to nullptr, so a "
		"checkpoint cannot bring it back. DISAGREEMENT with "
		"A16-STATUS-MEASURED.md, which records this as reversible:false",
		"action checkpoint: the track's own XML (Track::saveState into a "
		"JournalData DataFile) is captured before the delete and the recorded "
		"undo step recreates it with Track::create(element, song) - the same "
		"call TrackContainer::loadSettings makes. The capture is bounded like "
		"the device-state snapshot (64 KiB)",
		""),
	R("clip.add", RC::TrueInverse, true,
		"the clip is created inside a Track, and a Track checkpoint carries "
		"every clip it holds",
		"ProjectJournal (Track checkpoint)",
		""),
	R("clip.delete", RC::TrueInverse, true,
		"same container: the clip list is part of the Track's serialized state",
		"ProjectJournal (Track checkpoint)",
		""),
	R("clip.duplicate", RC::TrueInverse, true,
		"the duplicate is a second clip in the same Track",
		"ProjectJournal (Track checkpoint)",
		""),
	R("clip.move", RC::TrueInverse, true,
		"position is a Clip property and the Clip is a JournallingObject",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("clip.resize", RC::TrueInverse, true,
		"length is a Clip property",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("clip.split", RC::TrueInverse, true,
		"the split rewrites the clip list of one Track",
		"ProjectJournal (Track checkpoint)",
		""),
	R("clip.set_fade", RC::TrueInverse, true,
		"the two fade ramps are part of the Clip's own serialized state "
		"(Clip::saveClipEdits writes them onto the clip's element), so the Clip "
		"checkpoint the command takes is a live inverse of both",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("clip.set_gain", RC::TrueInverse, true,
		"the clip gain is part of the same Clip serialized state as the fades",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("clip.crossfade", RC::TrueInverse, true,
		"the command writes TWO clips, and it refuses unless both are on the same "
		"track - which is what lets one Track checkpoint cover the pair. Its "
		"recorded inverse op is named as the manual one (no single command "
		"restores two clips' fades) and control.undo's default 'journal' "
		"mechanism unwinds the checkpoint instead, exactly as clip.split does",
		"ProjectJournal (Track checkpoint)",
		""),
	R("note.add", RC::TrueInverse, true,
		"the note list belongs to the MidiClip, which is a JournallingObject",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("note.remove", RC::TrueInverse, true,
		"same clip-owned list",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("note.move", RC::TrueInverse, true,
		"note position is part of the clip's saved note list",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("note.resize", RC::TrueInverse, true,
		"note length is part of the clip's saved note list",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("note.velocity_set", RC::TrueInverse, true,
		"note volume is part of the clip's saved note list",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("mixer.set_volume", RC::TrueInverse, true,
		"the fader is a FloatModel, i.e. a JournallingObject",
		"ProjectJournal (MixerChannel volume model checkpoint)",
		""),
	R("mixer.add_channel", RC::TrueInverse, true,
		"a created MixerChannel has no before-state; the mixer is a "
		"JournallingObject but restoring its checkpoint would destroy and "
		"recreate every channel, and a MixerView holds those pointers - the "
		"same GUI-safety reason the TrackContainer checkpoints are commented "
		"out upstream. DISAGREEMENT with A16-STATUS-MEASURED.md, which records "
		"this as reversible:false",
		"action checkpoint: the recorded undo step deletes the channel it "
		"created (Mixer::deleteChannel), through the same code path "
		"mixer.remove_channel uses; a fresh channel carries only defaults, so "
		"removing it restores the mixer exactly",
		""),
	R("automation.add_point", RC::TrueInverse, true,
		"the point lives on an AutomationClip, which is a JournallingObject - "
		"EXCEPT on the call that has to create the AutomationClip first",
		"ProjectJournal (AutomationClip checkpoint); on the creating call, an "
		"action checkpoint that removes the automation track the command "
		"created, so the first point is one undoable step like every later one. "
		"DISAGREEMENT with A16-STATUS-MEASURED.md, which records the creating "
		"call as reversible:false",
		""),
	R("automation.remove_point", RC::TrueInverse, true,
		"the point list is the clip's own state",
		"ProjectJournal (AutomationClip checkpoint)",
		""),
	R("automation.clear", RC::TrueInverse, true,
		"the point list is the clip's own state",
		"ProjectJournal (AutomationClip checkpoint)",
		""),
	R("warp.add", RC::TrueInverse, true,
		"the marker set is the SampleClip's own serialized state - the additive "
		"<warp> child element #597 writes",
		"ProjectJournal (Clip checkpoint): SampleClip::saveSettings writes the "
		"<warp> element and SampleClip::loadSettings re-reads it, so the "
		"checkpoint taken before the first marker was added restores an "
		"unwarped clip",
		""),
	R("warp.move", RC::TrueInverse, true,
		"a marker's timeline offset is part of the same serialized map",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("warp.remove", RC::TrueInverse, true,
		"removing a marker rewrites the same <warp> element, and removing the "
		"last one leaves an empty map the checkpoint restores exactly",
		"ProjectJournal (Clip checkpoint)",
		""),
	R("warp.set", RC::TrueInverse, true,
		"one call may write the marker list, the tempo mode and the source "
		"tempo, and all three are fields of the same serialized <warp> element",
		"ProjectJournal (Clip checkpoint): the three fields travel as ONE "
		"checkpoint, so one control.undo restores the whole warp map",
		""),
	R("plugin.bypass", RC::TrueInverse, true,
		"the On/Off control is the Effect's enabled model",
		"ProjectJournal (Effect enabled-model checkpoint)",
		""),
	R("plugin.param_set", RC::TrueInverse, true,
		"the parameter is an AutomatableModel, i.e. a JournallingObject",
		"ProjectJournal (parameter model checkpoint)",
		""),
	R("plugin.state_load", RC::TrueInverse, true,
		"the load replaces the device's settings, and the device's PREVIOUS "
		"settings are captured in the transaction's before-state",
		"action checkpoint: the recorded undo step restores the captured "
		"state XML through the same controlRestoreDeviceState path "
		"plugin.state_load itself uses",
		""),
	R("plugin.preset_load", RC::TrueInverse, true,
		"same as plugin.state_load: the preset replaces the device's "
		"parameters and the previous settings are captured first",
		"action checkpoint: the recorded undo step restores the captured "
		"state XML",
		""),
	R("settings.set", RC::TrueInverse, true,
		"ConfigManager is not a JournallingObject, so there is no object "
		"checkpoint - but the previous value is a bounded scalar",
		"action checkpoint: the recorded undo step writes the previous value "
		"back and saves the config file, exactly as the command does",
		""),
	R("export.set_dither", RC::TrueInverse, true,
		"ExportRenderSettings is not a JournallingObject, so there is no object "
		"checkpoint - but the choice is a bounded scalar (on/off) and the "
		"previous value is captured before the write",
		"action checkpoint: the recorded undo step restores the previous dither "
		"choice through ExportRenderSettings::setDither, exactly as the command "
		"sets the new one. The step goes on the engine's own stack, so a user's "
		"Ctrl+Z and control.undo are one history",
		""),
	R("export.set_src_quality", RC::TrueInverse, true,
		"same shape as export.set_dither: a bounded enum owned by "
		"ExportRenderSettings rather than by a project object, with the previous "
		"converter selection captured before the write",
		"action checkpoint: the recorded undo step restores the previous "
		"SrcQuality through ExportRenderSettings::setSrcQuality, exactly as the "
		"command sets the new one",
		""),
	R("audio.device_set", RC::TrueInverse, true,
		"the preference is a scalar in the config file, not in the project",
		"action checkpoint: the recorded undo step writes the previous device "
		"name back and saves the config file",
		""),
	R("project.restore_revision", RC::TrueInverse, true,
		"a revision restore is a file write; before it runs, the file it "
		"replaces is rotated into the same bounded revision set",
		"action checkpoint: the recorded undo step restores the revision the "
		"restore replaced",
		""),
#ifdef LMMS_HAVE_SESSION_VIEW
	// The session.* group: SessionModel is not a JournallingObject, so each edit
	// captures the whole <session> block and records ONE action checkpoint that
	// restores it (the true_inverse form clip.split and track.add use).
	R("session.set_grid", RC::TrueInverse, true,
		"grid dimensions live in the <session> block, which is not a JournallingObject", "action checkpoint: the captured <session> XML is restored whole through SessionModel::restoreState, so dimensions, slots and scenes come back together", ""),
	R("session.set_quantisation", RC::TrueInverse, true,
		"one field of the <session> block (launchquantisation)", "action checkpoint: the captured <session> block is restored whole", ""),
	R("session.set_scene", RC::TrueInverse, true,
		"a scene's name and its tempo / time-signature overrides live in the <session> block", "action checkpoint: the block is restored whole, so an override that was OFF before is OFF again rather than merely zeroed", ""),
	R("session.set_slot", RC::TrueInverse, true,
		"a clip slot's reference and launch settings are cells of the <session> block and have no journalled object behind them", "action checkpoint: the block is restored whole, so the slot's previous reference kind, launch mode and playback settings return together", ""),
	R("session.clear_slot", RC::TrueInverse, true,
		"clearing a cell destroys a reference id or an audio source path that no live object holds a copy of", "action checkpoint: the captured <session> block is the only place the cleared reference still exists, and it is restored", ""),
	R("session.clear", RC::TrueInverse, true,
		"it empties every cell, every scene override and the global quantisation at once, on a model the engine does not journal", "action checkpoint: ONE control.undo restores the whole session, so clearing a grid is one undoable step rather than one per cell", ""),
#endif // LMMS_HAVE_SESSION_VIEW

	// ---- racks (#599): the chains, the selector, the macros and the zones ----
	R("rack.add_chain", RC::TrueInverse, true,
		"a created chain has no before-state to restore; the rack is a member "
		"of the MixerChannel, not a JournallingObject, so no object checkpoint "
		"exists for it",
		"action checkpoint: the recorded undo step removes the chain the "
		"command created, through the same Rack::removeChain rack.remove_chain "
		"uses, so the rack is exactly as it was - a fresh chain carries no "
		"effects",
		""),
	R("rack.set_selected", RC::TrueInverse, true,
		"the selection is a scalar the rack owns and the rack does not journal, "
		"so the inverse is the operation rather than an object checkpoint",
		"action checkpoint: the recorded undo step calls Rack::setSelectedChain "
		"with the previous selection the transaction's before-state holds",
		""),
	R("rack.macro_add", RC::TrueInverse, true,
		"a created macro has no before-state; the macro list lives on the rack, "
		"which is not a JournallingObject",
		"action checkpoint: the recorded undo step removes the macro the "
		"command created (RackMacros::removeMacro). A new macro carries only "
		"its name and its value, so removing it restores the list exactly",
		""),
	R("rack.macro_remove", RC::TrueInverse, true,
		"a removed macro has no live object behind it, and the rack journals "
		"nothing",
		"action checkpoint: the recorded undo step re-inserts the captured "
		"macro - name, value and the whole target list - at its own index "
		"(RackMacros::insertMacro), so the list and the macro-<n> ids come back "
		"exactly",
		""),
	R("rack.macro_target_add", RC::TrueInverse, true,
		"a target is the macro's own data: no model is created or destroyed by "
		"binding one",
		"action checkpoint: the recorded undo step removes the target the "
		"command appended, at the same index",
		""),
	R("rack.macro_target_remove", RC::TrueInverse, true,
		"same list; a removed target has no live object behind it",
		"action checkpoint: the recorded undo step re-inserts the captured "
		"target at its own index, so the bind order - which is the order the "
		"targets are applied in - comes back exactly",
		""),
	R("rack.macro_set", RC::TrueInverse, true,
		"the call writes MORE THAN ONE object: the macro's own scalar, which "
		"lives on the rack and is not journalled, plus every parameter model it "
		"drives. A model checkpoint alone would leave the macro's value behind, "
		"and the macro alone is not a JournallingObject",
		"action checkpoint: ONE recorded undo step puts the macro's value back "
		"and writes every parameter the call changed back to the value it had. "
		"Each target is re-resolved by chain/effect/parameter name when the "
		"step runs - never a raw device pointer - and a target whose device is "
		"gone is skipped rather than dereferenced",
		""),
	R("rack.zone_add", RC::TrueInverse, true,
		"a created zone has no before-state; the zone list is the rack's own "
		"data",
		"action checkpoint: the recorded undo step drops the zone the command "
		"appended, the same shape rack.add_chain uses, so a later edit to "
		"another zone cannot make the undo eat a zone nobody asked about",
		""),
	R("rack.zone_remove", RC::TrueInverse, true,
		"a removed zone has no live object behind it",
		"action checkpoint: the recorded undo step re-inserts the captured zone "
		"at its own index, so the list - and the order the first-match rule "
		"reads - comes back exactly",
		""),

	// =====================================================================
	// snapshot - no live object can be restored. The inverse is a bounded
	// recorded state: a container snapshot, a file revision, or a scalar.
	// reversible=false means the recorded state is a MANUAL fallback: an undo
	// attempt is refused, typed, and names the fallback rather than
	// pretending.
	// =====================================================================
	R("project.save", RC::Snapshot, true,
		"the engine writes the project file in place and keeps no previous "
		"revision by default. DISAGREEMENT with A16-STATUS-MEASURED.md, which "
		"records this as reversible:false with no file-level inverse",
		"file revision: before the write, the existing file is rotated into a "
		"named retention set (keep-3: <file>.rev0, .rev1, .rev2, each capped at "
		"8 MiB, 24 MiB per project). The recorded inverse is the command "
		"project.restore_revision, which control.undo dispatches. File-level "
		"commands are deliberately NOT put on the GUI undo stack - see "
		"docs/A16-REVERSIBILITY.md - because a file is not project state",
		""),
	R("plugin.load", RC::Snapshot, false,
		"two branches: appending an EFFECT has an exact inverse (the instance "
		"is new and carries only defaults), but REPLACING an instrument does "
		"not - the replaced instrument and its parameter values are gone",
		"the effect branch pushes an action checkpoint that unloads the device "
		"it created (plugin.load of an effect is therefore one undoable step). "
		"The instrument branch records the replaced plugin NAME only and is "
		"NOT reversible: replacing an instrument destroys its state",
		"the instrument branch: reload the previous instrument by dev-<n> "
		"(the transaction's before.plugin names it); its parameter values are "
		"NOT restored, so treat an instrument replacement as destructive"),
	R("mixer.remove_channel", RC::Snapshot, false,
		"the channel's full state is in the transaction's before-state, but "
		"there is no command that recreates a channel WITH state: "
		"mixer.add_channel always makes a default channel",
		"bounded container snapshot: before-state holds the removed channel's "
		"name, gain, mute/solo, sends and effects, so the state is not lost - "
		"only the automatic replay is missing. DISAGREEMENT with "
		"A16-STATUS-MEASURED.md in one respect: the doc lists this command "
		"under \"not exercised\", so its class was unmeasured; this row is the "
		"measurement",
		"recreate the channel with mixer.add_channel and re-apply name, gain, "
		"mute/solo and sends from before; the effects must be loaded again "
		"with plugin.load and their state XML re-applied"),
	R("plugin.state_save", RC::Snapshot, false,
		"the command writes a file OUTSIDE the project; the project is "
		"untouched, and the file's previous revision is recorded in "
		"before.previous_content (bounded)",
		"file-level snapshot: the replaced revision is in "
		"before.previous_content, bounded like every other state snapshot",
		"write before.previous_content back with plugin.state_save, or "
		"delete the file when before.replaced was false"),
	R("plugin.preset_save", RC::Snapshot, false,
		"the command writes a file OUTSIDE the project; the replaced revision "
		"is recorded in before.previous_content (bounded)",
		"file-level snapshot: the replaced revision is in "
		"before.previous_content",
		"write before.previous_content back with plugin.preset_save, or "
		"delete the file when before.replaced was false"),

	R("rack.remove_chain", RC::Snapshot, false,
		"the removed chain's effects and their settings are captured in the "
		"transaction's before-state, but recreating a chain WITH its effects is "
		"not one operation the registry can run - the same defect class "
		"mixer.remove_channel records for a channel, and for the same reason: "
		"the chain carries devices whose instantiation is a plugin load",
		"bounded container snapshot: before-state holds the chain's own state "
		"XML - EffectChain::saveState, the call the project file's <fxchain> is "
		"written by, capped like every other state snapshot at 64 KiB - or, "
		"when it exceeds that bound, only its size and the fact that it was "
		"oversized",
		"rack.add_chain, then plugin.load each device the captured XML names "
		"onto the new chain and plugin.state_load its state. A rack with more "
		"than one parallel chain does NOT get the removed chain's position "
		"back, so a selection that named a later chain has to be set again with "
		"rack.set_selected"),

	// ==============================================================	// writes nothing (or refuses every call) - there is no transaction, and
	// therefore nothing for control.undo to reverse or to be blocked by.
	// =====================================================================
	R("export.get_settings", RC::NotMutating, false,
		"a read of the render settings (dither, SRC quality): it writes "
		"nothing, so there is no transaction and nothing for control.undo to "
		"reverse or to be blocked by",
		"nothing to inverse. export.set_dither and export.set_src_quality are "
		"the writers, and both carry an action checkpoint",
		""),
	R("clip.select", RC::NotMutating, false,
		"selection is control-surface view state: it is not serialized, the "
		"GUI keeps its own copy in QGraphicsItem state, and no engine "
		"checkpoint can hold it",
		"nothing to inverse in the project. The registry records NO transaction "
		"for this command (mutating is false), so a select cannot block or "
		"shadow the undo of a real edit; the previous selection is reported in "
		"the result so a client can restore the view itself",
		""),
	R("note.select", RC::NotMutating, false,
		"same view state, per note",
		"same: no transaction, the previous selection is reported in the result",
		""),
	R("mixer.set_pan", RC::NotMutating, false,
		"declared mutating, but the handler REFUSES every call: this tree has "
		"no pan on a MixerChannel, and inventing one would change the mixer's "
		"serialization format",
		"no write happens, so no transaction is recorded and control.undo is "
		"not blocked by it",
		"none needed: the command is a typed refusal, use track panning "
		"(InstrumentTrack/SampleTrack panningModel) or per-note panning"),
	R("track.set_arm", RC::NotMutating, false,
		"declared mutating, but the handler REFUSES every call: arm state "
		"lives on the prototype MultiTrackRecorder, not on lmms::Track",
		"no write happens, so no transaction is recorded",
		"none needed: the command is a typed refusal"),
	R("automation.mode_set", RC::NotMutating, false,
		"declared mutating, but the handler REFUSES every call: this build has "
		"automation modes in the engine but no way to select or persist one "
		"(docs/KNOWN-LIMITATIONS.md)",
		"no write happens, so no transaction is recorded",
		"use automation.add_point to write a curve instead"),
	R("control.undo", RC::NotMutating, false,
		"it IS the inverse applier: it is the thing that reverses another "
		"command, so classifying it as a command to be reversed would recurse",
		"the engine's ProjectJournal",
		""),
	R("control.redo", RC::NotMutating, false,
		"the inverse of the undo; it re-applies a step, it is not project state",
		"the engine's ProjectJournal",
		""),
	R("control.quit", RC::NotMutating, false,
		"process lifecycle, not a project edit; the response reports whether "
		"unsaved changes were discarded so the caller is not surprised",
		"no project state is written",
		""),
	R("transport.play", RC::NotMutating, false,
		"the transport run state is engine state, not project state, and the "
		"registry has never recorded a transaction for it",
		"nothing to reverse: transport.stop is the operation a client calls, "
		"and it is available directly",
		""),
	R("transport.stop", RC::NotMutating, false,
		"same engine run state",
		"nothing to reverse: transport.play is the operation",
		""),
	R("midi.learn_toggle", RC::NotMutating, false,
		"the armed flag is GUI/engine mode state (MidiLearn's own enabled flag), "
		"not project state: no model, no serialized field and no journal checkpoint "
		"is written, so the registry records no transaction",
		"nothing to reverse: calling midi.learn_toggle again is the operation a "
		"client calls, and setArmed() keeps the Edit menu tick in step",
		""),
#ifdef ZENE_TELEMETRY_ENABLED
	// The two telemetry.* rows travel with the client: with the packager kill
	// switch off the commands are absent from the registry, and this table must
	// hold a row for every registered command and no row for a command that is
	// not registered (ReversibilityContractTest asserts both directions).
	R("telemetry.consent", RC::NotMutating, false,
		"it OPENS A SCREEN, it does not edit the project: the Help menu's "
		"\"Telemetry - what we send...\" action declares it and the menu slot and "
		"the registry handler are one function (openTelemetryConsentScreen), and "
		"the consent record is written by the screen through "
		"Telemetry::saveConsent() when - and only when - the human there clicks "
		"Save, which is the human's act and not a project edit",
		"nothing to reverse, and nothing an agent could reverse: the command "
		"declares `requires: display, human`, so the registry refuses it before "
		"the handler runs and no automated caller can reach it or change the "
		"consent record at all",
		""),
#endif // ZENE_TELEMETRY_ENABLED
	R("render.render", RC::NotMutating, false,
		"it writes an OUTPUT ARTEFACT; the session it renders is not modified "
		"(it serialises to a temp file and removes it)",
		"the project is unchanged; the rendered file is an output, not project "
		"state, and overwriting it is the caller's decision",
		""),

	// ---------- read-only inspectors ----------
	R("app.version", RC::NotMutating, false, "reads the build identity", "no write", ""),
	R("arrangement.get_state", RC::NotMutating, false, "reads the model", "no write", ""),
	R("audio.device_list", RC::NotMutating, false, "reads the device table", "no write", ""),
	R("automation.get_state", RC::NotMutating, false, "reads the model", "no write", ""),
	R("control.commands_list", RC::NotMutating, false, "reads the registry", "no write", ""),
	R("control.ping", RC::NotMutating, false, "liveness probe", "no write", ""),
	R("control.surface_report", RC::NotMutating, false, "reads the menu/toolbar reflection", "no write", ""),
	R("control.transactions", RC::NotMutating, false, "reads the transaction record", "no write", ""),
	R("control.version", RC::NotMutating, false, "reads the version strings", "no write", ""),
	R("dsp.get_state", RC::NotMutating, false, "reads the device chains", "no write", ""),
	R("midi.device_list", RC::NotMutating, false, "reads the MIDI client", "no write", ""),
	R("mixer.get_state", RC::NotMutating, false, "reads the mixer", "no write", ""),
	R("plugin.list", RC::NotMutating, false, "reads the device catalogue", "no write", ""),
	R("plugin.param_get", RC::NotMutating, false, "reads a parameter", "no write", ""),
	R("plugin.preset_list", RC::NotMutating, false, "reads a preset directory", "no write", ""),
	R("project.get_state", RC::NotMutating, false, "reads the project state", "no write", ""),
	R("roll.get_state", RC::NotMutating, false, "reads the note list", "no write", ""),
	R("script.list", RC::NotMutating, false, "reads the scripts directory", "no write", ""),
	R("settings.get", RC::NotMutating, false, "reads one config value", "no write", ""),
#ifdef ZENE_TELEMETRY_ENABLED
	R("telemetry.status", RC::NotMutating, false,
		"reads the consent record and the payload builder", "no write", ""),
#endif // ZENE_TELEMETRY_ENABLED
	R("track.get_state", RC::NotMutating, false, "reads one track", "no write", ""),
	R("track.list", RC::NotMutating, false, "reads the track container", "no write", ""),
	R("transport.get_state", RC::NotMutating, false, "reads the transport", "no write", ""),
	R("warp.list", RC::NotMutating, false, "reads a clip's warp map", "no write", ""),
#ifdef LMMS_HAVE_SESSION_VIEW
	// The session.* launch requests write NO project state - they queue into
	// SessionScheduler exactly like transport.play - so recording a transaction
	// for one would shadow the undo of the real edit underneath it (the defect
	// clip.select's row records).
	R("session.launch_slot", RC::NotMutating, false,
		"the request queues into the scheduler's lock-free queue; the slot's launch state lives on the audio thread and is not project state", "nothing to reverse: session.stop_slot is the operation a client calls, and it is available directly", ""),
	R("session.launch_scene", RC::NotMutating, false,
		"the same queued requests, one per non-empty cell of the row; nothing in the <session> block is written", "nothing to reverse: session.stop_all drops every launched slot and session.stop_slot stops one", ""),
	R("session.stop_slot", RC::NotMutating, false,
		"a stop request is the same engine-state queue; the scheduled stop fires on the audio thread", "nothing to reverse: a stopped slot is relaunched with session.launch_slot", ""),
	R("session.stop_all", RC::NotMutating, false,
		"one atomic reset request; it edits no model and drops only the audio thread's transient slot table", "nothing to reverse: the slots are relaunched from the model, which the reset did not touch", ""),
	R("session.get_state", RC::NotMutating, false,
		"reads the model and the launch engine's atomics", "no write", ""),
#endif // LMMS_HAVE_SESSION_VIEW
=======
};

constexpr int kRowCount = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));

} // namespace

const ReversibilityRow* reversibilityRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kRowCount; }
	return kRows;
}

} // namespace control
} // namespace lmms
