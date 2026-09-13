/*
 * ControlRegistry.cpp - the in-app command registry (SPEC A11-A16).
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

#include "ControlRegistry.h"
#include "UnattendedRun.h"

#include "ControlReversibility.h"
#include "ProjectJournal.h"


#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaObject>
#include <QThread>

#include "AudioEngine.h"
#include "Engine.h"
#include "Mixer.h"
#include "Song.h"

namespace lmms
{

ControlRegistry* ControlRegistry::s_instance = nullptr;
bool ControlRegistry::s_ready = false;

constexpr int ControlProtocolVersion = 1;

QString controlErrorKindName(ControlErrorKind kind)
{
	switch (kind)
	{
		case ControlErrorKind::None: return QString();
		case ControlErrorKind::NotFound: return QStringLiteral("not_found");
		case ControlErrorKind::Requires: return QStringLiteral("requires");
		case ControlErrorKind::InvalidArgs: return QStringLiteral("invalid_args");
		case ControlErrorKind::Busy: return QStringLiteral("busy");
		case ControlErrorKind::Refused: return QStringLiteral("refused");
		case ControlErrorKind::Irreversible: return QStringLiteral("irreversible");
	}
	return QString();
}

ControlResult ControlResult::success(QJsonObject result)
{
	ControlResult r;
	r.ok = true;
	r.result = std::move(result);
	return r;
}

ControlResult ControlResult::failure(ControlErrorKind kind, const QString& message)
{
	ControlResult r;
	r.ok = false;
	r.errorKind = kind;
	r.errorMessage = message;
	return r;
}

// ---------------------------------------------------------------------------
// schema validation (a deliberately small JSON-schema subset: type, required,
// properties, additionalProperties, minimum, maximum, enum)
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// registry
// ---------------------------------------------------------------------------

ControlRegistry::ControlRegistry(QObject* parent) :
	QObject(parent),
	m_commands(),
	m_transactions(),
	m_headless(false)
{
	// Headless = no display a human could answer a dialog on. One place decides
	// (UnattendedRun.h), because MainWindow asks the same question before the
	// registry exists.
	m_headless = isUnattendedRun();  // one predicate for one concept: agent instance or no display
}

ControlRegistry* ControlRegistry::instance()
{
	if (s_instance == nullptr)
	{
		s_instance = new ControlRegistry;
		registerControlCommands(*s_instance);
	}
	return s_instance;
}

void ControlRegistry::destroy()
{
	delete s_instance;
	s_instance = nullptr;
}

void ControlRegistry::registerCommand(const ControlCommand& command)
{
	Q_ASSERT(!command.id.isEmpty());
	m_commands.insert(command.id, command);
}

bool ControlRegistry::hasCommand(const QString& id) const
{
	return m_commands.contains(id);
}

QStringList ControlRegistry::commandIds() const
{
	QStringList ids = m_commands.keys();
	ids.sort();
	return ids;
}

const ControlCommand* ControlRegistry::command(const QString& id) const
{
	const auto it = m_commands.constFind(id);
	return it == m_commands.constEnd() ? nullptr : &it.value();
}

bool ControlRegistry::isEngineReady()
{
	return s_ready && Engine::getSong() != nullptr && Engine::mixer() != nullptr;
}

void ControlRegistry::setReady(bool ready)
{
	s_ready = ready;
}

bool ControlRegistry::startupComplete()
{
	return s_ready;
}


QString ControlRegistry::checkRequires(const ControlCommand& command) const
{
	for (const QString& requirement : command.requiresDecl)
	{
		if (requirement == QLatin1String("human"))
		{
			return QStringLiteral("command '%1' requires a human").arg(command.id);
		}
		if (requirement == QLatin1String("display") && m_headless)
		{
			return QStringLiteral("command '%1' requires a display (headless instance)").arg(command.id);
		}
		if (requirement == QLatin1String("device"))
		{
			AudioEngine* audio = Engine::audioEngine();
			if (audio != nullptr && audio->audioDevStartFailed())
			{
				return QStringLiteral("command '%1' requires an audio device: the configured device "
					"'%2' failed to open (%3), so this instance is running with '%4', which produces "
					"no sound output")
					.arg(command.id, audio->audioDevRequestName(), audio->audioDevStartReason(),
						audio->audioDevName());
			}
		}
	}
	return QString();
}

ControlResult ControlRegistry::runOnUiThread(const std::function<ControlResult()>& fn)
{
	QCoreApplication* app = QCoreApplication::instance();
	if (app == nullptr || QThread::currentThread() == app->thread())
	{
		return fn();
	}

	ControlResult out = ControlResult::failure(ControlErrorKind::Busy, QStringLiteral("the UI thread did not run the command"));
	QMetaObject::invokeMethod(app, [&fn, &out]() { out = fn(); }, Qt::BlockingQueuedConnection);
	return out;
}

ControlResult ControlRegistry::invoke(const QString& id, const QJsonObject& args)
{
	const ControlCommand* cmd = command(id);
	if (cmd == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no command '%1'").arg(id));
	}

	const QString unmet = checkRequires(*cmd);
	if (!unmet.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Requires, unmet);
	}

	const QString invalid = validateArgs(cmd->argsSchema, args);
	if (!invalid.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, invalid);
	}

	if (cmd->requiresEngine && !isEngineReady())
	{
		// Carry the reason (task #626): a bare 'busy' left a client with no way
		// to tell "still starting" from "your audio device failed" - the agent
		// surface contract says every failure is typed AND explained.
		const ReadinessReport state = readinessReport();
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the engine is not addressable yet [%1]: %2").arg(state.code, state.message));
	}

	const std::function<ControlResult(const QJsonObject&)> handler = cmd->handler;
	const bool mutating = cmd->mutating;
	const QString commandId = cmd->id;
	return runOnUiThread([this, handler, mutating, commandId, args]() {
		return runHandler(handler, commandId, mutating, args);
	});
}

/*! Everything that has to happen around one handler: the one-command-one-step
 *  merge, the coalescing rule, and the transaction record.
 *
 *  Split out of invoke() so neither function carries the dispatch AND the
 *  bookkeeping (the complexity ratchet counts the lambda's decisions as
 *  invoke's - the same reason the file-length and complexity gates pushed the
 *  transaction record into ControlTransactions.cpp).
 *
 *  ONE agent command = ONE undo step (SPEC A16 deliverable 3): the depth is
 *  marked before the handler runs and everything it pushed is merged after,
 *  because AutomatableModel::setValue() pushes a checkpoint of its own on every
 *  non-automated write, so a command that writes N models would otherwise cost
 *  N undos.
 */
