/*
 * SessionCheckpoint.cpp - Q050-044: one undo step that puts the whole session back
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

#include "SessionCheckpoint.h"

#include <functional>

#include <QDomDocument>
#include <QDomElement>

#include "ControlReversibility.h"
#include "DocumentIndex.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"

namespace lmms
{
namespace control
{

namespace
{

QString megabytes(qint64 bytes)
{
	return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 2);
}

//! Puts a checkpoint's session back as it was captured: its snapshot, and the container format it had.
//! A restore keeps the format of the session it replaces (Song::restoreUndoSnapshot), so the captured
//! format is set here - and only when the restore took, since a refused one leaves the session as it was.
bool restoreCheckpointSession(Song* song, const QByteArray& snapshot, const QString& file,
	const QStringList& skipped, bool containerV2)
{
	const bool restored = song->restoreUndoSnapshot(snapshot, file, skipped);
	if (restored) { song->setContainerV2Project(containerV2); }
	return restored;
}

//! The undo step restores the pre-action session; redo restores the captured post-action session.
void pushRestoreStep(SessionCheckpoint& checkpoint, bool discardLoadedSteps,
	const std::shared_ptr<const QByteArray>& after, const QString& fileAfter,
	const QStringList& skippedAfter, bool containerAfter)
{
	const std::shared_ptr<const QByteArray> before = checkpoint.before;
	const QString fileBefore = checkpoint.fileBefore;
	const QStringList skippedBefore = checkpoint.skippedBefore;
	const bool containerBefore = checkpoint.containerV2Before;

	std::function<void()> undo = [before, fileBefore, skippedBefore, containerBefore, discardLoadedSteps]() {
		Song* song = Engine::getSong();
		restoreCheckpointSession(song, *before, fileBefore, skippedBefore, containerBefore);
		// An opened project's history is that project's, not the displaced session's.
		if (discardLoadedSteps)
		{
			if (ProjectJournal* journal = Engine::projectJournal()) { journal->discardLoadedSteps(); }
		}
	};
	std::function<void()> redo = [after, fileAfter, skippedAfter, containerAfter]() {
		restoreCheckpointSession(Engine::getSong(), *after, fileAfter, skippedAfter, containerAfter);
	};
	addStructuralUndoStep(std::move(undo), std::move(redo), checkpoint.retainedBytes);
}

//! What a snapshot says about the project, independent of attribute order (QDom keeps attributes in a
//! hash, so two captures of the same project need not be byte-equal): the digest of every child of the
//! document root, without the root's own id counter, which a restore never rewinds. Empty when the
//! document cannot be read.
QString contentDigest(const QByteArray& compressed)
{
	QDomDocument document;
	if (!document.setContent(qUncompress(compressed))) { return QString(); }
	QString digest;
	for (QDomElement child = document.documentElement().firstChildElement(); !child.isNull();
		child = child.nextSiblingElement())
	{
		digest += child.tagName() + QLatin1Char(' ') + sectionDigest(child) + QLatin1Char('\n');
	}
	return digest;
}

SessionCheckpoint skipped(const QString& why)
{
	SessionCheckpoint checkpoint;
	checkpoint.note = why;
	return checkpoint;
}

bool retainedPairFits(SessionCheckpoint* checkpoint, const QString& action,
	const std::shared_ptr<const QByteArray>& after, QString* refusal)
{
	checkpoint->retainedBytes = checkpoint->bytes + after->size();
	if (checkpoint->retainedBytes <= checkpoint->limit) { return true; }
	checkpoint->state = SessionCheckpoint::State::TooLarge;
	checkpoint->note = QStringLiteral("%1 refused: the pre-action snapshot is %2 MiB and its redo state is %3 MiB; "
		"keeping both would use %4 MiB, over the %5 MiB undo budget. Nothing was changed. Save the project, "
		"or raise the budget with control.set_undo_depth {bytes} and run it again")
		.arg(action, megabytes(checkpoint->bytes), megabytes(after->size()),
			megabytes(checkpoint->retainedBytes), megabytes(checkpoint->limit));
	if (refusal != nullptr) { *refusal = checkpoint->note; }
	return false;
}

bool pushLoadedSessionForRedo(SessionCheckpoint* checkpoint, Song* song, QString* refusal)
{
	QStringList skippedAfter;
	const auto after = song->captureUndoSnapshot(&skippedAfter);
	if (!after)
	{
		restoreCheckpointSession(song, *checkpoint->before, checkpoint->fileBefore, checkpoint->skippedBefore,
			checkpoint->containerV2Before);
		checkpoint->note = QStringLiteral("project.open refused: the loaded project could not be captured for redo, "
			"so the previous session was restored");
		if (refusal != nullptr) { *refusal = checkpoint->note; }
		return false;
	}
	checkpoint->containerV2After = song->isContainerV2Project();
	if (!retainedPairFits(checkpoint, QStringLiteral("project.open"), after, refusal))
	{
		restoreCheckpointSession(song, *checkpoint->before, checkpoint->fileBefore, checkpoint->skippedBefore,
			checkpoint->containerV2Before);
		return false;
	}
	pushRestoreStep(*checkpoint, true, after, song->projectFileName(), skippedAfter, checkpoint->containerV2After);
	checkpoint->pushed = true;
	return true;
}

bool sessionDiffers(const QString& after, const SessionCheckpoint& checkpoint,
	const QString& fileAfter, const QStringList& skippedAfter)
{
	return after.isEmpty() || after != contentDigest(*checkpoint.before)
		|| fileAfter != checkpoint.fileBefore || skippedAfter != checkpoint.skippedBefore;
}

} // namespace

QJsonObject SessionCheckpoint::toJson() const
{
	QJsonObject out;
	out.insert(QStringLiteral("taken"), taken());
	out.insert(QStringLiteral("undoable"), pushed);
	out.insert(QStringLiteral("bytes"), static_cast<double>(bytes));
	out.insert(QStringLiteral("retained_bytes"), static_cast<double>(retainedBytes));
	out.insert(QStringLiteral("limit_bytes"), static_cast<double>(limit));
	out.insert(QStringLiteral("note"), note);
	return out;
}

SessionCheckpoint takeSessionCheckpoint(const QString& action, bool onlyWhenModified)
{
	Song* song = Engine::getSong();
	ProjectJournal* journal = Engine::projectJournal();
	if (song == nullptr || journal == nullptr || !journal->isJournalling())
	{
		return skipped(QStringLiteral("the engine's undo journal is not recording, so no step can be kept"));
	}
	if (onlyWhenModified && !song->isModified())
	{
		return skipped(QStringLiteral("the session has no unsaved changes, so the open loses "
			"nothing that was not already saved"));
	}
	SessionCheckpoint checkpoint;
	checkpoint.before = song->captureUndoSnapshot(&checkpoint.skippedBefore);
	if (!checkpoint.before)
	{
		return skipped(QStringLiteral("the session cannot be captured right now (a load is running)"));
	}
	checkpoint.fileBefore = song->projectFileName();
	checkpoint.containerV2Before = song->isContainerV2Project();
	checkpoint.bytes = checkpoint.before->size();
	checkpoint.limit = journal->maxUndoBytes();
	if (checkpoint.bytes > checkpoint.limit)
	{
		checkpoint.state = SessionCheckpoint::State::TooLarge;
		checkpoint.note = QStringLiteral("%1 refused: the session's undo checkpoint is %2 MiB "
			"(compressed) and the undo budget is %3 MiB, so the step that would undo this could not be "
			"kept. Nothing was changed. Save the project (project.save), or raise the budget with "
			"control.set_undo_depth {bytes} and run it again")
			.arg(action, megabytes(checkpoint.bytes), megabytes(checkpoint.limit));
		return checkpoint;
	}
	checkpoint.state = SessionCheckpoint::State::Taken;
	return checkpoint;
}

bool loadProjectWithCheckpoint(const QString& path, const QStringList& skipSections,
	SessionCheckpoint* checkpoint, QString* refusal)
{
	SessionCheckpoint taken = takeSessionCheckpoint(QStringLiteral("project.open"), true);
	if (checkpoint != nullptr) { *checkpoint = taken; }
	if (taken.refused())
	{
		if (refusal != nullptr) { *refusal = taken.note; }
		return false;
	}

	Song* song = Engine::getSong();
	song->loadProject(path, skipSections);
	const bool checkpointReady = !taken.taken() || !song->loadRefusal().isEmpty()
		|| pushLoadedSessionForRedo(&taken, song, refusal);
	if (checkpoint != nullptr) { *checkpoint = taken; }
	return checkpointReady;
}

void finishScriptCheckpoint(SessionCheckpoint* checkpoint, quint64 serialBefore, bool* changed,
	QString* refusal)
{
	*changed = false;
	ProjectJournal* journal = Engine::projectJournal();
	if (checkpoint == nullptr || !checkpoint->taken() || journal == nullptr) { return; }

	Song* song = Engine::getSong();
	QStringList skippedAfter;
	const auto now = song->captureUndoSnapshot(&skippedAfter);
	if (!now)
	{
		journal->discardTopSteps(journal->retainedStepsFrom(serialBefore + 1));
		restoreCheckpointSession(song, *checkpoint->before, checkpoint->fileBefore, checkpoint->skippedBefore,
			checkpoint->containerV2Before);
		const QString why = QStringLiteral("script.run refused: the script's project result could not be captured "
			"for redo, so the previous session was restored");
		if (refusal != nullptr) { *refusal = why; }
		checkpoint->note = why;
		return;
	}
	const QString after = contentDigest(*now);
	*changed = sessionDiffers(after, *checkpoint, song->projectFileName(), skippedAfter);
	if (!*changed) { return; }
	checkpoint->containerV2After = song->isContainerV2Project();
	if (!retainedPairFits(checkpoint, QStringLiteral("script.run"), now, refusal))
	{
		journal->discardTopSteps(journal->retainedStepsFrom(serialBefore + 1));
		restoreCheckpointSession(song, *checkpoint->before, checkpoint->fileBefore, checkpoint->skippedBefore,
			checkpoint->containerV2Before);
		*changed = false;
		return;
	}

	// The finer steps the script pushed are subsumed: this one restores the whole project.
	journal->discardTopSteps(journal->retainedStepsFrom(serialBefore + 1));
	pushRestoreStep(*checkpoint, false, now, song->projectFileName(), skippedAfter, checkpoint->containerV2After);
	checkpoint->pushed = true;
}

} // namespace control
} // namespace lmms
