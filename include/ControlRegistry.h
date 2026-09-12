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

#include "lmms_export.h"

namespace lmms
{

//! Error kinds of the control protocol. Closed set, on the wire as
//! "not_found | requires | invalid_args | busy | refused" (AGENT-TOOLING.md #4).
enum class ControlErrorKind
{
	None,
	NotFound,
	Requires,
	InvalidArgs,
	Busy,
	Refused,
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

	void recordTransaction(const Transaction& tx);
	QJsonArray transactions() const;
	void clearTransactions();

	//! Run by control.quit's watchdog when the event loop does not stop on its
	//! own: the control socket unlinks itself here so the shutdown contract
	//! ("the socket file is removed on exit") holds either way.
	void addShutdownHook(std::function<void()> hook);
	void runShutdownHooks();

private:
	explicit ControlRegistry(QObject* parent = nullptr);
	QString checkRequires(const ControlCommand& command) const;

	static ControlRegistry* s_instance;
	static bool s_ready;

	QHash<QString, ControlCommand> m_commands;
	QVector<Transaction> m_transactions;
	QVector<std::function<void()>> m_shutdownHooks;
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
//! project.* and render.render
LMMS_EXPORT void registerProjectCommands(ControlRegistry& registry);
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

//! Shared helpers for the command groups.
namespace control
{
//! "trk-<n>" for a Song track index.
QString trackId(int index);
//! "ch-<n>" for a mixer channel index.
QString channelId(int index);
//! "dev-<n>" for an index in the build's device catalogue (plugin.list).
QString deviceId(int index);
//! "fx-<n>" for a device instance's index in its target's chain.
QString effectId(int index);
//! Parses "trk-<n>" / "ch-<n>"; returns -1 when malformed.
int idToIndex(const QString& id, const QString& prefix);
//! A fresh "mutating command is not undone by itself" transaction record.
ControlRegistry::Transaction makeTransaction(const QString& command, QJsonObject before,
	QJsonObject inverse, bool reversible, const QString& mechanism);
} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_REGISTRY_H