ControlResult ControlRegistry::runHandler(
	const std::function<ControlResult(const QJsonObject&)>& handler, const QString& commandId,
	bool mutating, const QJsonObject& args)
{
	ProjectJournal* journal = Engine::projectJournal();
	const int mark = journal != nullptr ? journal->undoDepth() : -1;
	const quint64 serialBefore = journal != nullptr ? journal->topStepSerial() : 0;

	ControlResult result = handler(args);

	bool coalesced = false;
	quint64 step = 0;
	if (journal != nullptr)
	{
		// The merge first: one command is one step however many models it wrote.
		// THEN the coalescing rule, which can merge that step into the previous
		// command's when this call continues the same gesture.
		journal->mergeCheckpointsFrom(mark);
		// Did THIS call produce a step? Asked by SERIAL, not by depth: a bound
		// can evict steps while the handler runs, so the depth can be exactly
		// what it was before the call even though a step was pushed. (Found by
		// measurement - docs/UNDO-BOUNDS.md, "the step serial".)
		const quint64 produced = journal->topStepSerial();
		if (produced != serialBefore)
		{
			step = produced;
			coalesced = coalesceStepOf(*journal, commandId, args, true);
		}
	}
	if (mutating && result.ok)
	{
		recordTransactionOf(commandId, &result, coalesced, step);
	}
	result.result.remove(QStringLiteral("__transaction"));
	return result;
}

//! Records what a successful mutating handler described (SPEC A16).
void ControlRegistry::recordTransactionOf(const QString& commandId, ControlResult* result,
	bool coalesced, quint64 step)
{
	// One record per UNDO STEP, not per call: when the coalescing rule merged
	// this call's step into the previous run's, the record that run started is
	// extended and no second record is written. Nothing this call reported is
	// lost - `before` is still the state before the gesture and `inverse` still
	// reverts all of it - so the count is the only thing that changes.
	if (coalesced) { extendTopTransaction(); return; }

	// The handler describes before-state + inverse under the private
	// "__transaction" key; the registry records it. A handler that supplies
	// nothing still gets an honest record saying so, and the CLASS always comes
	// from the contract table rather than from the handler.
	const QJsonObject recorded = result->result.value(QStringLiteral("__transaction")).toObject();
	if (recorded.isEmpty())
	{
		Transaction honest;
		honest.command = commandId;
		honest.step = step;
		stampContract(commandId, &honest);
		honest.mechanism = QStringLiteral("this command recorded no inverse or "
			"snapshot; ") + honest.mechanism;
		recordTransaction(honest);
		return;
	}

	Transaction tx;
	tx.command = commandId;
	tx.before = recorded.value(QStringLiteral("before")).toObject();
	tx.inverse = recorded.value(QStringLiteral("inverse")).toObject();
	tx.reversible = recorded.value(QStringLiteral("reversible")).toBool(false);
	tx.mechanism = recorded.value(QStringLiteral("mechanism")).toString();
	tx.step = step;
	stampContract(commandId, &tx);
	recordTransaction(tx);
}

