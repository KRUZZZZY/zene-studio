/*
 * ControlCommandsControl.cpp - the control.* command group (SPEC A11-A16).
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

#include <cstdio>

#include <optional>

#include <QJsonArray>
#include <QJsonObject>

#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlStructuralSupport.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "RenderQueue.h"
#include "Song.h"
#include "UndoHistory.h"
#include "lmmsversion.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The control protocol version (AGENT-TOOLING.md #2).
constexpr int ControlProtocolVersion = 1;

//! The engine's own view of the audio device. A client must never be told a bare
//! "ready" while the instance cannot make a sound (task #626): the fallback is
//! announced here, with the device that failed and the one in use. The SHAPE is
//! stable - every field is always present - because a client may poll this before
//! the engine object exists at all, and a checker cannot read optional keys.
QJsonObject audioReport()
{
	QJsonObject out;
	out.insert(QStringLiteral("state"), QStringLiteral("engine_missing"));
	out.insert(QStringLiteral("device"), QString());
	out.insert(QStringLiteral("requested"), QString());
	out.insert(QStringLiteral("start_failed"), false);
	out.insert(QStringLiteral("sound_output"), false);
	out.insert(QStringLiteral("play_handle_overflow"), QJsonObject{
		{QStringLiteral("engine_handles_dropped"), 0},
		{QStringLiteral("bus_edits_dropped"), 0},
		{QStringLiteral("worker_buffer_overflows"), 0}});
	out.insert(QStringLiteral("slicert_state_overflow"), 0);
	out.insert(QStringLiteral("sample_voice_overflow"), 0);
	out.insert(QStringLiteral("sample_voice_queue_invariant_failures"), 0);
	AudioEngine* audio = Engine::audioEngine();
	if (audio == nullptr) { return out; }

	const bool failed = audio->audioDevStartFailed();
	out.insert(QStringLiteral("state"),
		failed ? QStringLiteral("dummy_fallback") : QStringLiteral("ok"));
	out.insert(QStringLiteral("device"), audio->audioDevName());
	out.insert(QStringLiteral("requested"), audio->audioDevRequestName());
	out.insert(QStringLiteral("start_failed"), failed);
	out.insert(QStringLiteral("sound_output"), !failed);
	out.insert(QStringLiteral("play_handle_overflow"), QJsonObject{
		{QStringLiteral("engine_handles_dropped"),
			static_cast<qint64>(audio->droppedPlayHandles())},
		{QStringLiteral("bus_edits_dropped"),
			static_cast<qint64>(audio->droppedPlayHandleEdits())},
		{QStringLiteral("worker_buffer_overflows"), static_cast<qint64>(
			AudioEngineWorkerThread::deferredPlayHandleEditOverflowCount())}});
	out.insert(QStringLiteral("slicert_state_overflow"),
		static_cast<qint64>(audio->slicerTStateOverflows()));
	out.insert(QStringLiteral("sample_voice_overflow"), static_cast<qint64>(audio->sampleVoiceOverflows()));
	out.insert(QStringLiteral("sample_voice_queue_invariant_failures"),
		static_cast<qint64>(audio->sampleVoiceQueueInvariantFailures()));
	if (failed)
	{
		out.insert(QStringLiteral("message"), audio->audioDevStartReason());
	}
	return out;
}

//! control.ping's result: liveness, readiness, and - while not ready - why.
QJsonObject pingResult()
{
	const ControlRegistry::ReadinessReport state = ControlRegistry::readinessReport();
	QJsonObject result;
	result.insert(QStringLiteral("pong"), true);
	result.insert(QStringLiteral("engine_ready"), state.ready);
	result.insert(QStringLiteral("version"), QString::fromUtf8(LMMS_VERSION));
	result.insert(QStringLiteral("proto"), ControlProtocolVersion);
	result.insert(QStringLiteral("render_queue_capacity_refusals"),
		static_cast<double>(RenderQueue::instance().capacityRefusals()));
	if (!state.ready)
	{
		QJsonObject reason;
		reason.insert(QStringLiteral("code"), state.code);
		reason.insert(QStringLiteral("message"), state.message);
		result.insert(QStringLiteral("reason"), reason);
	}
	result.insert(QStringLiteral("audio"), audioReport());
	return result;
}

//! control.quit's handler. Split out of the registration below so the CCN
//! ratchet measures the decision, not the declaration.
ControlResult handleQuit(const QJsonObject& args)
{
	const bool save = args.value(QStringLiteral("save")).toBool(false);
	Song* song = Engine::getSong();
	const bool modified = song != nullptr && song->isModified();
	const bool hasFile = song != nullptr && !song->projectFileName().isEmpty();

	// Saving without a file name would open the interactive Save-As dialog - a
	// hang in a headless run. Refuse, typed and actionable.
	if (save && !hasFile)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("control.quit with save:true needs a project file; call project.save "
				"first, or call control.quit with save:false to discard the unsaved changes"));
	}

	QJsonObject result;
	result.insert(QStringLiteral("quitting"), true);
	result.insert(QStringLiteral("save_requested"), save);
	result.insert(QStringLiteral("project_modified"), modified);
	result.insert(QStringLiteral("unsaved_changes"),
		(!save && modified) ? QStringLiteral("discarded") : QStringLiteral("none"));

	// The real fix for #626 is here: the GUI's own quit questions (the "project
	// was modified" QMessageBox that MainWindow::closeEvent shows) are answered
	// from this intent instead of by a dialog nobody can click, and a request
	// that arrives before startup has finished is remembered and applied by
	// main() once the engine is ready. There is no "did the event loop stop?"
	// workaround any more; the bounded last-resort guard lives in
	// ControlRegistry::scheduleQuit().
	ControlRegistry::requestQuit(save ? ControlRegistry::QuitPromptAnswer::Save
									  : ControlRegistry::QuitPromptAnswer::Discard);
	return ControlResult::success(result);
}

void registerPingCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.ping");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("ping");
	cmd.description = QStringLiteral("Liveness probe; also reports whether the engine is addressable "
		"yet, why not when it is not, and how many render jobs were refused at the bounded queue capacity.");
	cmd.requiresEngine = false;
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("pong"), booleanProperty()},
		{QStringLiteral("engine_ready"), booleanProperty()},
		{QStringLiteral("version"), stringProperty()},
		{QStringLiteral("proto"), integerProperty()},
		{QStringLiteral("render_queue_capacity_refusals"), integerProperty()},
		// present only while engine_ready is false: {code, message}
		{QStringLiteral("reason"), objectProperty()},
		// always present: the device actually in use and whether it makes sound
		{QStringLiteral("audio"), objectProperty()},
	});
	cmd.handler = [](const QJsonObject&) { return ControlResult::success(pingResult()); };
	registry.registerCommand(cmd);
}

void registerVersionCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.version");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("version");
	cmd.description = QStringLiteral("The product version string and the control protocol version.");
	cmd.requiresEngine = false;
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("version"), stringProperty()},
		{QStringLiteral("proto"), integerProperty()},
	});
	cmd.handler = [](const QJsonObject&) {
		QJsonObject result;
		result.insert(QStringLiteral("version"), QString::fromUtf8(LMMS_VERSION));
		result.insert(QStringLiteral("proto"), ControlProtocolVersion);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerCommandsListCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.commands_list");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("commands_list");
	cmd.description = QStringLiteral("Every registered command with its schemas and requires declaration.");
	cmd.requiresEngine = false;
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("commands"), arrayProperty()},
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("proto"), numberProperty()},
	});
	cmd.handler = [&registry](const QJsonObject&) { return ControlResult::success(registry.describeAll()); };
	registry.registerCommand(cmd);
}

void registerTransactionsCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.transactions");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("transactions");
	cmd.description = QStringLiteral("The transactions recorded for mutating commands (SPEC A16 "
		"hook), each with the contract table's class and the serialised size of the record, "
		"plus the bounds they are kept within (cap_records/cap_bytes, retained_bytes, "
		"evicted, capped).");
	cmd.requiresEngine = false;
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("transactions"), arrayProperty()},
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("retained_bytes"), integerProperty()},
		{QStringLiteral("cap_records"), integerProperty()},
		{QStringLiteral("cap_bytes"), integerProperty()},
		{QStringLiteral("evicted"), integerProperty()},
		{QStringLiteral("capped"), booleanProperty()},
	});
	cmd.handler = [&registry](const QJsonObject&) {
		return ControlResult::success(registry.transactionsReport());
	};
	registry.registerCommand(cmd);
}

//! The message a refused undo carries: which command, why it has no inverse,
//! and what to do instead. A refusal that does not name the fallback is a dead
//! end for an agent, so the fallback is not optional here.
QString irreversibleUndoMessage(const ControlRegistry::Transaction& tx,
	const control::ReversibilityEntry* entry)
{
	QString message = QStringLiteral("cannot undo '%1' (class '%2'): ").arg(tx.command, tx.cls);
	message += (entry != nullptr && !entry->reason.isEmpty())
		? entry->reason
		: QStringLiteral("no inverse is recorded for it");
	if (!tx.mechanism.isEmpty()) { message += QStringLiteral(" [") + tx.mechanism + QLatin1Char(']'); }
	const QString fallback = entry == nullptr ? QString() : entry->fallback;
	message += fallback.isEmpty()
		? QStringLiteral("; there is no fallback: the change cannot be reversed")
		: QStringLiteral("; instead: ") + fallback;
	return message;
}

/*! E19: the refusal for a record whose inverse is the journal but whose call left NO
 *  step on it. Unwinding the journal then would take back an OLDER edit than the one
 *  asked about - an inverse that "depends entirely on a checkpoint existing" is
 *  one-way when none exists, and says so rather than pretending.
 */
