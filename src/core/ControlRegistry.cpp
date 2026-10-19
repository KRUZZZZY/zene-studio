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
#include "ControlResultCheck.h"
#include "UnattendedRun.h"

#include "CollabCommandTap.h"
#include "ControlReversibility.h"
#include "EditLocks.h"
#include "ProjectJournal.h"
#include "ProvenanceSection.h"


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

namespace
{

/*! \brief The process's shutdown hooks, and the serial that identifies one
 *  (CODE-8).
 *
 * NOT a member of the registry instance, and that is the whole point. The
 * registry is a singleton that can be destroyed and re-created - main() takes
 * `ControlRegistry::instance()` and ControlSession.cpp's last-resort guard calls
 * `ControlRegistry::instance()->runShutdownHooks()` on the way out - so if the
 * instance that existed at registration time is gone, instance() builds a NEW,
 * EMPTY one and a hook held in a member would die with the first instance. The
 * guard would then run nothing at all and leave the control socket behind on
 * exactly the route the hook exists for.
 *
 * A function-local static gives the right lifetime: one store per process,
 * constructed on first use and destroyed at exit - after every path that can
 * call runShutdownHooks(), and never before a hook that is still registered.
 */
struct ShutdownHookEntry
{
	ControlRegistry::ShutdownHookId id = 0;
	std::function<void()> hook;
};

QVector<ShutdownHookEntry>& shutdownHooks()
{
	static QVector<ShutdownHookEntry> hooks;
	return hooks;
}

//! Serial source for ShutdownHookId. 0 is reserved for "no hook", so the first
//! registered hook is 1 and a default-initialised id can never match one.
ControlRegistry::ShutdownHookId nextShutdownHookId()
{
	static ControlRegistry::ShutdownHookId next = 0;
	return ++next;
}

} // namespace

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
	if (command.id.isEmpty() || m_commands.contains(command.id))
	{
		qFatal("ControlRegistry: duplicate or empty command id '%s'", command.id.toUtf8().constData());
	}
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
		// 0.5.0 F6 (WF-050): on a peer that joined a collaboration session, an edit is the host's to apply.
		ControlResult elsewhere;
		if (collab::CommandTap::intercepted(commandId, mutating, m_depth, args, &elsewhere)) { return elsewhere; }
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
namespace
{

//! A true-inverse command leaves a journal step - or, for one with its own mechanism, a transaction payload - whenever it
//! changes something, so a call that left neither changed nothing (a report-only call, a value it already had) and is
//! no transaction and no macro step (review W3-batchA #4, #8).
bool changedNothing(bool journalled, quint64 step, const QString& commandId, const ControlResult& result)
{
	if (!journalled || step != 0 || result.result.contains(QStringLiteral("__transaction"))) { return false; }
	const control::ReversibilityEntry* row = control::ReversibilityTable::instance().lookup(commandId);
	return row != nullptr && row->cls == control::ReversibilityClass::TrueInverse;
}

//! WF-033: whether a call is an EDIT whose undo step needs the project as it was before it (the journal's
//! snapshot bracket). Only a true-inverse command leaves a journal step; an unclassified one is assumed to.
bool bracketsEdit(const ProjectJournal* journal, bool mutating, const QString& commandId)
{
	if (journal == nullptr || !mutating) { return false; }
	// script.run is a snapshot-class row whose one step also carries the bracket's capture (Q050-044), so a
	// project that persists its undo history keeps the step instead of losing the history behind it.
	if (commandId == QLatin1String("script.run")) { return true; }
	const control::ReversibilityEntry* row = control::ReversibilityTable::instance().lookup(commandId);
	return row == nullptr || row->cls == control::ReversibilityClass::TrueInverse;
}

//! WF-033: ProjectJournal::beginEdit/endEdit around one handler (docs/UNDO-HISTORY-PERSISTENCE.md).
class EditBracket
{
public:
	EditBracket(ProjectJournal* journal, bool mutating, const QString& commandId) :
		m_journal(bracketsEdit(journal, mutating, commandId) ? journal : nullptr)
	{
		if (m_journal != nullptr) { m_journal->beginEdit(); }
	}
	~EditBracket()
	{
		if (m_journal != nullptr) { m_journal->endEdit(); }
	}
	EditBracket(const EditBracket&) = delete;
	EditBracket& operator=(const EditBracket&) = delete;

private:
	ProjectJournal* m_journal;
};

//! WF-044: whether a mutating call would edit a locked track or clip (EditLocks), with the reason in @a why.
bool lockedOut(bool mutating, const QString& commandId, const QJsonObject& args, QString* why)
{
	return mutating && EditLocks::refuses(commandId, args, why);
}

/*! Whether THIS call records the step it changed. A mutating command another
 *  mutating command runs inside itself is part of that outer step (the journal
 *  merges both into ONE checkpoint run, ED-056), so only the call whose enclosing
 *  handlers are all non-mutating records. A non-mutating wrapper (range.consolidate,
 *  arrangement.nudge, workflow.run) records nothing of its own, so each mutating
 *  command it runs still records itself - that is the contract those rows state. */
bool recordsItsOwnStep(bool mutating, bool changed, bool enclosedByMutating)
{
	return mutating && changed && !enclosedByMutating;
}

/*! Whether a nested call's own record is folded into an enclosing mutating step while
 *  carrying a descriptor that step does not keep. */
bool foldsIntoEnclosingStep(bool mutating, bool changed, bool enclosedByMutating, const QJsonObject& result)
{
	return mutating && changed && enclosedByMutating && result.contains(QStringLiteral("__transaction"));
}

//! A script can return an error after it has changed the project and kept its whole-session undo step.
bool countsAsChangedResult(const ControlResult& result, const QString& commandId, quint64 step)
{
	return result.ok || (commandId == QLatin1String("script.run") && step != 0
		&& result.result.contains(QStringLiteral("__transaction")));
}

/*! The record an outer mutating call writes for a step that also holds nested
 *  changes whose own inverse was not kept (the nested record was folded into this
 *  step): when the outer handler supplied no descriptor, it must not claim the
 *  journal step alone reverses the nested change. Refuses to claim more than it knows. */
void markFoldedNestedChanges(ControlResult* result, int folded, int foldedBefore)
{
	if (folded == foldedBefore) { return; }
	// The outer record keeps its own descriptor (before, inverse) but may not claim more than
	// the step covers: a nested change folded into it is not reversed by that descriptor.
	QJsonObject descriptor = result->result.value(QStringLiteral("__transaction")).toObject();
	// A whole-session checkpoint (script.run, Q050-044) restores everything its nested commands did.
	if (descriptor.value(QStringLiteral("covers_nested")).toBool(false)) { return; }
	const QString prior = descriptor.value(QStringLiteral("mechanism")).toString();
	descriptor.insert(QStringLiteral("reversible"), false);
	descriptor.insert(QStringLiteral("mechanism"), QStringLiteral("a command this call ran inside itself changed the "
		"project and its own inverse is not part of this record, so this step is not reported as reversible")
		+ (prior.isEmpty() ? QString() : QStringLiteral(" (") + prior + QStringLiteral(")")));
	result->result.insert(QStringLiteral("__transaction"), descriptor);
}

} // namespace