void ControlRegistry::stampContract(const QString& commandId, Transaction* tx) const
{
	// The CLASS comes from the one table (ReversibilityTable), never from the
	// handler: a command cannot declare itself reversible where the contract
	// says it is not. A handler may still record LESS than its class allows (an
	// instrument replacement inside plugin.load), never more.
	const control::ReversibilityEntry* entry = control::ReversibilityTable::instance().lookup(commandId);
	if (entry == nullptr)
	{
		// No row: the table does not describe this command, so it cannot
		// contradict the handler either. The claim is kept and the record says
		// the row is missing, because the anti-drift test is what must fail
		// here - silently downgrading a handler's honest inverse would hide the
		// real problem (a command the contract forgot).
		tx->cls = QString();
		tx->mechanism.prepend(QStringLiteral("NO CONTRACT ROW for this command (the "
			"classification table is incomplete); "));
		return;
	}
	tx->cls = control::reversibilityClassName(entry->cls);
	if (entry->cls == control::ReversibilityClass::Irreversible && tx->reversible)
	{
		// Refusing to launder it: the contract says there is no inverse, so the
		// record must not claim one.
		tx->reversible = false;
		tx->mechanism = QStringLiteral("handler claimed an inverse but the contract class is "
			"'irreversible'; the claim is dropped. ") + tx->mechanism;
	}
}

QJsonObject ControlRegistry::describeAll() const
{
	QJsonArray commands;
	for (const QString& id : commandIds())
	{
		const ControlCommand& cmd = *command(id);
		QJsonObject entry;
		entry.insert(QStringLiteral("id"), cmd.id);
		entry.insert(QStringLiteral("group"), cmd.group);
		entry.insert(QStringLiteral("description"), cmd.description);
		entry.insert(QStringLiteral("requires"), QJsonArray::fromStringList(cmd.requiresDecl));
		entry.insert(QStringLiteral("mutating"), cmd.mutating);
		entry.insert(QStringLiteral("args_schema"), cmd.argsSchema);
		entry.insert(QStringLiteral("result_schema"), cmd.resultSchema);
		commands.append(entry);
	}
	QJsonObject out;
	out.insert(QStringLiteral("commands"), commands);
	out.insert(QStringLiteral("count"), commands.size());
	out.insert(QStringLiteral("proto"), ControlProtocolVersion);
	return out;
}






void ControlRegistry::addShutdownHook(std::function<void()> hook)
{
	m_shutdownHooks.append(std::move(hook));
}

void ControlRegistry::runShutdownHooks()
{
	for (const std::function<void()>& hook : m_shutdownHooks)
	{
		if (hook) { hook(); }
	}
	m_shutdownHooks.clear();
}

// ---------------------------------------------------------------------------
// shared helpers
//
// The id formatters and idToIndex() moved to ControlVocabulary.cpp
// (2026-09-12): one definition for the whole surface.
// ---------------------------------------------------------------------------

namespace control
{

} // namespace control

void registerControlCommands(ControlRegistry& registry)
{
	registerControlGroupCommands(registry);
	registerTransportCommands(registry);
	registerTransportTempoMapCommands(registry);
	registerMixerCommands(registry);
	registerProjectCommands(registry);
	registerSurfaceCommands(registry);
#ifdef ZENE_TELEMETRY_ENABLED
	// The telemetry.* group travels with the client. -DZENE_TELEMETRY=OFF
	// removes the client, so the registry must not carry ids that would
	// describe commands no handler in this binary could answer.
	registerTelemetryCommands(registry);
#endif

	registerArrangementCommands(registry);
	registerClipCommands(registry);
	registerClipEditsCommands(registry);
	registerNoteCommands(registry);
	registerPluginCommands(registry);
	registerDspCommands(registry);
	registerSettingsCommands(registry);
	registerAutomationCommands(registry);
	registerAutomationEditCommands(registry);
	registerWarpCommands(registry);
	registerScriptCommands(registry);
#ifdef LMMS_HAVE_SESSION_VIEW
	// The session.* group travels with the Session View data layer: without
	// LMMS_HAVE_SESSION_VIEW there is no grid to address, and the registry
	// must not carry ids whose handler could not exist (the same rule the
	// telemetry.* group above follows). The A16 table guards its rows with the
	// same #ifdef, so the two stay consistent in both directions.
	registerSessionCommands(registry);
	registerSessionLaunchCommands(registry);
#endif
	registerExportCommands(registry);
	registerRackCommands(registry);
	// The browser group carries no compile-time switch: it reads directories and
	// a JSON file in the config directory, both of which exist in every
	// configuration, so its ids are always honest.
	registerBrowserCommands(registry);
	// Take lanes and the composite (comping, task #600): the take half and the
	// composite half, each its own translation unit.
	registerCompCommands(registry);
	registerCompEditCommands(registry);
<<<<<<< HEAD
#ifdef LMMS_HAVE_WASM
	// The wasm.* group travels with the WASM DSP sandbox: without LMMS_HAVE_WASM
	// there is no wasmtime to host a module in, and the registry must not carry
	// ids whose handler could not exist (the rule the telemetry.* and session.*
	// groups above follow). The A16 table guards its six rows with the same
	// #ifdef, so the two stay consistent in both directions.
	registerWasmCommands(registry);
#endif
=======
	// The modulation layer (#602): the layer + LFO half, the route half, and the
	// per-note expression group. No compile-time switch - the layer is a plain
	// value on Song and a Note field, so its ids are honest in every
	// configuration.
	registerModulatorCommands(registry);
	registerModulatorRouteCommands(registry);
	registerNoteExpressionCommands(registry);
>>>>>>> 030/w18-modulation
}

} // namespace lmms