QString unjournalledMessage(const ControlRegistry::Transaction& record)
{
	return QStringLiteral("cannot undo '%1': its inverse is an undo step on the project journal, but the "
		"call left none there (nothing it changed was checkpointed), so unwinding the journal now would "
		"take back an OLDER edit than the one asked for. Its before-state is in control.transactions; "
		"re-drive the previous values by hand").arg(record.command);
}

/*! The typed failure for the journal step that just ran, when its device recreate
 *  did not happen, or nullopt (BUG-CTL-1, SPEC A16).
 *
 *  A structural step's callback cannot return a failure - the journal's callbacks
 *  are `void` and undo()/redo() are `void` - so a recreate that could not put the
 *  captured settings back records it in the structural-support layer instead. The
 *  caller clears the record BEFORE it runs the step, so only a failure THIS step
 *  recorded is reported; the read consumes it, so nothing leaks into a later call.
 *  Undo and redo answer alike: a redo that re-creates devices (plugin.copy,
 *  chain.copy, rack.wrap) can fail the same way, and reporting `redone: true`
 *  over a device that is not there is the same lie.
 */
std::optional<ControlResult> restoreFailureOf(const QString& verb, const QString& command)
{
	const std::optional<ControlResult> failed = takeStructuralRestoreFailure();
	if (!failed) { return std::nullopt; }
	// The step left its stack, but the device was NOT re-created: the half-restored
	// instance is already gone, so what remains is the honest state, and the
	// command's captured settings document is the bounded record that allows a
	// rebuild by hand.
	const QString what = command.isEmpty()
		? QStringLiteral("the last journal step")
		: QStringLiteral("'%1'").arg(command);
	return ControlResult::failure(failed->errorKind,
		QStringLiteral("cannot %1 %2: the recorded step ran but the device was NOT "
			"restored - %3. The half-restored instance was removed again, so no "
			"default-parameters device is left behind and the device stays absent; the "
			"command's 'before.state_xml' is the bounded record that allows a rebuild by "
			"hand").arg(verb, what, failed->errorMessage));
}