ControlResult ControlRegistry::runHandler(
	const std::function<ControlResult(const QJsonObject&)>& handler, const QString& commandId,
	bool mutating, const QJsonObject& args)
{
	// 0.5.0 L12 (WF-044): a mutating command that would edit a locked track or clip is refused before
	// its handler runs, so a lock holds for every client - socket, Lua, menus - alike.
	QString locked;
	if (lockedOut(mutating, commandId, args, &locked)) { return ControlResult::failure(ControlErrorKind::Refused, locked); }

	ProjectJournal* journal = Engine::projectJournal();
	const quint64 serialBefore = journal != nullptr ? journal->topStepSerial() : 0;

	// Who else is running: a nested mutating call is part of its outermost mutating
	// ancestor's step, and the count of folded nested records tells that ancestor
	// whether its step holds a change it cannot describe by itself.
	const bool enclosedByMutating = m_mutatingDepth > 0;
	const bool enclosedByWrapper = m_wrapperDepth > 0;
	const int foldedBefore = m_foldedNestedRecords;
	m_mutatingDepth += static_cast<int>(mutating);
	m_wrapperDepth += static_cast<int>(!mutating);
	ControlResult result;
	{
		/*! Restores the three running-handler counts on EVERY exit. A handler that
		 *  throws (a filesystem write, a plug-in load) unwinds past a straight-line
		 *  decrement and an outer dispatch layer catches it: a leaked depth then
		 *  silences the observer for every later top-level edit and a leaked class
		 *  count makes the next one look nested or wrapped, so its record is never
		 *  written. */
		struct DepthGuard
		{
			int& depth;
			int& mutatingDepth;
			int& wrapperDepth;
			const bool mutating;
			DepthGuard(int& dispatchDepth, int& mutatingCount, int& wrapperCount, bool isMutating) :
				depth(dispatchDepth), mutatingDepth(mutatingCount), wrapperDepth(wrapperCount), mutating(isMutating)
			{
				++depth;
			}
			~DepthGuard()
			{
				--depth;
				mutatingDepth -= static_cast<int>(mutating);
				wrapperDepth -= static_cast<int>(!mutating);
			}
		};
		const EditBracket edit(journal, mutating, commandId);
		DepthGuard depthGuard{m_depth, m_mutatingDepth, m_wrapperDepth, mutating};
		result = handler(args);
	}

	bool coalesced = false;
	quint64 step = 0;
	if (journal != nullptr)
	{
		// Merge by serial, not by stack index: a bounded journal can evict entries
		// while this handler runs, but every checkpoint pushed after serialBefore
		// is still exactly this command's window.
		// THEN the coalescing rule, which can merge that step into the previous
		// command's when this call continues the same gesture.
		journal->mergeCheckpointsSince(serialBefore);
		// Did THIS call produce a step? Asked by SERIAL, not by depth: a bound
		// can evict steps while the handler runs, so the depth can be exactly
		// what it was before the call even though a step was pushed. (Found by
		// measurement - docs/UNDO-BOUNDS.md, "the step serial".)
		const quint64 produced = journal->topStepSerial();
		if (produced != serialBefore)
		{
			step = produced;
			// A call a wrapper runs is never coalesced: its steps are merged into the wrapper's ONE step
			// (flushNestedRecords). Coalescing one into a step BEFORE the wrapper would leave that step's
			// record covering it twice.
			coalesced = mayCoalesce(commandId, enclosedByWrapper, enclosedByMutating) && coalesceStepOf(*journal, commandId, args, true);
		}
	}
	// A dry run changes NOTHING and so must leave the journal exactly as it found
	// it: recording one put a non-reversible record on top of the last real step,
	// and the next control.undo refused 'irreversible' while the edit the caller
	// meant to take back stayed in place (BUG-DRYRUN-UNDO). The three preview
	// handlers share the one `dry_run: true` result field, so the seam is here.
	const bool dryRunPreview = result.result.value(QStringLiteral("dry_run")).toBool(false);
	// Review W3-batchA #8: a call that changed nothing (a report-only call, a value already set) records nothing.
	const bool changed = countsAsChangedResult(result, commandId, step) && !dryRunPreview
		&& !changedNothing(journal != nullptr, step, commandId, result);
	recordCall(commandId, args, &result, coalesced, step, mutating, changed, enclosedByMutating, enclosedByWrapper, foldedBefore);
	control::finishResult(commandId, m_commands.value(commandId).resultSchema, &result);
	notifyObserver(commandId, args, mutating, changed);
	collab::CommandTap::observed(commandId, args, mutating, changed, m_depth);  // F6: what a host streams to its peers
	return result;
}

