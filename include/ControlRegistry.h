/*
 * ControlRegistry.h - the in-app command registry behind menus, shortcuts and
 *                     the agent control surface (SPEC-zene-studio.md A11-A16).
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

#ifndef LMMS_CONTROL_REGISTRY_H
#define LMMS_CONTROL_REGISTRY_H

#include <functional>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ControlUndoCoalescing.h"
#include "ControlVocabulary.h"
#include "lmms_export.h"

class QTimer;

namespace lmms
{

//! Error kinds of the control protocol. Closed set, on the wire as
//! "not_found | requires | invalid_args | busy | refused | irreversible"
//! (AGENT-TOOLING.md #4; `irreversible` added by SPEC A16 so an undo attempt on
//! a command with no inverse FAILS, typed, instead of pretending).
enum class ControlErrorKind
{
	None,
	NotFound,
	Requires,
	InvalidArgs,
	Busy,
	Refused,
	//! control.undo refuses: the last recorded command has no inverse (or its
	//! inverse is a documented manual fallback only). The message names the
	//! command, its class and the fallback.
	Irreversible,
};

//! Wire name of \p kind; empty for None.
LMMS_EXPORT QString controlErrorKindName(ControlErrorKind kind);

//! The reply to one command: exactly one of result / error is populated.
struct ControlResult
{
	bool ok = true;
	QJsonObject result;
	ControlErrorKind errorKind = ControlErrorKind::None;
	QString errorMessage;

	static ControlResult success(QJsonObject result = QJsonObject());
	static ControlResult failure(ControlErrorKind kind, const QString& message);
};

//! One registered command (SPEC A11). Every user-facing action is one of these;
//! menus, shortcuts, Lua and the agent surface must all call this entry.
struct ControlCommand
{
	QString id;             //!< stable "group.verb"
	QString group;
	QString verb;
	QString description;
	//! subset of {"display","device","human"}; empty means headless-safe (SPEC A13).
	//! "requires" is a C++20 keyword, so the field is named requiresDecl on the
	//! C++ side; it is serialised as "requires" on the wire.
	QStringList requiresDecl;
	//! JSON-schema subset for args; see ControlRegistry::validateArgs()
	QJsonObject argsSchema;
	QJsonObject resultSchema;
	//! records a transaction per SPEC A16
	bool mutating = false;
	//! false for the liveness and bookkeeping commands (control.*) that must answer
	//! even before the engine is initialised; those report readiness themselves.
	bool requiresEngine = true;
	std::function<ControlResult(const QJsonObject& args)> handler;
};

//! The single command registry (SPEC A11). Handlers always run on the UI thread.
class LMMS_EXPORT ControlRegistry : public QObject
{
	Q_OBJECT
public:
	//! What a mutating command recorded for reversibility (SPEC A16).
	struct Transaction
	{
		QString command;
		QJsonObject before;   //!< before-state snapshot
		QJsonObject inverse;  //!< the inverse operation, serialised
		bool reversible = false;
		QString mechanism;    //!< how it is reversed, or why it cannot be
		//! The contract table's class for \c command: "true_inverse",
		//! "snapshot", "irreversible" or "not_mutating". Stamped by the registry
		//! from ReversibilityTable, so the class has ONE definition and a
		//! command cannot declare itself out of it.
		QString cls;
		//! Serialised size of this record (before + inverse + mechanism), the
		//! quantity MaxTransactionBytes bounds.
		int bytes = 0;
		/*! How many commands this ONE record covers. 1 normally; more when the
		 *  coalescing rule merged a same-command-same-target run into a single
		 *  undo step - a 200-step drag is one step, so it is one record with
		 *  commands == 200. `before` and `inverse` describe the state BEFORE the
		 *  run and still revert the whole of it. */
		int commands = 1;
		/*! The serial of the journal step this record describes, 0 when the
		 *  record's inverse is a command rather than a step (a file revision).
		 *  control.undo compares it against the stack's oldest retained serial:
		 *  a record whose step a bound has evicted must REFUSE, typed, instead
		 *  of unwinding an older step the caller never asked about. */
		quint64 step = 0;
	};

	static ControlRegistry* instance();
	static void destroy();

	void registerCommand(const ControlCommand& command);
	bool hasCommand(const QString& id) const;
	QStringList commandIds() const;
	const ControlCommand* command(const QString& id) const;
	int commandCount() const { return m_commands.size(); }

	//! Validate, check \c requires and the engine state, then run the handler on
	//! the UI thread.
	ControlResult invoke(const QString& id, const QJsonObject& args = QJsonObject());

	//! Queue \p fn onto the UI thread and wait for its result.
	static ControlResult runOnUiThread(const std::function<ControlResult()>& fn);

	//! Empty string when \p args satisfies \p schema, else a human-readable reason.
	static QString validateArgs(const QJsonObject& schema, const QJsonObject& args);

	//! Payload of control.commands_list.
	QJsonObject describeAll() const;

	//! True when no display is available (offscreen/minimal/vnc platform).
	bool isHeadless() const { return m_headless; }
	void setHeadless(bool headless) { m_headless = headless; }

	//! True once the GUI is fully constructed and the initial project exists.
	//! Commands return the typed 'busy' refusal until then, so a client cannot
	//! re-enter the engine while the application is still starting up.
	static bool isEngineReady();
	static void setReady(bool ready);
	//! True once startup has finished (the registry has been told the model is
	//! up), whether or not an engine came with it. readinessReport() uses it to
	//! tell "still starting" from "started without an engine".
	static bool startupComplete();

	//! What a client needs to understand a `busy` answer (task #626): whether the
	//! engine is addressable, and when it is not, WHY. `code` is a stable reason
	//! code and `message` is actionable. The audio fields are filled from the
	//! engine's own device state, so "the configured device failed to open" can
	//! never be reported as a bare false.
	//!
	//! Reason codes (closed set):
	//!   engine_starting      - startup has not finished; poll control.ping
	//!   audio_device_failed  - the configured device could not open; the engine
	//!                          fell back to the dummy device and will be
	//!                          addressable, but nothing is audible
	//!   engine_missing       - startup finished without an engine (fatal)
	//!   control_unsupported  - the instance was not started with a usable config
	struct ReadinessReport
	{
		bool ready = false;
		QString code;
		QString message;
		bool audioStarted = true;
		QString requestedDevice;
		QString actualDevice;
	};
	static ReadinessReport readinessReport();

	//! How the control surface answers the questions the GUI would ask a human.
	//! Ask is the unchanged interactive behaviour; control.quit sets one of the
	//! others so the shutdown completes with nobody there to click (task #626).
	enum class QuitPromptAnswer
	{
		Ask,      //!< ask a human (default)
		Discard,  //!< answer the "project was modified" question with Discard
		Save,     //!< answer it with Save
	};

	//! Record the shutdown intent and, once the instance is ready, ask the
	//! application to quit through its normal path. A request that arrives while
	//! the application is still starting up is remembered: main() applies it
	//! after startup with applyPendingQuit(), so a client that connects at once
	//! and quits still stops the process.
	static void requestQuit(QuitPromptAnswer answer);
	static bool quitPending();
	static QuitPromptAnswer quitPromptAnswer();
	static void setQuitPromptAnswer(QuitPromptAnswer answer);
	//! Apply a quit requested before readiness. Returns true when it did.
	static bool applyPendingQuit();
	//! Stop the last-resort guard: main() calls this the moment the event loop
	//! returns, so a slow but healthy teardown is never force-exited.
	static void cancelShutdownGuard();

	//! Milliseconds the last-resort shutdown guard waits before forcing an exit.
	//! Kept deliberately long: it must never fire on a healthy shutdown.
	static constexpr int ShutdownGuardMs = 10000;

	void recordTransaction(const Transaction& tx);
	QJsonArray transactions() const;

	//! The coalescing window in force, milliseconds (0 = grouping disabled),
	//! read by control.undo_depth and set by control.set_undo_coalescing.
	int coalesceWindowMs() const { return m_coalescer.windowMs(); }
	//! Sets it; false (and unchanged) outside [0, MaxUndoCoalesceWindowMs], which
	//! the caller reports as a typed refusal.
	bool setCoalesceWindowMs(int ms) { return m_coalescer.setWindowMs(ms); }
	//! control.transactions' payload: the records plus the bounds they live
	//! within (count/bytes caps, retained bytes, evicted count, `capped`).
	QJsonObject transactionsReport() const;
	/*! The most recent transaction, or nullptr when there is none.
	 *
	 * control.undo reads this: it is the record of the LAST agent command, and
	 * the contract is "undo the last agent command, or refuse, typed" - never
	 * "silently undo an older one because this one could not be reversed".
	 */
	const Transaction* lastTransaction() const;
	//! Retained records evicted by the two-sided cap since the last clear.
	int evictedCount() const { return m_evicted; }
	//! Serialised bytes the retained records currently occupy.
	int retainedTransactionBytes() const { return m_recordedBytes; }
	void clearTransactions();

	//! Run by the shutdown path (and by the last-resort guard) so the control
	//! socket unlinks itself whatever route the process leaves by: the shutdown
	//! contract is "the socket file is removed on exit".
	void addShutdownHook(std::function<void()> hook);
	void runShutdownHooks();