/*! The typed failure for the journal step that just ran when its recorded action
 *  did not complete, or nullopt.
 *
 *  Journal action callbacks are `void`, so an action whose inverse failed has no
 *  return path to the command that asked for undo or redo. `Refused` reports that
 *  the recorded action ran but its prior state was not restored.
 */
std::optional<ControlResult> actionRestoreFailureOf(const QString& verb, const QString& command)
{
	const QString reason = takeActionRestoreFailure();
	if (reason.isEmpty()) { return std::nullopt; }
	const QString what = command.isEmpty()
		? QStringLiteral("the last journal step")
		: QStringLiteral("'%1'").arg(command);
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("cannot %1 %2: the recorded action was NOT restored - %3")
			.arg(verb, what, reason));
}

//! Unwinds one step of the engine's own undo stack.
//!
//! \a commands is how many COMMANDS that one step covered: 1 for an ordinary
//! edit, more when the step is a coalesced run of the same command on the same
//! target (the drag rule, docs/UNDO-BOUNDS.md). It travels in the reply as
//! `undone_commands`, because `undone_command` alone reads as "one command was
//! undone" and a merged span is NOT one command: BUG-CTL-4 was an agent that
//! saw the singular claim, read the state back, and found two edits reverted.
ControlResult undoThroughJournal(const QString& command, int commands = 1,
	quint64 stepSerial = 0, bool unrecordedEdit = false)
{
	ProjectJournal* journal = Engine::projectJournal();
	clearStructuralRestoreFailure();  // see restoreFailureOf
	clearActionRestoreFailure();      // as above: only THIS step's failure is reported
	bool undone = false;
	if (journal != nullptr && journal->canUndo())
	{
		journal->undo();
		undone = true;
	}
	if (std::optional<ControlResult> failed = restoreFailureOf(QStringLiteral("undo"), command)) { return *failed; }
	if (std::optional<ControlResult> failed = actionRestoreFailureOf(QStringLiteral("undo"), command)) { return *failed; }
	QJsonObject result;
	result.insert(QStringLiteral("undone"), undone);
	result.insert(QStringLiteral("undone_command"), command);
	// How many commands that one step covered: 1 normally, more for a coalesced
	// run (the drag rule). Never implied away - the count is what tells an agent
	// "one command" from "a merged span" (BUG-CTL-4).
	result.insert(QStringLiteral("undone_commands"), commands);
	result.insert(QStringLiteral("mechanism"), QStringLiteral("lmms::ProjectJournal"));
	result.insert(QStringLiteral("can_undo"), journal != nullptr && journal->canUndo());
	result.insert(QStringLiteral("can_redo"), journal != nullptr && journal->canRedo());
	if (unrecordedEdit)
	{
		result.insert(QStringLiteral("unrecorded_edit"), true);
		result.insert(QStringLiteral("step_serial"), static_cast<double>(stepSerial));
	}
	if (!undone)
	{
		result.insert(QStringLiteral("reason"), QStringLiteral("the engine's undo stack is empty"));
	}
	return ControlResult::success(result);
}