//! Records, or holds for its wrapper, the step of one finished call (see runHandler for the rules).
void ControlRegistry::recordCall(const QString& commandId, const QJsonObject& args, ControlResult* result,
	bool coalesced, quint64 step, bool mutating, bool changed, bool enclosedByMutating, bool enclosedByWrapper,
	int foldedBefore)
{
	m_foldedNestedRecords += static_cast<int>(foldsIntoEnclosingStep(mutating, changed, enclosedByMutating, result->result));
	const bool ownStep = recordsItsOwnStep(mutating, changed, enclosedByMutating);
	// A recorded call that produced NO journal step breaks a coalescing run: its record is not a step the run
	// can extend, so the next press must open its own step and record (a call that records nothing leaves the
	// run alone, as documented in ControlUndoCoalescing.cpp).
	if (ownStep && step == 0) { m_coalescer.breakRun(); }
	if (ownStep && enclosedByWrapper)
	{
		// Held, with its descriptor, until the wrapper's journal step is known (see flushNestedRecords).
		// A folded nested change is flagged here too, before the descriptor is kept.
		markFoldedNestedChanges(result, m_foldedNestedRecords, foldedBefore);
		m_nestedRecords.append(NestedRecord{commandId, *result, args, step});
	}
	else if (ownStep)
	{
		markFoldedNestedChanges(result, m_foldedNestedRecords, foldedBefore);
		recordTransactionOf(commandId, result, coalesced, step);
	}
	if (!mutating && m_wrapperDepth == 0 && !m_nestedRecords.isEmpty()) { flushNestedRecords(commandId, *result, step); }
}