private:
	explicit ControlRegistry(QObject* parent = nullptr);
	QString checkRequires(const ControlCommand& command) const;
	/*! Runs one handler with everything that has to happen around it: the
	 *  one-command-one-step merge, the coalescing rule and the transaction
	 *  record. Split out of invoke() so neither function carries the dispatch
	 *  and the bookkeeping (the complexity ratchet). */
	ControlResult runHandler(const std::function<ControlResult(const QJsonObject&)>& handler,
		const QString& commandId, bool mutating, const QJsonObject& args);
	//! Stamps \a tx with the contract table's class for its command and refuses
	//! to let a handler claim an inverse the contract says does not exist.
	void stampContract(const QString& commandId, Transaction* tx) const;
	//! Records the transaction a successful mutating handler described, under
	//! the contract table's class. Split out of invoke() so one function does
	//! not carry the dispatch, the merge and the record (complexity ratchet).
	//! \a coalesced says the step was merged into the previous run's (then the
	//! record it belongs to is EXTENDED, not duplicated) and \a step is the
	//! serial of the journal step the call produced (0 when it produced none).
	void recordTransactionOf(const QString& commandId, ControlResult* result,
		bool coalesced, quint64 step);
	//! Drops the oldest records until both record caps hold. One definition,
	//! called by the append path and by a coalesced run's re-measurement.
	void trimTransactions();
	/*! Applies the coalescing rule to the command that just ran (SPEC A16's
	 *  "undo depth and drag coalescing", task #623): merges its step into the
	 *  previous one when the contract table says this command coalesces on this
	 *  target and the run is inside the window. Defined in
	 *  ControlUndoCoalescing.cpp - this class's own member, kept out of
	 *  ControlRegistry.cpp for the file-length ratchet. */
	bool coalesceStepOf(ProjectJournal& journal, const QString& commandId,
		const QJsonObject& args, bool producedStep);
	//! The record covered one more command of the same run: the step it names is
	//! unchanged (that is what coalescing means), so the command count grows and
	//! the bytes are re-measured - and no second record is appended.
	void extendTopTransaction();

	//! Ask the application to quit through its normal path and arm the
	//! last-resort guard. Called by requestQuit() (immediately) and by
	//! applyPendingQuit() (after startup, for a request that arrived too early).
	static void scheduleQuit();

	static ControlRegistry* s_instance;
	static bool s_ready;
	static bool s_quitPending;
	static QuitPromptAnswer s_quitAnswer;
	static QTimer* s_quitGuard;

	QHash<QString, ControlCommand> m_commands;
	QVector<Transaction> m_transactions;
	//! The coalescing state of the control surface: the run in flight, and the
	//! window it is grouped in (control.set_undo_coalescing).
	control::UndoCoalescer m_coalescer;
	QVector<std::function<void()>> m_shutdownHooks;
	int m_recordedBytes = 0;
	int m_evicted = 0;
	bool m_headless;
};

