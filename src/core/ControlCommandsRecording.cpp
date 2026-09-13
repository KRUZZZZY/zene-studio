/*
 * ControlCommandsRecording.cpp - the record.* group's JOURNALLING half: the
 *                                verbs that write, advance and retire a take's
 *                                crash-recovery side file (0.3.0).
 *
 * The engine half is include/RecordingJournal.h (the side file beside a take:
 * its format, its scanner and the BOUND it states) and TrackRecorder, whose
 * arm()/disarm() write and retire that journal around a real capture - so "a
 * recording that is in progress is journalled to disk" is a property of the
 * capture path, not of this group. What was missing is the AGENT SURFACE:
 * without commands a crashed capture could only be found by knowing the file
 * naming convention and reading the side file by hand, which is exactly the gap
 * AGENT-TOOLING.md section 1 makes a defect.
 *
 * `record.` is the family AGENT-TOOLING.md's boarded list already reserves for
 * the recording path (`record.punch_set`), and this group is the one that names
 * it. The recovery half of the group - what the next start does with a journal
 * an abnormal exit left behind - is ControlCommandsRecordingRecovery.cpp; the
 * two are one group and two translation units for the same reason the warp and
 * automation groups split (the file-length ratchet measures a file).
 *
 * WHAT IS PROVEN, and what is NOT, stated where the claim is:
 *  - a journal written for a take is found by the next start and the material
 *    it names is measured against the take's own bytes;
 *  - the reported recoverable count is `min(journal, file)`, so it can never
 *    promise audio that is not on disk;
 *  - control.undo takes a journal write back (the recorded inverse is the
 *    paired command, `applies: command`).
 * NOT in 0.3.0: nothing inserts the recovered take into the SESSION - see
 * record.recovery_restore's own description and docs/KNOWN-LIMITATIONS.md.
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <cstdint>

#include <QFileInfo>
#include <QJsonObject>
#include <QString>

#include "ControlEdit.h"
#include "ControlRecordingSupport.h"
#include "ControlRegistry.h"
#include "RecordingJournal.h"

namespace lmms
{

using namespace control;         // the shared vocabulary lives in ControlVocabulary.h
using namespace recordingjournal;

namespace
{

// ---------------------------------------------------------------------------
// record.journal_begin
// ---------------------------------------------------------------------------
ControlResult journalBegin(const QJsonObject& args)
{
	const QString take = takeArg(args);
	if (take.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'take' is required: the absolute path of the capture's WAV file"));
	}
	if (!QFileInfo(take).isAbsolute())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'take' must be an ABSOLUTE path: a relative one names a different "
				"file in the next process's working directory, which is the run this journal "
				"exists to be found by"));
	}
	const int sampleRate = static_cast<int>(args.value(QStringLiteral("sample_rate")).toDouble());
	if (sampleRate <= 0)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'sample_rate' is required and must be positive (the journal's own "
				"lag bound is expressed against it)"));
	}

	TakeJournal previous;
	const bool hadJournal = read(journalPathFor(take), &previous);

	TakeJournal journal;
	journal.takePath = take;
	journal.state = QStringLiteral("in_progress");
	journal.sampleRate = sampleRate;
	journal.channels = args.value(QStringLiteral("channels")).toInt(1);
	journal.framesOnDisk = 0;
	journal.projectPath = args.value(QStringLiteral("project")).toString();
	journal.track = args.value(QStringLiteral("track")).toString();
	journal.startTicks = args.contains(QStringLiteral("start_ticks"))
		? static_cast<qint64>(args.value(QStringLiteral("start_ticks")).toDouble())
		: -1;
	if (!write(journalPathFor(take), journal))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("could not write the recording journal %1")
				.arg(journalPathFor(take)));
	}

	QJsonObject before;
	before.insert(QStringLiteral("take"), take);
	before.insert(QStringLiteral("journal_present"), hadJournal);
	if (hadJournal) { before.insert(QStringLiteral("state"), previous.state); }

	QJsonObject result = takeJson(journal);
	// A journal that already existed is restored by writing it back; one that did
	// not is taken back by discarding the file this call created.
	QJsonObject previousArgs;
	previousArgs.insert(QStringLiteral("take"), take);
	previousArgs.insert(QStringLiteral("sample_rate"), previous.sampleRate);
	previousArgs.insert(QStringLiteral("channels"), previous.channels);
	result.insert(QStringLiteral("__transaction"),
		commandTransaction(before,
			hadJournal ? QStringLiteral("record.journal_begin")
				: QStringLiteral("record.recovery_discard"),
			hadJournal ? previousArgs : QJsonObject{{QStringLiteral("take"), take}}));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// record.journal_update
// ---------------------------------------------------------------------------
ControlResult journalUpdate(const QJsonObject& args)
{
	TakeJournal journal;
	ControlResult error;
	if (!readJournal(args, &journal, &error)) { return error; }

	const double raw = args.value(QStringLiteral("frames_on_disk")).toDouble(-1.0);
	if (raw < 0.0)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'frames_on_disk' is required and must not be negative"));
	}
	const std::uint64_t frames = static_cast<std::uint64_t>(raw);
	// Monotonic on purpose: the field says how much of the take had reached disk
	// when the journal was last written, so a smaller number would un-record
	// material a previous update already made recoverable.
	if (frames < journal.framesOnDisk)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("'frames_on_disk' would move BACKWARDS (%1 < %2): the journal only "
				"ever records that MORE of the take reached disk")
				.arg(frames).arg(journal.framesOnDisk));
	}

	const QJsonObject before = takeJson(journal);
	journal.framesOnDisk = frames;
	if (!write(journalPathFor(journal.takePath), journal))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("could not rewrite the recording journal %1")
				.arg(journalPathFor(journal.takePath)));
	}

	QJsonObject result = takeJson(journal);
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("take"), journal.takePath);
	inverseArgs.insert(QStringLiteral("frames_on_disk"),
		static_cast<qint64>(before.value(QStringLiteral("frames_journalled")).toDouble()));
	result.insert(QStringLiteral("__transaction"),
		commandTransaction(before, QStringLiteral("record.journal_update"), inverseArgs));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// record.journal_finish
// ---------------------------------------------------------------------------
ControlResult journalFinish(const QJsonObject& args)
{
	TakeJournal journal;
	ControlResult error;
	if (!readJournal(args, &journal, &error)) { return error; }

	const QJsonObject before = takeJson(journal);
	if (!remove(journalPathFor(journal.takePath)))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("could not remove the recording journal %1")
				.arg(journalPathFor(journal.takePath)));
	}

	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("take"), journal.takePath);
	inverseArgs.insert(QStringLiteral("sample_rate"), journal.sampleRate);
	inverseArgs.insert(QStringLiteral("channels"), journal.channels);

	QJsonObject result;
	result.insert(QStringLiteral("take"), journal.takePath);
	result.insert(QStringLiteral("journal"), journalPathFor(journal.takePath));
	result.insert(QStringLiteral("state"), QStringLiteral("finished"));
	result.insert(QStringLiteral("removed"), true);
	result.insert(QStringLiteral("frames_recoverable"), static_cast<qint64>(0));
	result.insert(QStringLiteral("__transaction"),
		commandTransaction(before, QStringLiteral("record.journal_begin"), inverseArgs));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// registration
// ---------------------------------------------------------------------------
void registerJournalBegin(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.journal_begin");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("journal_begin");
	cmd.description = QStringLiteral("Start a journalled capture: write the take journal "
		"(<take>.rec-journal) that makes a recording recoverable if this process dies before "
		"the capture stops cleanly. `take` must be absolute (a relative path names a different "
		"file in the next process). Write the journal, then arm the recorder against the same "
		"path, then keep it current with record.journal_update. Reversible: the recorded "
		"inverse is the paired command (record.recovery_discard when no journal existed, "
		"record.journal_begin with the previous contents when one did), which control.undo "
		"dispatches.");
	cmd.argsSchema = journalArgsSchema(true);
	cmd.resultSchema = recoveryResultSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return journalBegin(args); };
	registry.registerCommand(cmd);
}

void registerJournalUpdate(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.journal_update");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("journal_update");
	cmd.description = QStringLiteral("Record how much of the take had reached disk at the "
		"last flush ('frames_on_disk'), which is the count the recovery offer reports as "
		"guaranteed. It must not move backwards: the journal only ever records that MORE of "
		"the take reached disk. The recorder's own disk-writer does this once per second of "
		"audio (RecordingJournal::UpdateIntervalFrames), which is the whole of the lag bound. "
		"Reversible (paired command).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("take"), stringProperty()},
		{QStringLiteral("frames_on_disk"), integerProperty()},
	}, {QStringLiteral("take"), QStringLiteral("frames_on_disk")});
	cmd.resultSchema = recoveryResultSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return journalUpdate(args); };
	registry.registerCommand(cmd);
}

void registerJournalFinish(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.journal_finish");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("journal_finish");
	cmd.description = QStringLiteral("End a journalled capture CLEANLY: remove the take "
		"journal so the next start does not offer this take as a crashed recording. This is "
		"what the recorder's own disarm() does - a clean stop leaves no journal, which is why "
		"'a journal is there' and 'the capture died' are the same fact. Reversible: the "
		"recorded inverse is record.journal_begin with the journal that was removed.");
	cmd.argsSchema = journalArgsSchema(true);
	cmd.resultSchema = objectSchema({
		{QStringLiteral("take"), stringProperty()},
		{QStringLiteral("journal"), stringProperty()},
		{QStringLiteral("state"), stringProperty()},
		{QStringLiteral("removed"), booleanProperty()},
		{QStringLiteral("frames_recoverable"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return journalFinish(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerRecordingCommands(ControlRegistry& registry)
{
	registerJournalBegin(registry);
	registerJournalUpdate(registry);
	registerJournalFinish(registry);
}

} // namespace lmms