//! Records what a successful mutating handler described (SPEC A16).
void ControlRegistry::recordTransactionOf(const QString& commandId, ControlResult* result,
	bool coalesced, quint64 step)
{
	// One record per UNDO STEP, not per call: when the coalescing rule merged
	// this call's step into the previous run's, the record that run started is
	// extended and no second record is written. Nothing this call reported is
	// lost - `before` is still the state before the gesture and `inverse` still
	// reverts all of it - so the count is the only thing that changes. The
	// document's provenance extends with it (SPEC-ARCH-4 1.9: one <z:change>
	// per recorded change, and a coalesced run is the change it extends).
	if (coalesced)
	{
		extendTopTransaction();
		provenance::extendTopChange(result->result);
		return;
	}

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
		// Review W3-batchA A5: a true-inverse command whose call left an undo step on the journal is reversed by
		// that step (control.undo), payload or not; only a call that left none is recorded without an inverse.
		const bool journalled = step != 0 && honest.cls == QLatin1String("true_inverse");
		honest.reversible = journalled;
		honest.mechanism = (journalled ? QStringLiteral("the undo step this call left on the journal (control.undo reverses it); ")
			: QStringLiteral("this command recorded no inverse or snapshot; ")) + honest.mechanism;
		recordTransaction(honest);
		// SPEC-ARCH-4 1.9 (ARCH-4 S6): BOTH exits below feed the document's
		// append-only <z:provenance>, so a change reaches the file exactly
		// when one was recorded here - the additive rule, one seam.
		provenance::recordChange(commandId, recorded.value(QStringLiteral("before")).toObject(),
			result->result);
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
	provenance::recordChange(commandId, tx.before, result->result);
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






ControlRegistry::ShutdownHookId ControlRegistry::addShutdownHook(std::function<void()> hook)
{
	const ShutdownHookId id = nextShutdownHookId();
	shutdownHooks().append(ShutdownHookEntry{id, std::move(hook)});
	return id;
}


bool ControlRegistry::removeShutdownHook(ShutdownHookId id)
{
	if (id == 0)
	{
		return false;
	}
	QVector<ShutdownHookEntry>& hooks = shutdownHooks();
	for (int i = 0; i < hooks.size(); ++i)
	{
		if (hooks.at(i).id == id)
		{
			hooks.remove(i);
			return true;
		}
	}
	return false;
}


int ControlRegistry::shutdownHookCount() const
{
	return shutdownHooks().size();
}


void ControlRegistry::runShutdownHooks()
{
	// Take the list and clear it BEFORE running anything: a hook that
	// re-registers (a re-listen) must not extend the run it is inside, and a
	// hook that quits or aborts must not leave the store half-consumed for the
	// next caller on the way out.
	const QVector<ShutdownHookEntry> hooks = shutdownHooks();
	shutdownHooks().clear();
	for (const ShutdownHookEntry& entry : hooks)
	{
		if (entry.hook) { entry.hook(); }
	}
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


} // namespace lmms
