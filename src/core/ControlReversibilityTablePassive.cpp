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
// The telemetry.* rows are guarded by that switch, so it has to be included
// explicitly here too - without it the #ifdef below reads "off" even in a
// telemetry-enabled build and silently drops the rows the registry declares.
#include "lmmsconfig.h"

/*!
 * The THIRD literal block of THE classification table (see
 * ControlReversibilityTable.cpp for the first, the true_inverse rows, and
 * ControlReversibilityTableSnapshot.cpp for the second, the snapshot rows):
 * the rows for the commands that have no inverse - irreversible (an undo
 * attempt must FAIL, typed, and name the fallback) and not_mutating (nothing is
 * written, so there is nothing to reverse). The files are ONE table assembled
 * by ReversibilityTable's constructor; the split exists because this fork's
 * file-length ratchet reads a file as a unit, and a table that has to be read as
 * a whole is still printed as a whole by control.transactions and by
 * tests/…/ReversibilityContractTest.
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

const ReversibilityRow kPassiveRows[] = {

	// =====================================================================
	// irreversible - no inverse exists in this engine, by nature of the
	// command. An undo attempt must FAIL with the typed 'irreversible' error
	// and name the fallback. Nothing here pretends.
	// =====================================================================
	R("project.open", RC::Irreversible, false,
		"loading a project replaces the whole session, and the engine keeps no "
		"pre-load snapshot: the previous document, including any UNSAVED "
		"edits, is gone",
		"none. The transaction records the previous file path and its sha256 so "
		"the caller can see what was displaced",
		"reopen the file named in before.previous_file; unsaved changes to the "
		"displaced session are LOST - save first (project.save keeps a "
		"revision) if they matter"),
	R("script.run", RC::Irreversible, false,
		"a Lua script mutates the engine through its own bindings; the "
		"registry sees one command and cannot know what the script wrote",
		"none. A script that wants to be undoable must take its own checkpoint "
		"(Lua addCheckPoint()), which ProjectJournal::undo DOES replay - so "
		"the record is honest about what it can and cannot cover",
		"run a script that takes its own checkpoint (Lua addCheckPoint()) "
		"before it edits; control.undo then replays that checkpoint"),
	R("record.recovery_discard", RC::Irreversible, false,
		"refusing the offer REMOVES the journal file, and the offer is the "
		"journal: no engine record holds the bytes it carried, and the project "
		"journal holds nothing for a side file beside a take",
		"none. The transaction's before-state holds the journal's whole contents "
		"(take, state, sample rate, channels, the frame count it recorded, the "
		"project and track it named) so a caller can see exactly what was refused",
		"the take's own WAV is NEVER deleted by this command - it stays on disk at "
		"before.take, which is the fallback: bring it in by hand, and re-create the "
		"offer with record.journal_begin if the crash story is still wanted"),

	// =====================================================================
	// writes nothing (or refuses every call) - there is no transaction, and
	// therefore nothing for control.undo to reverse or to be blocked by.
	// =====================================================================
	R("export.get_settings", RC::NotMutating, false,
		"a read of the render settings (dither, SRC quality): it writes "
		"nothing, so there is no transaction and nothing for control.undo to "
		"reverse or to be blocked by",
		"nothing to inverse. export.set_dither and export.set_src_quality are "
		"the writers, and both carry an action checkpoint",
		""),
	R("link.get_state", RC::NotMutating, false,
		"a read of the session-sync state (whether sync is on, the peers, the "
		"session tempo and revision owner, the shared beat and its phase, this "
		"engine's tempo and phase, and whether announcements can travel at "
		"all): it writes nothing, so there is no transaction",
		"nothing to inverse. The four writers are link.set_enabled, "
		"link.set_quantum, link.set_start_stop_sync and link.set_session_tempo, "
		"and each carries an action checkpoint",
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
		"no project state is written. CODE-8 is the half of this row that is "
		"about the SOCKET rather than the session: the socket is unlinked by a "
		"shutdown hook that ControlServer registers and un-registers with "
		"itself, and by the server's own destructor, so the contract \"the "
		"socket file is removed on exit\" holds on the normal route, on the "
		"last-resort guard's route, and when the owner dies first",
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
	R("midi.retro_capture_arm", RC::NotMutating, false,
		"the armed flag is mode state on the capture object "
		"(std::atomic<bool> m_armed next to the ring, include/RetroMidiCapture.h): no "
		"model, no song and no journal checkpoint is written, so the registry records "
		"no transaction. The command DOES write the config file's midi/retrocapture "
		"key, and only when the mode actually moved; ConfigManager is not a "
		"JournallingObject, so that scalar is the persistent default of the mode, not "
		"project state - the settings.set precedent, whose config write is likewise "
		"outside the project journal",
		"nothing to reverse: calling midi.retro_capture_arm with 'armed': false (or "
		"calling it again) is the operation a client calls, and the Edit > Arm MIDI "
		"Capture action re-reads the real state whenever the menu is opened",
		""),
	R("control.set_undo_coalescing", RC::NotMutating, false,
		"it sets the WINDOW the control surface groups a same-command-same-target run in: control-surface grouping state, not project state. It cannot change, destroy or restore anything already on the stack, and it is reported by control.undo_depth",
		"no write. The window is not carried by any project file; it affects only how LATER commands are grouped, and 0 disables coalescing entirely, which is what reproduces the pre-0.3.0 behaviour exactly",
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
		"(it serialises to a temp file and removes it). A render RANGE "
		"(start_ticks/end_ticks, feature row 71) is an ARGUMENT of this same "
		"command, not a second verb, and it changes nothing about that: the loop "
		"points the range rides are set in the render's own CHILD process, which "
		"then exits, so this instance's timeline is untouched",
		"the project is unchanged; the rendered file is an output, not project "
		"state, and overwriting it is the caller's decision. A render already "
		"performed is not undone by undoing anything here - not by the apply whose "
		"preset shaped its options either (export.preset_apply's row) - so the "
		"fallback for a render made with the wrong span or settings is to render "
		"again",
		""),
	R("render.stems", RC::NotMutating, false,
		"it writes OUTPUT ARTEFACTS - one file per unmuted track - and nothing "
		"else: the export runs in a child process against a serialised copy of "
		"the session, so no project state, clip or model in this instance is "
		"touched and there is nothing for a checkpoint to capture",
		"the project is unchanged; the stem files are outputs, not project state, "
		"and overwriting them is the caller's decision. Its DECLARED BOUND is the "
		"one every render-running command carries: the child blocks the dispatch "
		"thread for up to waitForFinished(600000), so the control surface does "
		"not answer until the export finishes (docs/RENDER-CHILD-WAIT.md:120-126, "
		"docs/KNOWN-LIMITATIONS.md)",
		""),
	R("bounce.in_place", RC::NotMutating, false,
		"it writes an OUTPUT ARTEFACT and nothing else: the render runs in a "
		"child process against a serialised copy of the session with every other "
		"track muted, so no project state, clip or model in this instance is "
		"touched and there is nothing for a checkpoint to capture",
		"the session is unchanged; the bounce file is an output, not project "
		"state - freeze.track / freeze.region are the verbs that record a take",
		""),

	// ---------- read-only inspectors ----------
	R("app.version", RC::NotMutating, false, "reads the build identity", "no write", ""),
	R("arrangement.get_state", RC::NotMutating, false, "reads the model", "no write", ""),
	R("audio.device_list", RC::NotMutating, false, "reads the device table", "no write", ""),
	R("automation.get_state", RC::NotMutating, false, "reads the model", "no write", ""),
	R("control.undo_depth", RC::NotMutating, false,
		"reads the engine's own undo stack - its depth, the two caps it is bounded by, the serialised bytes it retains, how many steps a bound has evicted, and the coalescing window - and writes nothing",
		"no write",
		""),
	R("comp.get_state", RC::NotMutating, false,
		"reads the take lanes and the composite of one track, plus what each "
		"segment resolves to (the take clip and its source frame); it writes "
		"nothing and the audio of no take is opened",
		"no write",
		""),
	R("comp.lane_list", RC::NotMutating, false, "reads the take lanes and their takes", "no write", ""),
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
	R("rack.get_state", RC::NotMutating, false, "reads the rack", "no write", ""),
	R("rack.zone_resolve", RC::NotMutating, false,
		"reads the zone list and matches a note against it; it routes nothing, "
		"because the rack renders one stereo block and no note path consults a "
		"zone in this build (docs/KNOWN-LIMITATIONS.md)",
		"no write",
		""),
	R("roll.get_state", RC::NotMutating, false, "reads the note list", "no write", ""),
	R("script.list", RC::NotMutating, false, "reads the scripts directory", "no write", ""),
	R("settings.get", RC::NotMutating, false, "reads one config value", "no write", ""),
#ifdef ZENE_TELEMETRY_ENABLED
	R("telemetry.status", RC::NotMutating, false,
		"reads the consent record, the payload builder and - CODE-7 - the "
		"transport's own policy and the verdict on the endpoint that is "
		"configured (transport_policy, transport_endpoint_allowed, "
		"transport_endpoint_reason, transport_blocking). Reporting a policy is "
		"not writing one: nothing here can change the endpoint or the scheme, "
		"and the endpoint is not project state", "no write", ""),
#endif // ZENE_TELEMETRY_ENABLED
	R("track.get_state", RC::NotMutating, false, "reads one track", "no write", ""),
	R("track.list", RC::NotMutating, false, "reads the track container", "no write", ""),
	R("track.folder_get_state", RC::NotMutating, false,
		"reads one folder: its mode, its two flags, the channel it owns and every "
		"child with the channel that child is on", "no write", ""),
	R("track.visibility_set_list", RC::NotMutating, false,
		"reads the container's named visibility sets and the active one",
		"no write", ""),
	// The `vca.*` group's two reads (OWNER-31 item 11). They are passive rows
	// and not rows of the group's own table file because this table's blocks are
	// split by WHAT THE INVERSE IS, not by command group: a reader with nothing
	// to reverse belongs with the other `not_mutating` inspectors, exactly as
	// track.folder_get_state and track.visibility_set_list do.
	R("vca.list", RC::NotMutating, false,
		"reads every group of the mix: the fader, the gain published from it, the "
		"mute and solo flags, the phase lock, the member channels with the gain "
		"each is being scaled by, and the edit set",
		"no write", ""),
	R("vca.get_state", RC::NotMutating, false,
		"reads one group's own state and resolves its edit set against the live "
		"track container - a read that reports an id whose track is gone as "
		"missing rather than dropping it",
		"no write", ""),
	R("transport.get_state", RC::NotMutating, false, "reads the transport", "no write", ""),

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

	R("warp.list", RC::NotMutating, false, "reads a clip's warp map", "no write", ""),


	// The browser's four read-only commands (030/w10-browser): restored after
	// a merge resolution took the other side wholesale and dropped them.
	R("browser.roots", RC::NotMutating, false,
		"reads the browser's root directories and whether each exists",
		"no write", ""),
	R("browser.query", RC::NotMutating, false,
		"walks the browser's directories and reads the tag store; it opens an "
		"audio file for its metadata only when the caller sets 'probe', and it "
		"writes nothing",
		"no write: the result is derived state, and nothing is kept between "
		"calls", ""),
	R("browser.tags", RC::NotMutating, false,
		"reads the tag store and the vocabulary derived from it", "no write", ""),
	R("browser.peaks", RC::NotMutating, false,
		"opens the audio file and fills the peak cache: the cache is a "
		"memory-resident derived view of the file, keyed on its path and last "
		"modification, and it is neither project state nor written to disk",
		"no write: an entry is dropped when the file changes, and the whole "
		"cache is bounded (BrowserPeakCache::Capacity entries of "
		"BrowserPeakCache::BaseBuckets peaks each)", ""),
#ifdef LMMS_HAVE_WASM
	// ==============================================================	// wasm.* - the sandbox's read half (item #614). It writes no project state;
	// the rows travel with the group for the reason the three snapshot rows for
	// wasm.load / unload / set_param give in
	// ControlReversibilityTableSnapshot.cpp.
	// =====================================================================
	R("wasm.list", RC::NotMutating, false,
		"reports the ABI the sandbox implements - its hard limits, the exports a "
		"module must declare, the host imports it may use - and enumerates the "
		".wat/.wasm files in one directory: a directory listing and a set of "
		"constants, nothing written",
		"no write. A 'root' that is not a readable directory is a typed "
		"not_found naming the default rather than an empty module list, so a "
		"mis-typed path cannot read as 'this build can host nothing'", ""),
	R("wasm.get_state", RC::NotMutating, false,
		"reads the hosted module's runtime state: whether one is hosted, its "
		"path/format/fuel budget, and the ABI values the host has read from it "
		"(channels and latency after the host's clamp, whether it exports "
		"process(), its memory size) plus all 16 parameter slots",
		"no write to the project. It DOES consume the sandbox's log buffer - the "
		"text belongs to the most recent call and takeLog() clears it - which "
		"the command's own description states on the wire rather than leaving "
		"the caller to discover it", ""),
	R("wasm.process", RC::NotMutating, false,
		"runs one block through the hosted module and reads plane 0's output "
		"back. The only thing it writes is the module's OWN linear memory - the "
		"input planes the host fills, and whatever the module itself stores "
		"there - which is not project state, is not serialized and is not "
		"journalled",
		"no transaction: nothing the project or the control surface holds "
		"changes, so there is nothing for control.undo to reverse and nothing "
		"for it to be blocked by. A call whose module traps or runs out of fuel "
		"is REPORTED ('status': 'trap' with the wasmtime trap code) rather than "
		"thrown, because a contained trap is the sandbox's whole purpose", ""),
#endif // LMMS_HAVE_WASM

	// The modulation layer's two read-only inspectors (#602). modulator.get_state
	// reports the layer AND the number of routes the audio thread will actually
	// write, so a route whose device is gone is visible rather than hidden.
	R("modulator.get_state", RC::NotMutating, false,
		"reads the layer and its resolved write set; nothing is changed",
		"no write: the writers are modulator.create/remove/rate_set and the "
		"route half, each of which carries an action checkpoint", ""),

	R("note.expression_get", RC::NotMutating, false,
		"reads a note's per-note MPE expression (task #601's fields) or lists "
		"the notes of a clip that carry one",
		"no write: note.expression_set and note.expression_clear are the "
		"writers, and both carry a MidiClip checkpoint", ""),

	// Punch in/out and the take journal's inspectors (0.3.0).
	R("transport.punch_get_state", RC::NotMutating, false,
		"reads the transport's punch region (its range, its arm flag) and "
		"Timeline::punchCapturesAt() at the current play position - the gate's own "
		"answer - so a caller can see whether a capture starting here would be "
		"inside the region. Nothing is written",
		"no write: transport.punch_set and transport.punch_clear are the writers, "
		"and both carry a Timeline checkpoint", ""),
	R("record.recovery_get_state", RC::NotMutating, false,
		"READS a directory for take journals an abnormal exit left behind and "
		"measures each take's own file for its frame count; it creates, rewrites "
		"and removes nothing, so there is no transaction for control.undo to "
		"reverse or to be blocked by",
		"no write: record.journal_begin / journal_update / journal_finish are the "
		"writers, record.recovery_restore and record.recovery_discard resolve an "
		"offer. The recoverable count it reports is the smaller of what the "
		"journal recorded and what the take's file holds, so the reading cannot "
		"promise audio that is not on disk (include/RecordingJournal.h)", ""),

	// The groove pool's read-only inspector (docs/GROOVE-POOL.md). Its six
	// mutating siblings have their own rows: groove.apply and groove.quantize
	// are live-checkpoint rows in ControlReversibilityTable.cpp, and the four
	// pool edits are action-checkpoint rows in
	// ControlReversibilityTableAction.cpp.
	R("groove.list", RC::NotMutating, false,
		"reads the project's groove pool - the templates, the engine's own "
		"bounds, and with 'name' one groove's steps",
		"no write: groove.extract/set/remove/rename are the pool's writers and "
		"each carries a recorded action checkpoint", ""),
	R("clock.get_state", RC::NotMutating, false,
		"reads the clock's master and slave state, the message counters it has "
		"emitted and the tracker's measurement",
		"no write: clock.master_set and clock.slave_set are the group's writers "
		"and each carries a recorded action checkpoint", ""),


	// The chain-preset store's two read-only inspectors (the 0.3.0 ladder's
	// "plugin-chain presets", OWNER-31 item 2). Its four writers are
	// action-checkpoint rows in ControlReversibilityTableAction.cpp: the
	// store is a FILE tree outside the project, so nothing in it is a live
	// JournallingObject a checkpoint could restore.
	R("chain.list", RC::NotMutating, false,
		"reads the preset store: every *.zcp document in the user preset tree's "
		"chainpresets/ directory, each parsed for its device count and its device "
		"identities, and with 'name' one preset in full. It creates no directory, "
		"opens no device and writes nothing",
		"no write: the store is read with QDir/QFileInfo and each document is "
		"parsed for its own identity attributes. A store that does not exist yet "
		"is an empty list, which is the state before chain.save runs once", ""),
	R("chain.get_state", RC::NotMutating, false,
		"reads ONE stored chain preset: its devices in order, each device's own "
		"identity (the LV2 URI, the LADSPA file and label, or the built-in name), "
		"the format, and the size and SHA-256 of the device's state document",
		"no write: the state documents themselves are not even returned - the "
		"command reports their size and hash, and chain.apply is what writes a "
		"device's settings back", ""),
};

constexpr int kPassiveRowCount = static_cast<int>(sizeof(kPassiveRows) / sizeof(kPassiveRows[0]));

} // namespace

const ReversibilityRow* reversibilityPassiveRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kPassiveRowCount; }
	return kPassiveRows;
}

} // namespace control
} // namespace lmms