//! Register every command of the first control-surface slice (SPEC A12/A16).
LMMS_EXPORT void registerControlCommands(ControlRegistry& registry);
//! control.* — ping, version, commands_list, undo, redo, quit.
LMMS_EXPORT void registerControlGroupCommands(ControlRegistry& registry);
/*! control.undo_depth (read the depth, both caps, the retained bytes and the
 *  coalescing rule), control.set_undo_depth (set the caps) and
 *  control.set_undo_coalescing (set the window). The bounded-undo slice of
 *  SPEC A16's obligation; registered by registerControlGroupCommands so the
 *  group stays one group. */
LMMS_EXPORT void registerUndoBoundsCommands(ControlRegistry& registry);
//! transport.* and track.*
LMMS_EXPORT void registerTransportCommands(ControlRegistry& registry);
/*! transport.tempo_map_get / add / remove / clear / set_active - the tempo
 *  map's half of the transport group (D11, docs/TEMPO-MAP.md). It is a separate
 *  translation unit, NOT a separate group: every id keeps the `transport.`
 *  prefix, so an agent finds the map where it finds the tempo. */
LMMS_EXPORT void registerTransportTempoMapCommands(ControlRegistry& registry);
//! mixer.*
LMMS_EXPORT void registerMixerCommands(ControlRegistry& registry);
//! project.open and render.render
LMMS_EXPORT void registerProjectCommands(ControlRegistry& registry);
//! project.save / project.restore_revision / project.get_state - the file-level
//! commands, whose inverse is a retained file revision rather than an object.
LMMS_EXPORT void registerProjectFilesCommands(ControlRegistry& registry);
//! track.set_arm and arrangement.get_state.
LMMS_EXPORT void registerArrangementStateCommands(ControlRegistry& registry);
//! control.surface_report - the live menu/toolbar reflection (SPEC A15).
LMMS_EXPORT void registerSurfaceCommands(ControlRegistry& registry);
//! telemetry.consent (display+human, opens the consent screen) and
//! telemetry.status (read-only: is it compiled in, is consent on, what would
//! be sent). Declared here whatever -DZENE_TELEMETRY says, but DEFINED, and
//! registered, only when the client is compiled in (ZENE_TELEMETRY_ENABLED).
LMMS_EXPORT void registerTelemetryCommands(ControlRegistry& registry);
/*! Open the telemetry consent screen - the ONE implementation behind both the
 *  Help menu's "Telemetry - what we send..." action (which declares
 *  telemetry.consent via the dynamic property "controlCommand") and that
 *  command's registry handler. SPEC A11: one action, one implementation.
 *
 *  Refuses, typed, in an unattended run (include/UnattendedRun.h): the screen
 *  is modal, and a dialog nobody can answer is a hang, not a consent.
 */