//! The undo of @a record by dispatching its inverse COMMAND (a file revision, say), or nullopt when its inverse is not a
//! registered command and the journal is the way back. @a record is a copy - see undoLastCommand for why.
std::optional<ControlResult> undoByCommand(ControlRegistry& registry, const ControlRegistry::Transaction& record)
{
	const QJsonObject inverse = record.inverse;
	const QString op = inverse.value(QStringLiteral("op")).toString();
	const QString applies = inverse.value(QStringLiteral("applies")).toString(QStringLiteral("journal"));
	if (applies != QLatin1String("command") || !registry.hasCommand(op)) { return std::nullopt; }
	// The inverse command is dispatched like any other call, so its run records its own transaction: a second
	// control.undo then reverses it (the established contract, AGENTS.md "Verified facts" and docs/UNDO-BOUNDS.md).
	const ControlResult applied = registry.invoke(op, inverse.value(QStringLiteral("args")).toObject());
	if (!applied.ok) { return applied; }
	QJsonObject result;
	result.insert(QStringLiteral("undone"), true);
	result.insert(QStringLiteral("undone_command"), record.command);
	result.insert(QStringLiteral("undone_commands"), record.commands);
	result.insert(QStringLiteral("class"), record.cls);
	result.insert(QStringLiteral("restored_by"), op);
	result.insert(QStringLiteral("inverse_result"), applied.result);
	return ControlResult::success(result);
}

