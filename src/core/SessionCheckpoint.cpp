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

//! The undo step restores the pre-action session; redo restores the captured post-action session.
void pushRestoreStep(const SessionCheckpoint& checkpoint, bool discardLoadedSteps,
	const std::shared_ptr<const QByteArray>& after, const QString& fileAfter,
	const QStringList& skippedAfter)
{
	const std::shared_ptr<const QByteArray> before = checkpoint.before;
	const QString fileBefore = checkpoint.fileBefore;
	const QStringList skippedBefore = checkpoint.skippedBefore;

	std::function<void()> undo = [before, fileBefore, skippedBefore, discardLoadedSteps]() {
		Song* song = Engine::getSong();
		song->restoreUndoSnapshot(*before, fileBefore, skippedBefore);
		// An opened project's history is that project's, not the displaced session's.
		if (discardLoadedSteps)
		{
			if (ProjectJournal* journal = Engine::projectJournal()) { journal->discardLoadedSteps(); }
		}
	};
	std::function<void()> redo = [after, fileAfter, skippedAfter]() {
		Song* song = Engine::getSong();
		song->restoreUndoSnapshot(*after, fileAfter, skippedAfter);
	};
	addStructuralUndoStep(std::move(undo), std::move(redo), checkpoint.bytes);
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

} // namespace

QJsonObject SessionCheckpoint::toJson() const
{
	QJsonObject out;
	out.insert(QStringLiteral("taken"), taken());
	out.insert(QStringLiteral("undoable"), pushed);
	out.insert(QStringLiteral("bytes"), static_cast<double>(bytes));
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

	// A refused file leaves the session as it was: nothing to undo.
	if (taken.taken() && song->loadRefusal().isEmpty())
	{
		QStringList skippedAfter;
		const auto after = song->captureUndoSnapshot(&skippedAfter);
		if (!after)
		{
			song->restoreUndoSnapshot(*taken.before, taken.fileBefore, taken.skippedBefore);
			const QString why = QStringLiteral("project.open refused: the loaded project could not be captured for redo, "
				"so the previous session was restored");
			if (refusal != nullptr) { *refusal = why; }
			taken.note = why;
			if (checkpoint != nullptr) { *checkpoint = taken; }
			return false;
		}
		pushRestoreStep(taken, true, after, song->projectFileName(), skippedAfter);
		taken.pushed = true;
		if (checkpoint != nullptr) { *checkpoint = taken; }
	}
	return true;
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
		song->restoreUndoSnapshot(*checkpoint->before, checkpoint->fileBefore, checkpoint->skippedBefore);
		const QString why = QStringLiteral("script.run refused: the script's project result could not be captured "
			"for redo, so the previous session was restored");
		if (refusal != nullptr) { *refusal = why; }
		checkpoint->note = why;
		return;
	}
	const QString after = contentDigest(*now);
	*changed = after.isEmpty() || after != contentDigest(*checkpoint->before)
		|| song->projectFileName() != checkpoint->fileBefore || skippedAfter != checkpoint->skippedBefore;
	if (!*changed) { return; }

	// The finer steps the script pushed are subsumed: this one restores the whole project.
	journal->discardTopSteps(journal->retainedStepsFrom(serialBefore + 1));
	pushRestoreStep(*checkpoint, false, now, song->projectFileName(), skippedAfter);
	checkpoint->pushed = true;
}

} // namespace control
} // namespace lmms