LMMS_EXPORT ControlResult openTelemetryConsentScreen();

//! track.add/remove/rename/set_mute/set_solo/set_arm and arrangement.get_state
LMMS_EXPORT void registerArrangementCommands(ControlRegistry& registry);
//! clip.add/move/resize/split/delete/duplicate/select
LMMS_EXPORT void registerClipCommands(ControlRegistry& registry);
//! clip.set_fade/set_gain/crossfade - the same group's fade, crossfade and
//! clip-gain commands (the fade/crossfade/clip-gain wave).
LMMS_EXPORT void registerClipEditsCommands(ControlRegistry& registry);
//! note.add/remove/move/resize/velocity_set/select and roll.get_state
LMMS_EXPORT void registerNoteCommands(ControlRegistry& registry);
//! plugin.* and dsp.get_state - the device catalogue, load/unload/bypass,
//! parameters, plugin state files and presets.
LMMS_EXPORT void registerPluginCommands(ControlRegistry& registry);
//! plugin.list / load / unload / bypass.
LMMS_EXPORT void registerPluginDeviceCommands(ControlRegistry& registry);
//! plugin.param_get / plugin.param_set for a device instance or an instrument.
LMMS_EXPORT void registerPluginParameterCommands(ControlRegistry& registry);
//! plugin.state_save / state_load and plugin.preset_list / preset_load / preset_save.
LMMS_EXPORT void registerPluginStateCommands(ControlRegistry& registry);
//! plugin.preset_list / plugin.preset_load / plugin.preset_save.
LMMS_EXPORT void registerPluginPresetCommands(ControlRegistry& registry);
//! dsp.get_state - the device-chain read-back (SPEC A14).
LMMS_EXPORT void registerDspCommands(ControlRegistry& registry);
//! settings.*, audio.*, midi.* and app.version.
LMMS_EXPORT void registerSettingsCommands(ControlRegistry& registry);
//! automation.get_state and automation.mode_set.
LMMS_EXPORT void registerAutomationCommands(ControlRegistry& registry);
//! automation.add_point / automation.remove_point / automation.clear.
LMMS_EXPORT void registerAutomationEditCommands(ControlRegistry& registry);
//! script.run and script.list.
LMMS_EXPORT void registerScriptCommands(ControlRegistry& registry);
//! warp.list/add/move/remove/set - the warp-marker editing surface of a
//! SampleClip (#597). The engine half is include/WarpMarkers.h and
//! SampleClip's own accessors; this group is what makes it drivable.
LMMS_EXPORT void registerWarpCommands(ControlRegistry& registry);
//! warp.add / warp.move / warp.remove / warp.set - the mutating half of the
//! group, in its own translation unit (the automation group's split).
LMMS_EXPORT void registerWarpEditCommands(ControlRegistry& registry);
/*! session.get_state / set_grid / set_quantisation / set_scene / set_slot /
 *  clear_slot / clear - the Session View grid, its cells and its scenes
 *  (SPEC-zene-studio A1). Declared and DEFINED only when the Session View is
 *  compiled in (LMMS_HAVE_SESSION_VIEW), so a build without the data layer
 *  cannot advertise commands whose model does not exist. */
