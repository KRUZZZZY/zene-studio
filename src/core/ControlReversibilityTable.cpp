/*
 * ControlReversibilityTable.cpp - THE SPEC A16 classification table, blocks two
 *                                  and three: the snapshot rows, then the rows
 *                                  for the commands that write nothing.
 *
 * This file is data. It is deliberately separate from
 * ControlReversibility.cpp (the machinery) so that the whole contract can be
 * read and reviewed as a table in one place, and so the anti-drift test has
 * exactly one thing to compare against the registry.
 *
 * It holds TWO of the table's three literal blocks. The true_inverse rows - the
 * commands whose inverse is a live checkpoint - are the FIRST block, in
 * ControlReversibilityTableTrueInverse.cpp; the irreversible and the remaining
 * not_mutating rows are the LAST block, in
 * ControlReversibilityTablePassive.cpp. reversibilityRowTable() joins the
 * true_inverse block and this file's two blocks into ONE contiguous array (the
 * rows that have an inverse), and ReversibilityTable's constructor inserts that
 * and then the passive block, in that order. The file split exists because this
 * fork's file-length ratchet measures a file as a unit, not because the contract
 * is three contracts.
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

/*! This file's blocks of the contract table: the snapshot rows, then the rows
 * for the commands that write nothing. The table is completed by the
 * true_inverse block (ControlReversibilityTableTrueInverse.cpp, inserted first)
 * and the passive block (ControlReversibilityTablePassive.cpp, inserted last). */
const ReversibilityRow kRows[] = {

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

	// The browser.* group's two mutating verbs (W8 tag/metadata search). A tag
	// lives in a JSON store in the user's config directory, not in the project
	// (include/BrowserCatalog.h records why), so no JournallingObject carries it
	// and no ProjectJournal checkpoint can restore it.
	R("browser.tag.add", RC::Snapshot, true,
		"the tag store is a user-config file, not a JournallingObject: there "
		"is no object checkpoint that holds a path's tag set, and the project "
		"journal is deliberately not used for state outside the project",
		"the recorded inverse is the paired COMMAND browser.tag.remove with "
		"this path and tag (`applies: command`), which control.undo dispatches "
		"through the registry; the tag set the edit started from is in the "
		"transaction's before-state, bounded by the store's own "
		"distinct-tag cap",
		""),
	R("browser.tag.remove", RC::Snapshot, true,
		"the same store and the same absence of a checkpoint; removing the "
		"LAST tag of a file drops its entry from the store, which a "
		"re-add recreates exactly",
		"the recorded inverse is the paired COMMAND browser.tag.add with this "
		"path and tag (`applies: command`), dispatched by control.undo; the "
		"tag set the edit started from is in the transaction's before-state",
		""),
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
};

constexpr int kRowCount = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));

//! Appends one literal block to the assembled table (see reversibilityRowTable).
void appendBlock(QVector<ReversibilityRow>* rows, const ReversibilityRow* block, int rowCount)
{
	if (rows == nullptr || block == nullptr) { return; }
	for (int i = 0; i < rowCount; ++i) { rows->append(block[i]); }
}

} // namespace

const ReversibilityRow* reversibilityRowTable(int* rowCount)
{
	// The rows that have an inverse (true_inverse and snapshot) are TWO literal
	// blocks: kTrueInverseRows in ControlReversibilityTableTrueInverse.cpp and
	// kRows above. This entry point hands them out as ONE contiguous array, in
	// the order SPEC A16 reads them, so *rowCount is the sum of both blocks and
	// a caller never has to know the table was split. The assembly runs once, on
	// first lookup, and the pointer it returns lives for the process's lifetime -
	// the guarantee a literal array gives.
	static const QVector<ReversibilityRow> kAssembled = []
	{
		int inverseCount = 0;
		const ReversibilityRow* inverse = reversibilityTrueInverseRowTable(&inverseCount);
		QVector<ReversibilityRow> rows;
		rows.reserve(inverseCount + kRowCount);
		appendBlock(&rows, inverse, inverseCount);
		appendBlock(&rows, kRows, kRowCount);
		return rows;
	}();
	if (rowCount != nullptr) { *rowCount = static_cast<int>(kAssembled.size()); }
	return kAssembled.constData();
}

} // namespace control
} // namespace lmms