/*! WF-033 (docs/UNDO-HISTORY-PERSISTENCE.md): a project opened WITH its saved undo history. The open itself
 *  cannot be undone and its record says so, but the steps it loaded can - and while that record is still the
 *  newest, nothing recorded has been pushed above them, so the journal's newest step is one of them. */
bool undoesLoadedHistory(const ControlRegistry::Transaction& top)
{
	const quint64 loaded = UndoHistory::instance().loadedTopSerial();
	const ProjectJournal* journal = Engine::projectJournal();
	return loaded != 0 && top.step == loaded && journal != nullptr && journal->canUndo();
}

//! The journal's newest edit belongs to a control transaction only when its serial matches that record.
bool unrecordedJournalTop(const ControlRegistry::Transaction& record, const ProjectJournal* journal)
{
	return record.step != 0 && journal != nullptr && journal->canUndo()
		&& journal->topStepSerial() != record.step;
}

//! The answer when the newest record cannot be undone: the steps a project.open loaded (above), else the typed
//! refusal naming the command and its fallback. nullopt when the record is reversible.
std::optional<ControlResult> answerIrreversibleTop(const ControlRegistry::Transaction& top)
{
	if (top.reversible) { return std::nullopt; }
	if (undoesLoadedHistory(top)) { return undoThroughJournal(QString()); }
	return ControlResult::failure(ControlErrorKind::Irreversible,
		irreversibleUndoMessage(top, control::ReversibilityTable::instance().lookup(top.command)));
}

//! Undo a record through the journal, identifying an intervening edit by its own step serial.
ControlResult undoRecordedJournalStep(const ControlRegistry::Transaction& record, ProjectJournal* journal)
{
	// An asynchronous command may leave its journal step after it answered.
	const bool stepLater = record.inverse.value(QStringLiteral("applies")).toString() == QLatin1String("journal_later");
	if (record.step == 0 && journal != nullptr && !stepLater)
	{
		return ControlResult::failure(ControlErrorKind::Irreversible, unjournalledMessage(record));
	}
	if (unrecordedJournalTop(record, journal))
	{
		return undoThroughJournal(QStringLiteral("unrecorded edit"), 1, journal->topStepSerial(), true);
	}
	return undoThroughJournal(record.command, record.commands);
}

/*! control.undo: undo THE LAST AGENT COMMAND, or refuse typed.
 *
 * Three cases, in this order, and the order is the contract:
 *  1. no mutating command has been recorded -> unwind the engine journal.
 *  2. the last recorded command is not reversible -> TYPED failure naming the
 *     command, its class and its documented fallback. The journal is NOT
 *     touched: silently undoing an older command while the last one cannot be
 *     undone is the pretending SPEC A16 exists to remove.
 *  3. otherwise undo it - by dispatching the recorded inverse command when the
 *     inverse is a command (a file revision, say), else by unwinding the
 *     engine's own ProjectJournal, which the GUI's Ctrl+Z drives too.
 */
