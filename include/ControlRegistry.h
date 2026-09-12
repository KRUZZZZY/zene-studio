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
	//! Stamps \a tx with the contract table's class for its command and refuses
	//! to let a handler claim an inverse the contract says does not exist.
	void stampContract(const QString& commandId, Transaction* tx) const;
	//! Records the transaction a successful mutating handler described, under
	//! the contract table's class. Split out of invoke() so one function does
	//! not carry the dispatch, the merge and the record (complexity ratchet).
	void recordTransactionOf(const QString& commandId, ControlResult* result);

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
	QVector<std::function<void()>> m_shutdownHooks;
	int m_recordedBytes = 0;
	int m_evicted = 0;
	bool m_headless;
};

//! Register every command of the first control-surface slice (SPEC A12/A16).
LMMS_EXPORT void registerControlCommands(ControlRegistry& registry);
//! control.* — ping, version, commands_list, undo, redo, quit.
LMMS_EXPORT void registerControlGroupCommands(ControlRegistry& registry);
//! transport.* and track.*
LMMS_EXPORT void registerTransportCommands(ControlRegistry& registry);
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

//! Shared helpers for the command groups.
namespace control
{
// The id formatters and idToIndex() (trackId, clipId, noteId, channelId,
// deviceId, effectId) live in ControlVocabulary.h - one definition for the
// whole surface. See that header's comment for why.
//! A fresh "mutating command is not undone by itself" transaction record.
ControlRegistry::Transaction makeTransaction(const QString& command, QJsonObject before,
	QJsonObject inverse, bool reversible, const QString& mechanism);
} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_REGISTRY_H
