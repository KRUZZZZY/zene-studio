/*
 * ControlRecordingSupport.cpp - the record.* group's shared helpers (see the
 *                               header for why they are in their own unit).
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

#include "ControlRecordingSupport.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

#include "ConfigManager.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"

namespace lmms
{

using namespace recordingjournal;

namespace control
{

namespace
{

//! Why the journal's inverse is a command and not a checkpoint: the journal is a
//! file in a take directory, which the project journal holds nothing for.
const QString ClauseJournalFile = QStringLiteral("the journal is a side file beside the "
	"take, not project state, so the project journal holds nothing for it: the recorded "
	"inverse is the paired COMMAND (`applies: command`), which control.undo dispatches "
	"through the registry");

} // namespace

QJsonObject journalStateJson(const TakeJournal& journal, std::uint64_t framesInFile)
{
	QJsonObject result;
	result.insert(QStringLiteral("take"), journal.takePath);
	result.insert(QStringLiteral("journal"), journalPathFor(journal.takePath));
	result.insert(QStringLiteral("state"), journal.state);
	result.insert(QStringLiteral("sample_rate"), journal.sampleRate);
	result.insert(QStringLiteral("channels"), journal.channels);
	result.insert(QStringLiteral("frames_journalled"), static_cast<qint64>(journal.framesOnDisk));
	result.insert(QStringLiteral("frames_in_file"), static_cast<qint64>(framesInFile));
	// THE BOUND, as a number the caller can check: the guaranteed count is the
	// SMALLER of what the journal recorded and what the take really holds.
	result.insert(QStringLiteral("frames_recoverable"),
		static_cast<qint64>(std::min(journal.framesOnDisk, framesInFile)));
	result.insert(QStringLiteral("frames_beyond_the_journal"),
		surplus(framesInFile, journal.framesOnDisk));
	result.insert(QStringLiteral("journal_lag_bound_frames"),
		static_cast<qint64>(updateIntervalFrames(journal.sampleRate)));
	result.insert(QStringLiteral("ring_frames_not_recoverable"),
		static_cast<qint64>(RingFramesNotRecoverable));
	result.insert(QStringLiteral("updated_utc"), journal.updatedUtc);
	if (!journal.projectPath.isEmpty())
	{
		result.insert(QStringLiteral("project"), journal.projectPath);
	}
	if (!journal.track.isEmpty()) { result.insert(QStringLiteral("track"), journal.track); }
	if (journal.startTicks >= 0)
	{
		result.insert(QStringLiteral("start_ticks"), journal.startTicks);
	}
	return result;
}

QJsonObject takeJson(const TakeJournal& journal)
{
	return journalStateJson(journal, framesInFile(journal.takePath));
}

QString takeArg(const QJsonObject& args)
{
	return args.value(QStringLiteral("take")).toString();
}

bool readJournal(const QJsonObject& args, TakeJournal* journal, ControlResult* error)
{
	const QString take = takeArg(args);
	if (take.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'take' is required: the absolute path of the capture's WAV file"));
		return false;
	}
	if (!read(journalPathFor(take), journal))
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no recording journal for '%1' (expected %2); a capture that was "
				"never journalled cannot be recovered from the journal")
				.arg(take, journalPathFor(take)));
		return false;
	}
	return true;
}

QJsonObject commandTransaction(const QJsonObject& before, const QString& inverseOp,
	const QJsonObject& inverseArgs)
{
	QJsonObject transaction = transactionPayload(before, inverseOp, inverseArgs, true,
		ClauseJournalFile);
	QJsonObject inverse = transaction.value(QStringLiteral("inverse")).toObject();
	inverse.insert(QStringLiteral("applies"), QStringLiteral("command"));
	transaction.insert(QStringLiteral("inverse"), inverse);
	return transaction;
}

qint64 surplus(std::uint64_t left, std::uint64_t right)
{
	return left > right ? static_cast<qint64>(left - right) : 0;
}

QString defaultRecoveryDir()
{
	return QFileInfo(ConfigManager::inst()->recoveryFile()).absoluteDir().absolutePath();
}

QJsonObject journalArgsSchema(bool requireTake)
{
	return objectSchema({
		{QStringLiteral("take"), stringProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("channels"), integerProperty()},
		{QStringLiteral("project"), stringProperty()},
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("start_ticks"), integerProperty()},
	}, requireTake ? QJsonArray{QStringLiteral("take")} : QJsonArray{});
}

QJsonObject recoveryResultSchema()
{
	return objectSchema({
		{QStringLiteral("take"), stringProperty()},
		{QStringLiteral("journal"), stringProperty()},
		{QStringLiteral("state"), stringProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("channels"), integerProperty()},
		{QStringLiteral("frames_journalled"), integerProperty()},
		{QStringLiteral("frames_in_file"), integerProperty()},
		{QStringLiteral("frames_recoverable"), integerProperty()},
		{QStringLiteral("frames_beyond_the_journal"), integerProperty()},
		{QStringLiteral("journal_lag_bound_frames"), integerProperty()},
		{QStringLiteral("ring_frames_not_recoverable"), integerProperty()},
		{QStringLiteral("updated_utc"), stringProperty()},
		{QStringLiteral("project"), stringProperty()},
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("start_ticks"), integerProperty()},
	});
}

} // namespace control

} // namespace lmms
