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

//! The undo step: undo captures the session as it is then (so a redo can put it back) and restores
//! \a checkpoint; redo restores what undo captured. With \a restoreFileName the undo also puts the
//! displaced session's file name back (project.open); a script's undo keeps the current one.
void pushRestoreStep(const SessionCheckpoint& checkpoint, bool restoreFileName, bool redoable)
{
	const std::shared_ptr<const QByteArray> before = checkpoint.before;
	const QString fileBefore = checkpoint.fileBefore;
	const auto after = std::make_shared<std::shared_ptr<const QByteArray>>();
	const auto fileAfter = std::make_shared<QString>();

	std::function<void()> undo = [before, fileBefore, restoreFileName, after, fileAfter]() {
		Song* song = Engine::getSong();
		*after = song->captureUndoSnapshot();
		*fileAfter = song->projectFileName();
		if (restoreFileName)
		{
			song->restoreUndoSnapshot(*before, fileBefore);
			// The history the opened project brought is that project's, not the displaced session's.
			if (ProjectJournal* journal = Engine::projectJournal()) { journal->discardLoadedSteps(); }
		}
		else { song->restoreUndoSnapshot(*before); }
	};
	std::function<void()> redo;
	if (redoable)
	{
		redo = [restoreFileName, after, fileAfter]() {
			if (!*after) { return; }
			Song* song = Engine::getSong();
			if (restoreFileName) { song->restoreUndoSnapshot(**after, *fileAfter); }
			else { song->restoreUndoSnapshot(**after); }
		};
	}
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
	if (song->isPartialLoad())
	{
		return skipped(QStringLiteral("the session is a partial load (sections were skipped), which "
			"cannot be captured whole - a restore would bring back a smaller project"));
	}

	SessionCheckpoint checkpoint;
	checkpoint.before = song->captureUndoSnapshot();
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
		// A partial load of the NEW project cannot be captured, so its redo is not offered.
		pushRestoreStep(taken, true, !song->isPartialLoad());
		taken.pushed = true;
		if (checkpoint != nullptr) { *checkpoint = taken; }
	}
	return true;
}

void finishScriptCheckpoint(SessionCheckpoint* checkpoint, quint64 serialBefore, bool* changed)
{
	*changed = false;
	ProjectJournal* journal = Engine::projectJournal();
	if (checkpoint == nullptr || !checkpoint->taken() || journal == nullptr) { return; }

	const std::shared_ptr<const QByteArray> now = Engine::getSong()->captureUndoSnapshot();
	// A session that cannot be captured or read after the run is assumed changed: the checkpoint is still true.
	const QString after = now ? contentDigest(*now) : QString();
	*changed = after.isEmpty() || after != contentDigest(*checkpoint->before);
	if (!*changed) { return; }

	// The finer steps the script pushed are subsumed: this one restores the whole project.
	journal->discardTopSteps(journal->retainedStepsFrom(serialBefore + 1));
	pushRestoreStep(*checkpoint, false, true);
	checkpoint->pushed = true;
}

} // namespace control
} // namespace lmms
