/*
 * RevisionTimelineTest.cpp - the registered proof of the `revisions.*` group
 *                          (feature-list row 76, OWNER-31 item 30).
 *
 * The claim under test is not "the ids exist": it is that the timeline LISTS the
 * revision artefacts this engine already writes with their SOURCE and TIME, that
 * `compare` reports a real structural difference, and that `restore` puts a
 * revision's bytes back while a RESTORE OF THE FILE IT REPLACED brings the
 * previous content back - the recorded inverse, exercised through
 * control.undo's own mechanism (project.restore_revision) rather than asserted.
 *
 * Everything here is a real file in a temp directory: a project document, a
 * `.bak`, a `.rev0`, an autosave with its `.info` sidecar, and - when `git` is
 * on the machine - a real repository with two commits of the project. No Engine
 * state is faked and no artefact is written by the test that the engine would
 * not write itself: the fixture is built with the same calls the product uses
 * (control::rotateProjectRevision and plain QFile writes).
 *
 * Headless (QTEST_GUILESS_MAIN) and offscreen-safe; the group's commands are
 * also exercised through the registry, which is what makes `invoke()` - the
 * path a socket client takes - part of the proof.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include "ControlProjectAssets.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectContainer.h"
#include "ProjectRevisions.h"
#include "RevisionTimeline.h"
#include "ReversibilityTestSupport.h"

using namespace lmms;
using namespace lmms::control;

namespace
{

//! A minimal, REAL project document: the element counts are the thing `compare`
//! reports, so the two documents differ by exactly one <note> and one <clip>.
QByteArray documentWith(const int notes)
{
	QByteArray xml = "<?xml version=\"1.0\"?>\n<song version=\"1.2.0\">\n"
		"\t<track type=\"instrument\" name=\"lead\">\n\t\t<clip>\n";
	for (int i = 0; i < notes; ++i)
	{
		xml += "\t\t\t<note key=\"" + QByteArray::number(60 + i) + "\" pos=\"0\"/>\n";
	}
	xml += "\t\t</clip>\n\t</track>\n</song>\n";
	return xml;
}

QByteArray documentWithTagCounts(const int tags, const int countPerTag)
{
	QByteArray xml = "<?xml version=\"1.0\"?>\n<song>\n";
	for (int i = 0; i < tags; ++i)
	{
		const QByteArray tag = "tag" + QByteArray::number(i).rightJustified(3, '0');
		for (int count = 0; count < countPerTag; ++count)
		{
			xml += '<';
			xml += tag;
			xml += "/>\n";
		}
	}
	xml += "</song>\n";
	return xml;
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	return file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QByteArray(); }
	return file.readAll();
}

//! The entries of a `revisions.list` result, by id.
QJsonObject entryById(const QJsonObject& result, const QString& id)
{
	for (const QJsonValue& value : result.value(QStringLiteral("revisions")).toArray())
	{
		const QJsonObject entry = value.toObject();
		if (entry.value(QStringLiteral("id")).toString() == id) { return entry; }
	}
	return QJsonObject();
}

//! A command dispatched the way a socket client dispatches it.
ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

bool gitAvailable()
{
	return !QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty();
}

void runGitIn(const QString& dir, const QStringList& args)
{
	QProcess process;
	process.setWorkingDirectory(dir);
	process.start(QStringLiteral("git"), args);
	process.waitForFinished(20000);
}

} // namespace

class RevisionTimelineTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;
	QString m_project;

	QString path(const QString& name) const
	{
		return m_dir.path() + QLatin1Char('/') + name;
	}

	//! A project file, a `.bak`, a `.rev0` with DIFFERENT documents, and an
	//! autosave with its identity sidecar - the four artefacts row 76 names,
	//! minus git, which its own case builds.
	void buildFixture()
	{
		m_project = path(QStringLiteral("song.mmp"));
		QVERIFY(writeBytes(m_project, documentWith(3)));
		QVERIFY(writeBytes(path(QStringLiteral("song.mmp.bak")), documentWith(1)));
		QVERIFY(writeBytes(path(QStringLiteral("song.mmp.rev0")), documentWith(2)));
		QVERIFY(writeBytes(path(QStringLiteral("recover.mmp")), documentWith(4)));
		QVERIFY(writeBytes(path(QStringLiteral("recover.mmp.info")),
			QByteArray("# zene-studio autosave identity - a side file, never part of a project\n"
				"version=1\nproject=") + m_project.toUtf8()
				+ "\nsavedUTC=2026-09-15T10:00:00Z\n"));
	}

	QString recoveryFile() const { return path(QStringLiteral("recover.mmp")); }

private slots:

	void initTestCase();

	void cleanupTestCase();

	//! The list reports every artefact with its SOURCE and its TIME - the row's
	//! own words - and the git half answers even where there is no repository.
	void listReportsTheArtefactsWithSourceAndTime();

	//! compare reports the STRUCTURAL difference: the counts and the tags that
	//! differ, and byte identity for a document against itself.
	void compareReportsTheDifferenceAndTheIdentity();

	//! The result stays capped while the total counts every difference, and the
	//! helper constructs JSON entries only after it knows they fit the cap.
	void comparisonBoundsDifferenceConstruction();

	//! The private union helper uses set membership so a document with many
	//! distinct tags does not scan the growing result list for every tag.
	void comparisonTagUnionUsesSetMembership();

	//! A `.mmpz` v2 CONTAINER is refused rather than counted. This slot is the
	//! other half of the one above, and the two are the reason the guard has to
	//! come BEFORE the `<?xml` probe rather than after it: a v2 container is a
	//! STORE ZIP - DawProjectZip.cpp's header records that it does not compress - so its
	//! bytes contain a LITERAL `<?xml` and the probe matches them. Until the guard
	//! landed, the archive was handed to the tag counter as if it were a document
	//! and the comparison reported `readable: true` over the ZIP's own headers:
	//! not a refusal with a bad message, but a confident meaningless answer.
	void aV2ContainerIsRefusedRatherThanCountedAsADocument();

	//! restore puts the revision's bytes back, and the file it replaced is
	//! revision 0 - so `project.restore_revision` (the recorded inverse
	//! control.undo dispatches) brings the previous content back byte for byte.
	void restoreIsReversibleThroughTheRotatedRevision();

	//! Luna cluster 5236: a shift copy that fails does NOT take the destination
	//! backup with it, and the rotation says it was refused. copyRevision()
	//! removed the destination BEFORE copying, so a failed shift destroyed an
	//! older backup while rotation continued as if it had succeeded.
	void aFailedShiftKeepsTheBackupAndReportsARefusal();

	//! Luna cluster 4352: a replace that cannot complete leaves the LIVE project
	//! exactly as it was. replaceProjectWithStaged() removed the live file before
	//! renaming the staged one into place, and its recovery copies revision 0
	//! back - so a failure at the moment the disk is full, the one moment the
	//! recovery copy also cannot succeed, left no project file at all.
	void aFailedReplaceKeepsTheLiveProject();

	//! The replacement fails AFTER its staged copy is complete: the live project keeps its bytes and the
	//! staging file survives. Before the rework the live file was removed first, and revision 0 is made
	//! unreadable here, so the old recovery copy could not put it back and the failure emptied the path.
	void aFailedReplacementAfterStagingKeepsTheLiveProject();

	//! A file that is not ours, named the way the old pending copy was named, is not deleted by a restore.
	void aReplaceLeavesAnUnrelatedReplaceFileAlone();

	//! A replacement that fails AFTER its staged copy is complete leaves the slot it would have replaced
	//! exactly as it was, and the rotation reports the refusal. Before the rework the slot was removed
	//! first, so this failure emptied it.
	void aFailedReplacementAfterStagingKeepsTheSlot();

	//! A file that is not ours, named the way the old staging file was named, is not deleted by a rotation.
	void aRotationLeavesAnUnrelatedRotateFileAlone();

	//! An id in the keep-3 set restores through the policy's OWN function, which
	//! is the whole point of listing those artefacts rather than re-reading them:
	//! one implementation of the rotation order, and the replaced file is
	//! revision 0 afterwards.
	void restoreOfARotationIdGoesThroughTheKeepThreePolicy();

	//! A stale slot that cannot be removed is reported, rather than a successful
	//! rotation silently leaving a revision outside the keep-3 set.
	void rotationReportsFailureToRemoveStaleRevision();

	//! A refusal writes NOTHING: an unknown id, an id whose artefact is gone,
	//! and a restore into a session with no project file at all.
	void refusalsAreTypedAndWriteNothing();

	//! A zero-byte artefact reads back SUCCESSFULLY with no bytes, and it is not
	//! a project: restoring it is refused typed, and the live project, the
	//! revision set and the directory are left exactly as they were. Covers the
	//! timeline's own sources (backup, autosave) and the keep-3 rotation id,
	//! which takes the policy's own restore.
	void anEmptyRevisionIsRefusedAndTheLiveProjectKept();

	//! The git half, against a REAL repository with two commits of the project:
	//! the commits are listed with their own times, and the BYTES of one commit
	//! are what the timeline restores.
	void gitHistoryIsListedAndRestorable();

	//! Luna cluster 5737: a restore that CANNOT find its revision records no undo
	//! step. The handler registered the checkpoint before handing the request to
	//! restoreProjectRevision(), the call that can refuse it, so asking for a
	//! revision that is not there failed the command and still pushed a step whose
	//! undo "restores" a file the command never touched. The caller's next
	//! control.undo then spent itself on that phantom, and the step before it moved
	//! one further from the keyboard.
	void aFailedRestoreRecordsNoUndoStep();

	//! Luna cluster 5738: a redo restores the BYTES the restore selected, not the
	//! revision NUMBER it named. Every restore rotates the live file in, so the
	//! numbers shift under the recorded redo: after the undo, revision 1 held a
	//! different file and the redo put THAT over the live one, which left the
	//! project holding the pre-restore bytes under a redo that reported success.
	void aRedoRestoresTheBytesTheRestoreSelected();

	void aRefusedRestoreRecordsNoUndoStep();

	//! Luna cluster 4878: the redo of a revisions.restore re-resolved the captured
	//! revision ID. 'rev1' is a POSITION in the keep-3 set, and the restore's own
	//! rotation plus the undo's rotation each shift every position, so by redo time
	//! the id named different bytes and the redo wrote those over the live file
	//! under a success it had not earned.
	void theTimelineRedoRestoresTheBytesTheRestoreSelected();

	//! Luna cluster 1223: the transaction revisions.restore records advertises an
	//! inverse that can neither run nor mean anything. It named
	//! `revisions.restore` again with the SAME id, which reapplies the chosen
	//! revision instead of putting the replaced file back, and it passed the target
	//! as `path` where the command's args schema takes `project` - so a client
	//! replaying it either failed argument validation or targeted the session's own
	//! project instead of the requested file. The advertised inverse must be
	//! replayable AND be the undo.
	void theTimelineRestoreAdvertisesAReplayableInverse();

	//! project.save must not advertise restore_revision when its stale slot makes
	//! that inverse fail before it can replace the saved file.
	void saveDoesNotAdvertiseAnInverseAfterStaleCleanupRefusal();

	//! UNDO whose FILE OPERATION fails must not answer success. The recorded step
	//! restores revision 0 - the file the restore rotated in - and when that
	//! artefact is gone nothing is restored: `undone: true` would tell a caller
	//! the pre-restore project is on disk when it is not.
	void undoOfARestoreThatCannotPutTheFileBackFailsTyped();

	//! REDO, the other half of the same recorded pair: it re-restores the revision
	//! the call chose, from that revision's own artefact, and a vanished artefact
	//! fails the file operation the same way.
	void redoOfARestoreWhoseRevisionIsGoneFailsTyped();

	//! When revisions.restore created the live file, undo removes it instead of
	//! restoring a prior revision. A failed removal must still refuse undo.
	void undoOfRestoreCreatedFileThatCannotBeRemovedFailsTyped();

	//! BUG 9 (cluster new-p15-27): a dangling symlink written at the path after the
	//! restore created the file is a NEWER entry, not the bytes the restore wrote,
	//! so undo must leave it alone. QFileInfo::exists() follows the link and reports
	//! false, so the check looks at the link itself.
	void undoOfRestoreCreatedDanglingSymlinkLeavesTheNewerEntry();


	//! REDO of a rotation restore whose selected bytes are no longer retained must
	//! not answer success: the redo leaves the live file unchanged, and reporting
	//! `redone: true` would claim a restore that did not happen.
	void redoOfARestoreWhoseRotationBytesAreGoneFailsTyped();

	//! The SIBLING path into the same restore helper: `project.restore_revision`
	//! records its OWN undo step, and that step's file operation can fail the
	//! same way. `undone: true` over a project file that was NOT put back is the
	//! same lie, so the sibling must report it the same way.
	void undoOfAProjectRestoreRevisionThatCannotPutTheFileBackFailsTyped();

	//! The sibling's REDO half: it re-resolves the bytes the restore brought in
	//! and re-restores them. When those bytes are no longer retained the redo
	//! restores NOTHING - and must say so instead of answering 'redone: true'.
	void redoOfAProjectRestoreRevisionWhoseRotationBytesAreGoneFailsTyped();


	//! An unsaved session has no project-file revisions. Its rotation and backup
	//! paths are built FROM the empty project path, which would name `.rev0` and
	//! `.bak` in the working directory, while its session autosave remains valid.
	void anUnsavedSessionListsNoArtefactsFromTheWorkingDirectory();

	//! A sidecar over the read bound is malformed by definition, so it is treated
	//! as absent and its autosave is reported as an upstream one would be. The
	//! sidecar is otherwise valid and names ANOTHER project: without the bound it is
	//! parsed in full and the autosave is wrongly excluded from this timeline. The
	//! padding is 1 MiB, well past the bound in ProjectRecovery.h.
	void anOversizedIdentitySidecarIsUnavailableNotRead();
};

#include "RevisionTimelineTestParts1.h"
#include "RevisionTimelineTestParts2.h"
#include "RevisionTimelineTestParts3.h"

QTEST_GUILESS_MAIN(RevisionTimelineTest)

#include "RevisionTimelineTest.moc"
