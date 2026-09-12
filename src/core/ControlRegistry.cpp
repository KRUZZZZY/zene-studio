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
		// ONE agent command = ONE undo step (SPEC A16 deliverable 3). The mark is
		// taken before the handler runs and everything it pushed is merged after,
		// because AutomatableModel::setValue() pushes checkpoints of its own on
		// every non-automated write - so a command that writes N models would
		// otherwise cost N undos.
		ProjectJournal* journal = Engine::projectJournal();
		const int mark = journal != nullptr ? journal->undoDepth() : -1;
		ControlResult result = handler(args);
		if( journal != nullptr ) { journal->mergeCheckpointsFrom( mark ); }
		if (mutating && result.ok) { recordTransactionOf(commandId, &result); }
		result.result.remove(QStringLiteral("__transaction"));
		return result;
	});
}

//! Records what a successful mutating handler described (SPEC A16).
void ControlRegistry::recordTransactionOf(const QString& commandId, ControlResult* result)
{
	// The handler describes before-state + inverse under the private
	// "__transaction" key; the registry records it. A handler that supplies
	// nothing still gets an honest record saying so, and the CLASS always comes
	// from the contract table rather than from the handler.
	const QJsonObject recorded = result->result.value(QStringLiteral("__transaction")).toObject();
	if (recorded.isEmpty())
	{
		Transaction honest;
		honest.command = commandId;
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

void ControlRegistry::recordTransaction(const Transaction& tx)
{
	// The record is bounded TWO ways (SPEC A16 deliverable 2):
	//   - a hard count cap of MaxTransactionRecords records;
	//   - a hard total cap of MaxTransactionBytes of serialised before-state
	//     plus inverse descriptor.
	// Eviction policy: FIFO - the OLDEST record is dropped first, so the most
	// recent history is never the part that is lost, and the newest record
	// (the one control.undo reads) is always present. What happens at the cap
	// is reported, not hidden: `control.transactions` returns `evicted`,
	// `capped` and the retained/limit byte counts. The engine's own undo stack
	// evicts at the same depth (ProjectJournal::MAX_UNDO_STATES = 100), so an
	// agent never sees a record for a step it can no longer undo.
	Transaction stamped = tx;
	stamped.bytes = QJsonDocument(stamped.before).toJson(QJsonDocument::Compact).size()
		+ QJsonDocument(stamped.inverse).toJson(QJsonDocument::Compact).size()
		+ stamped.mechanism.toUtf8().size()
		+ static_cast<int>(stamped.command.toUtf8().size());

	m_transactions.append(stamped);
	m_recordedBytes += stamped.bytes;

	while (m_transactions.size() > control::MaxTransactionRecords
		|| (m_recordedBytes > control::MaxTransactionBytes && m_transactions.size() > 1))
	{
		m_recordedBytes -= m_transactions.first().bytes;
		m_transactions.remove(0);
		++m_evicted;
	}
}

const ControlRegistry::Transaction* ControlRegistry::lastTransaction() const
{
	return m_transactions.isEmpty() ? nullptr : &m_transactions.last();
}

QJsonArray ControlRegistry::transactions() const
{
	QJsonArray out;
	for (const Transaction& tx : m_transactions)
	{
		QJsonObject entry;
		entry.insert(QStringLiteral("command"), tx.command);
		entry.insert(QStringLiteral("class"), tx.cls);
		entry.insert(QStringLiteral("before"), tx.before);
		entry.insert(QStringLiteral("inverse"), tx.inverse);
		entry.insert(QStringLiteral("reversible"), tx.reversible);
		entry.insert(QStringLiteral("mechanism"), tx.mechanism);
		entry.insert(QStringLiteral("bytes"), tx.bytes);
		out.append(entry);
	}
	return out;
}

QJsonObject ControlRegistry::transactionsReport() const
{
	QJsonObject out;
	out.insert(QStringLiteral("transactions"), transactions());
	out.insert(QStringLiteral("count"), m_transactions.size());
	out.insert(QStringLiteral("retained_bytes"), m_recordedBytes);
	out.insert(QStringLiteral("cap_records"), control::MaxTransactionRecords);
	out.insert(QStringLiteral("cap_bytes"), control::MaxTransactionBytes);
	out.insert(QStringLiteral("evicted"), m_evicted);
	// `capped` says out loud that older records were dropped, so a client can
	// tell "this is the whole history" from "this is what the bound retains".
	out.insert(QStringLiteral("capped"), m_evicted > 0);
	return out;
}

void ControlRegistry::clearTransactions()
{
	m_transactions.clear();
	m_recordedBytes = 0;
	m_evicted = 0;
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

ControlRegistry::Transaction makeTransaction(const QString& command, QJsonObject before,
	QJsonObject inverse, bool reversible, const QString& mechanism)
{
	ControlRegistry::Transaction tx;
	tx.command = command;
	tx.before = std::move(before);
	tx.inverse = std::move(inverse);
	tx.reversible = reversible;
	tx.mechanism = mechanism;
	return tx;
}

} // namespace control

void registerControlCommands(ControlRegistry& registry)
{
	registerControlGroupCommands(registry);
	registerTransportCommands(registry);
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
	registerNoteCommands(registry);
	registerPluginCommands(registry);
	registerDspCommands(registry);
	registerSettingsCommands(registry);
	registerAutomationCommands(registry);
	registerAutomationEditCommands(registry);
	registerScriptCommands(registry);
}

} // namespace lmms
