/*
 * ControlCommandsRecordingRecovery.cpp - the record.* group's RECOVERY half:
 *                                        what the next start does with a take
 *                                        journal an abnormal exit left behind
 *                                        (0.3.0).
 *
 * One group, two translation units (the warp / automation groups' split) - see
 * ControlCommandsRecording.cpp for the group's half of the story and
 * include/RecordingJournal.h for the engine and the BOUND.
 *
 * THE ONE THING THIS FILE WILL NOT DO: pretend the recovered take is in the
 * session. `record.recovery_restore` resolves the offer and hands the material
 * back - it reports the take, the guaranteed frame count and the file's real
 * frame count, and it leaves the audio exactly where the crash left it. No
 * command in 0.3.0 imports an audio file onto a track as a clip, so the half
 * that would place it is deferred and named in the result's `next_step` and in
 * docs/KNOWN-LIMITATIONS.md. A partially-built feature that is drivable and
 * proved beats a large claim that is not.
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
#include <QJsonArray>
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
// record.recovery_get_state
// ---------------------------------------------------------------------------
ControlResult recoveryGetState(const QJsonObject& args)
{
	QString dir = args.value(QStringLiteral("dir")).toString();
	if (dir.isEmpty()) { dir = defaultRecoveryDir(); }
	if (!QFileInfo(dir).isDir())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'dir' is not a directory: %1").arg(dir));
	}

	const QList<TakeJournal> found = scan(dir);
	QJsonArray takes;
	for (const TakeJournal& journal : found) { takes.append(takeJson(journal)); }

	QJsonObject result;
	result.insert(QStringLiteral("dir"), dir);
	result.insert(QStringLiteral("count"), takes.size());
	result.insert(QStringLiteral("takes"), takes);
	result.insert(QStringLiteral("bound"),
		boundText(found.isEmpty() ? 0 : found.first().sampleRate));
	result.insert(QStringLiteral("scanned_for"), journalSuffix());
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// record.recovery_restore
// ---------------------------------------------------------------------------
ControlResult recoveryRestore(const QJsonObject& args)
{
	TakeJournal journal;
	ControlResult error;
	if (!readJournal(args, &journal, &error)) { return error; }
	if (!isRecoverable(journal))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the journal for '%1' is '%2', not an interrupted capture")
				.arg(journal.takePath, journal.state));
	}

	const QJsonObject before = takeJson(journal);
	journal.state = QStringLiteral("restored");
	if (!write(journalPathFor(journal.takePath), journal))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("could not write the recording journal %1")
				.arg(journalPathFor(journal.takePath)));
	}

	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("take"), journal.takePath);
	inverseArgs.insert(QStringLiteral("sample_rate"), journal.sampleRate);
	inverseArgs.insert(QStringLiteral("channels"), journal.channels);

	QJsonObject result = takeJson(journal);
	result.insert(QStringLiteral("restored"), true);
	// The audio is NOT moved, rewritten or deleted: this command resolves the
	// offer, it does not touch the material - and a caller reading
	// `restored: true` has to be told exactly that.
	result.insert(QStringLiteral("audio_untouched"), true);
	result.insert(QStringLiteral("audio"), journal.takePath);
	result.insert(QStringLiteral("next_step"),
		QStringLiteral("the take is left where the crash left it; 0.3.0 has no command that "
			"imports an audio file into the session as a clip, so bringing the material in "
			"is the deferred half (docs/KNOWN-LIMITATIONS.md)"));
	result.insert(QStringLiteral("__transaction"),
		commandTransaction(before, QStringLiteral("record.journal_begin"), inverseArgs));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// record.recovery_discard
// ---------------------------------------------------------------------------
ControlResult recoveryDiscard(const QJsonObject& args)
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

	QJsonObject result;
	result.insert(QStringLiteral("take"), journal.takePath);
	result.insert(QStringLiteral("journal"), journalPathFor(journal.takePath));
	result.insert(QStringLiteral("removed"), true);
	result.insert(QStringLiteral("audio_kept"), true);
	// No inverse exists: the journal is gone and the offer with it. What a caller
	// still has is the take's own file, and THAT is the fallback the contract row
	// and control.undo's typed refusal both name.
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(before, QString(), QJsonObject(), false,
			QStringLiteral("none: the journal file is removed, so the recovery offer is gone. "
				"The take's own WAV is never deleted by this command - it stays on disk at "
				"before.take and can be brought in by hand")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// registration
// ---------------------------------------------------------------------------
void registerRecoveryGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.recovery_get_state");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("recovery_get_state");
	cmd.description = QStringLiteral("Every capture an abnormal exit left behind in `dir` "
		"(default: this instance's working directory): one entry per in-progress take journal "
		"whose WAV still exists. Each entry reports frames_journalled (what the journal "
		"recorded at its last update), frames_in_file (measured from the take's RIFF header, "
		"falling back to the file's real length when a crashed header was never updated) and "
		"frames_recoverable (the SMALLER of the two - the guaranteed count). The bound travels "
		"with every entry: the journal lags by up to one second of audio, and up to 65536 "
		"frames still in the recorder's ring at the crash are gone for good. Writes nothing.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("dir"), stringProperty()},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("dir"), stringProperty()},
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("takes"), arrayProperty()},
		{QStringLiteral("bound"), stringProperty()},
		{QStringLiteral("scanned_for"), stringProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) { return recoveryGetState(args); };
	registry.registerCommand(cmd);
}

void registerRecoveryRestore(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.recovery_restore");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("recovery_restore");
	cmd.description = QStringLiteral("Take an interrupted capture's offer: report the "
		"material the journal recovered and mark the journal `restored`, so the next start "
		"stops offering it. The take's WAV is NOT moved, rewritten or deleted - the material "
		"stays exactly where the crash left it, and the result says so (`audio_untouched`). "
		"WHAT 0.3.0 DOES NOT DO: bring the take into the session as a clip. No command in "
		"this release imports an audio file onto a track, so that half is deferred and named "
		"in `next_step` (docs/KNOWN-LIMITATIONS.md). Reversible: the recorded inverse is "
		"record.journal_begin with the journal that was restored.");
	cmd.argsSchema = journalArgsSchema(true);
	cmd.resultSchema = objectSchema({
		{QStringLiteral("take"), stringProperty()},
		{QStringLiteral("journal"), stringProperty()},
		{QStringLiteral("state"), stringProperty()},
		{QStringLiteral("frames_journalled"), integerProperty()},
		{QStringLiteral("frames_in_file"), integerProperty()},
		{QStringLiteral("frames_recoverable"), integerProperty()},
		{QStringLiteral("restored"), booleanProperty()},
		{QStringLiteral("audio_untouched"), booleanProperty()},
		{QStringLiteral("audio"), stringProperty()},
		{QStringLiteral("next_step"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return recoveryRestore(args); };
	registry.registerCommand(cmd);
}

void registerRecoveryDiscard(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.recovery_discard");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("recovery_discard");
	cmd.description = QStringLiteral("Refuse an interrupted capture's offer: remove its take "
		"journal so the next start stops offering it. THE TAKE'S WAV IS NEVER DELETED - it "
		"stays on disk at the take path the transaction's before-state names, which is the "
		"documented fallback because this command has no inverse (control.undo fails, typed, "
		"naming that path).");
	cmd.argsSchema = journalArgsSchema(true);
	cmd.resultSchema = objectSchema({
		{QStringLiteral("take"), stringProperty()},
		{QStringLiteral("journal"), stringProperty()},
		{QStringLiteral("removed"), booleanProperty()},
		{QStringLiteral("audio_kept"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return recoveryDiscard(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerRecordingRecoveryCommands(ControlRegistry& registry)
{
	registerRecoveryGetState(registry);
	registerRecoveryRestore(registry);
	registerRecoveryDiscard(registry);
}

} // namespace lmms