ControlResult undoLastCommand(ControlRegistry& registry)
{
	const ControlRegistry::Transaction* top = registry.lastTransaction();
	if (top == nullptr)
	{
		return undoThroughJournal(QString());
	}
	if (std::optional<ControlResult> answered = answerIrreversibleTop(*top)) { return *answered; }

	// The stack is BOUNDED (task #623), so a record can outlive the step it
	// describes: a lower cap, or the byte budget, evicts the oldest steps while
	// the record list keeps its own. Unwinding then would take back a LATER edit
	// than the one asked about - the exact pretending SPEC A16 exists to remove -
	// so this REFUSES, typed, and names the bound that did it. `step == 0` is a
	// record whose inverse is a command rather than a step (a file revision):
	// it has no step to have lost.
	ProjectJournal* journal = Engine::projectJournal();
	if (top->step != 0 && journal != nullptr && top->step < journal->oldestStepSerial())
	{
		return ControlResult::failure(ControlErrorKind::Irreversible,
			QStringLiteral("cannot undo '%1': the undo step this record describes was EVICTED by the "
				"stack's bound (cap %2 steps / %3 bytes), so undoing now would take back a LATER edit "
				"than the one asked for. Read control.undo_depth's evicted/bounded fields to see the "
				"bound at work, raise the caps with control.set_undo_depth, or re-drive the edit")
				.arg(top->command)
				.arg(journal->maxUndoStates())
				.arg(journal->maxUndoBytes()));
	}

	// The record is COPIED, and never carried by pointer, across the dispatch
	// below. That is the whole reason this variable exists:
	//   * ControlRegistry::lastTransaction() hands out &m_transactions.last() - a
	//     pointer INTO the registry's QVector<Transaction>.
	//   * A mutating inverse records a transaction of its OWN while it runs
	//     (ControlRegistry::runHandler -> recordTransactionOf -> recordTransaction ->
	//     m_transactions.append; ControlCommandsPluginScanEdit registers
	//     plugin.scan_cache_quarantine_remove as mutating=true), and an append may
	//     REALLOCATE that vector.
	//   * So `top` dangles the moment the inverse is dispatched, and every read
	//     through it afterwards is a use-after-free whose visible outcome belongs to
	//     the allocator rather than to this code: glibc tends to leave the freed
	//     bytes readable, while Darwin's allocator may poison or reuse them.
	// This is the ONE branch that re-enters the registry (applies=command), it is the
	// first control.undo over such a record that reaches it, and it is where
	// ControlPluginScanCommands lost its instance on BOTH macOS jobs of run
	// 34870198514: the engine closed the control socket on that undo, with no reply
	// and nothing written about the cause. Copying costs a handful of refcounts.
	const ControlRegistry::Transaction record = *top;
	if (std::optional<ControlResult> applied = undoByCommand(registry, record)) { return *applied; }
	return undoRecordedJournalStep(record, journal);
}

void registerUndoCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.undo");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("undo");
	cmd.description = QStringLiteral("Undo the last agent command: the engine's own "
		"ProjectJournal step it recorded (the same stack the GUI's Ctrl+Z unwinds), or the "
		"recorded inverse command when the change is file-level. If the last recorded command "
		"has no inverse, this FAILS with the typed 'irreversible' error and names the "
		"documented fallback instead of undoing an older command. One call unwinds ONE undo "
		"STEP, and one step is one command EXCEPT under the coalescing rule "
		"(docs/UNDO-BOUNDS.md): a run of the same command on the same target inside the "
		"window - reads in between do not break it - is one step, so the reply's "
		"'undone_commands' is 1 normally and more for a merged span. Set the window to 0 "
		"(control.set_undo_coalescing) for strict one-command-per-call granularity. If the journal's "
		"top step has no matching transaction record, the reply identifies it as 'unrecorded edit' "
		"and includes its step_serial; transaction history is left intact.");
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("undone"), booleanProperty()},
		{QStringLiteral("undone_command"), stringProperty()},
		{QStringLiteral("undone_commands"), integerProperty()},
		{QStringLiteral("unrecorded_edit"), booleanProperty()},
		{QStringLiteral("step_serial"), numberProperty()},
		{QStringLiteral("class"), stringProperty()},
		{QStringLiteral("restored_by"), stringProperty()},
		{QStringLiteral("can_undo"), booleanProperty()},
		{QStringLiteral("can_redo"), booleanProperty()},
		{QStringLiteral("mechanism"), stringProperty()},
		{QStringLiteral("reason"), stringProperty()},
		{QStringLiteral("inverse_result"), objectProperty()},
	});
	cmd.handler = [&registry](const QJsonObject&) { return undoLastCommand(registry); };
	registry.registerCommand(cmd);
}

void registerRedoCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.redo");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("redo");
	cmd.description = QStringLiteral("Redo the last undone journal checkpoint. A structural "
		"step whose inverse was a one-way action has nothing to redo, and says so: the redo "
		"stack is emptied at that point rather than replaying an older step. A redo that "
		"re-creates a device which cannot be built again FAILS typed, as control.undo does, "
		"instead of answering 'redone'.");
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("redone"), booleanProperty()},
		{QStringLiteral("can_undo"), booleanProperty()},
		{QStringLiteral("can_redo"), booleanProperty()},
	});
	cmd.handler = [](const QJsonObject&) {
		auto* journal = Engine::projectJournal();
		bool redone = false;
		QString declined;
		clearStructuralRestoreFailure();  // as control.undo: only THIS step's failure is reported
		clearActionRestoreFailure();
		if (journal != nullptr && journal->canRedo())
		{
			journal->redo();
			// A redo that changed nothing is declined by its step: it is not redone, and the step stays on the redo stack.
			declined = journal->takeRedoDecline();
			redone = declined.isEmpty();
		}
		if (std::optional<ControlResult> failed = restoreFailureOf(QStringLiteral("redo"), QString())) { return *failed; }
		if (std::optional<ControlResult> failed = actionRestoreFailureOf(QStringLiteral("redo"), QString())) { return *failed; }
		if (!declined.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("cannot redo: %1. Nothing was changed, so no undo step was recorded and the step "
					"stays on the redo stack").arg(declined));
		}
		QJsonObject result;
		result.insert(QStringLiteral("redone"), redone);
		result.insert(QStringLiteral("can_undo"), journal != nullptr && journal->canUndo());
		result.insert(QStringLiteral("can_redo"), journal != nullptr && journal->canRedo());
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerQuitCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.quit");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("quit");
	cmd.description = QStringLiteral("Ask the instance to run its normal shutdown (the reply is sent "
		"first). Unsaved changes are discarded unless save is true.");
	cmd.requiresEngine = false;
	cmd.argsSchema = objectSchema({
		// false (the default): discard unsaved changes. true: save the current
		// project to its existing file first (refused when it has none).
		{QStringLiteral("save"), booleanProperty()},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("quitting"), booleanProperty()},
		{QStringLiteral("save_requested"), booleanProperty()},
		{QStringLiteral("project_modified"), booleanProperty()},
		{QStringLiteral("unsaved_changes"), stringProperty()},
	});
	cmd.handler = handleQuit;
	registry.registerCommand(cmd);
}

} // namespace

void registerControlGroupCommands(ControlRegistry& registry)
{
	registerPingCommand(registry);
	registerVersionCommand(registry);
	registerCommandsListCommand(registry);
	registerTransactionsCommand(registry);
	registerUndoCommand(registry);
	registerRedoCommand(registry);
	registerQuitCommand(registry);
	registerIdContractCommand(registry);
	// The bounded-undo slice (task #623): the depth, the two caps and the
	// coalescing window. Its own translation unit because this file is at the
	// file-length ratchet's limit; the same group either way.
	registerUndoBoundsCommands(registry);
	registerUndoHistoryCommands(registry);
}

} // namespace lmms