LMMS_EXPORT void registerSessionCommands(ControlRegistry& registry);
//! session.launch_slot / launch_scene / stop_slot / stop_all - the launch
//! requests, which queue into the audio thread's scheduler and write no project
//! state (SPEC-zene-studio A2/A3).
LMMS_EXPORT void registerSessionLaunchCommands(ControlRegistry& registry);
//! export.get_settings / export.set_dither / export.set_src_quality - the
//! render settings that outlive one OutputSettings (dither, SRC quality).
LMMS_EXPORT void registerExportCommands(ControlRegistry& registry);
//! rack.get_state/add_chain/remove_chain/set_selected, plus the macro and zone
//! halves (#599's macros and key/velocity zones). The engine half is
//! include/Rack.h, include/RackMacros.h and include/RackZones.h; this group is
//! what makes any of it drivable, and it is the ONLY way to reach a rack (the
//! engine lane's report, docs/RACKS.md section 5).
LMMS_EXPORT void registerRackCommands(ControlRegistry& registry);
//! rack.macro_add / macro_remove / macro_target_add / macro_target_remove /
//! macro_set - the macro half, in its own translation unit (the automation and
//! warp groups' split).
LMMS_EXPORT void registerRackMacroCommands(ControlRegistry& registry);
//! rack.zone_add / zone_remove / zone_resolve - the key/velocity zone half.
LMMS_EXPORT void registerRackZoneCommands(ControlRegistry& registry);
/*! comp.lane_add / lane_remove / lane_list / assign - take lanes and the
 *  assignment of takes to them (task #600). The engine half is include/TakeLane.h
 *  and `Track::takeLanes()`; docs/COMPING.md holds the element shape and what is
 *  deliberately not wired yet. */
LMMS_EXPORT void registerCompCommands(ControlRegistry& registry);
//! comp.select / comp.rebuild / comp.get_state - the composite half, in its own
//! translation unit (the clip, warp and rack groups' split).
LMMS_EXPORT void registerCompEditCommands(ControlRegistry& registry);

/*! wasm.list / get_state / process - what the sandbox can host, what it is
 *  hosting and what a block through it does - plus the mutating half.
 *
 *  GUARDED BY #ifdef LMMS_HAVE_WASM, in both the declaration and the definition,
 *  because the sandbox IS a compile-time feature: the wasmtime C API is an
 *  optional dependency (cmake/modules/FindWasmtime.cmake), and WANT_WASM
 *  degrades to OFF without it (CMakeLists.txt:957-963). A build without wasmtime
 *  compiles src/wasm out entirely, so the registry must not carry ids whose
 *  handler could not exist - the rule the session.* and telemetry.* groups
 *  follow. src/core/ControlRegistry.cpp guards its call with the same #ifdef and
 *  the A16 table guards its six rows with it too, so all three stay consistent in
 *  both directions. */
#ifdef LMMS_HAVE_WASM
LMMS_EXPORT void registerWasmCommands(ControlRegistry& registry);
//! wasm.load / unload / set_param - the mutating half, in its own translation
//! unit (the automation, warp, rack and comp groups' read/edit split). Called by
//! registerWasmCommands; the registry has exactly one wasm.* registration point.
LMMS_EXPORT void registerWasmEditCommands(ControlRegistry& registry);
#endif

//! The browser.* group (W8 tag/metadata search plus the waveform peak cache):
//! browser.roots, browser.query, browser.tags and browser.peaks. The engine half
//! is include/BrowserCatalog.h (the roots the browser tabs read, the metadata an
//! audio file can be probed for, the persisted tag store) and
//! include/BrowserPeakCache.h; this group is the ONLY way to reach any of it -
//! there is no UI for tags, no UI for a query and no UI for the peak cache.
LMMS_EXPORT void registerBrowserCommands(ControlRegistry& registry);
//! browser.tag.add / browser.tag.remove - the mutating half, in its own
//! translation unit (the automation and warp groups' read/edit split). Both
//! record a snapshot-class transaction whose inverse is the paired command.
LMMS_EXPORT void registerBrowserTagCommands(ControlRegistry& registry);

//! Shared helpers for the command groups.
namespace control
{
// The id formatters and idToIndex() (trackId, clipId, noteId, channelId,
// deviceId, effectId) live in ControlVocabulary.h - one definition for the
// whole surface. See that header's comment for why.
//! A fresh "mutating command is not undone by itself" transaction record.
ControlRegistry::Transaction makeTransaction(const QString& command, QJsonObject before,
	QJsonObject inverse, bool reversible, const QString& mechanism);
//! The serialised size of one transaction record, in bytes - the quantity
//! MaxTransactionBytes bounds. ONE definition: the append path and a coalesced
//! run's re-measurement both call it, so the byte accounting cannot drift.
LMMS_EXPORT int serialisedRecordBytes(const ControlRegistry::Transaction& tx);
} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_REGISTRY_H
