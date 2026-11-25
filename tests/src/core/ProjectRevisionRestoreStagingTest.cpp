/*
 * ProjectRevisionRestoreStagingTest.cpp - the staging file of a keep-3
 *                            restore, and the retry that has to find it
 *                            (cluster new-p12-185-port).
 *
 * The claim under test is not "a restore puts a revision back": it is that a
 * restore whose REPLACEMENT failed keeps the bytes it staged, and that a retry
 * of the same restore finds those bytes again. The rotation moves the live
 * file into revision 0, so after a failed restore the revision slot holds a
 * DIFFERENT file than the one that was asked for: a retry that stages the slot
 * again restores the file the restore was meant to replace.
 *
 * Everything here is a real file in a temp directory, written and read with
 * plain file calls: the engine's own revision functions are the only thing
 * under test.
 *
 * Headless (QTEST_GUILESS_MAIN) and offscreen-safe.
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

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "ProjectRevisions.h"

using namespace lmms;
using namespace lmms::control;

namespace
{

//! A minimal, REAL project document: the element counts are the thing the
//! restore puts back, so two documents differ by exactly one <note>.
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

//! A file of exactly \a size bytes, for a revision at or over the policy's per-revision cap.
QByteArray bytesOfSize(const qint64 size)
{
	return QByteArray(static_cast<int>(size), 'x');
}

//! True when a restore left a staging file behind in \a dir for the project file named \a fileName.
bool anyRestoreStageIn(const QString& dir, const QString& fileName)
{
	const QStringList stages = QDir(dir).entryList(
		QStringList{fileName + QStringLiteral(".rev*.restore*")}, QDir::Files | QDir::Hidden);
	return !stages.isEmpty();
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

} // namespace

class ProjectRevisionRestoreStagingTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;

	QString path(const QString& name) const
	{
		return m_dir.path() + QLatin1Char('/') + name;
	}

	//! The state a restore leaves behind when it fails at the replacement: the
	//! live file and revision 0 hold the bytes that REPLACED the target (the
	//! rotation put the live file there), and the requested bytes are only in
	//! the staging file \a staging.
	bool seedFailedRestore(const QString& project, const QByteArray& requested,
		const QByteArray& rotatedLive, const QString& staging)
	{
		return writeBytes(project, rotatedLive)
			&& writeBytes(projectRevisionPath(project, 0), rotatedLive)
			&& writeBytes(projectRevisionPath(project, 1), requested)
			&& writeBytes(staging, requested);
	}

private slots:

	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
	}

	//! A failed restore can leave its requested bytes staged after the rotation
	//! has changed revision 0. Retrying that revision must consume the pending
	//! staged target instead of staging the bytes the rotation put in the slot.
	void failedRestoreRetryUsesPendingStagedTarget()
	{
		const QString project = path(QStringLiteral("song.mmp"));
		const QByteArray requested = documentWith(2);
		const QByteArray rotatedLive = documentWith(3);
		const QString pending = projectRevisionPath(project, 0) + QStringLiteral(".restore");
		QVERIFY(seedFailedRestore(project, requested, rotatedLive, pending));

		QString error;
		QVERIFY2(restoreProjectRevision(project, 0, &error), qPrintable(error));
		QCOMPARE(readBytes(project), requested);
		QVERIFY(!QFileInfo::exists(pending));
	}

	//! The staging files of a NEW restore attempt are unique, and for a project
	//! whose file name starts with a dot they are hidden too - but a retry must
	//! still find the requested target instead of the overwritten revision 0.
	void failedRestoreRetryFindsHiddenUuidStage()
	{
		const QString project = path(QStringLiteral(".hidden.mmp"));
		const QByteArray requested = documentWith(2);
		const QByteArray rotatedLive = documentWith(3);
		const QString pending = projectRevisionPath(project, 0)
			+ QStringLiteral(".restore.4c0c55e0-809d-4923-a9d0-c708fd0adf7c");
		QVERIFY(seedFailedRestore(project, requested, rotatedLive, pending));
		QVERIFY(QFileInfo(pending).isHidden());

		QString error;
		QVERIFY2(restoreProjectRevision(project, 0, &error), qPrintable(error));
		QCOMPARE(readBytes(project), requested);
		QVERIFY(!QFileInfo::exists(pending));
	}

	//! A project file may hold glob characters: 'song[1].mmp' is a valid name.
	//! The pending stage of such a project must be found by its LITERAL name -
	//! a wildcard read the brackets as a character class, matched nothing, and
	//! the retry staged the changed revision slot instead.
	void failedRestoreRetryFindsStageForANameWithGlobCharacters()
	{
		const QString project = path(QStringLiteral("song[1].mmp"));
		const QByteArray requested = documentWith(2);
		const QByteArray rotatedLive = documentWith(3);
		const QString pending = projectRevisionPath(project, 0)
			+ QStringLiteral(".restore.4c0c55e0-809d-4923-a9d0-c708fd0adf7c");
		QVERIFY(seedFailedRestore(project, requested, rotatedLive, pending));
		QVERIFY(QFileInfo::exists(pending));

		QString error;
		QVERIFY2(restoreProjectRevision(project, 0, &error), qPrintable(error));
		QCOMPARE(readBytes(project), requested);
		QVERIFY(!QFileInfo::exists(pending));
	}

	//! BUG 8 (cluster new-p16-177): a restore whose rotation is REFUSED keeps the bytes it took out of the
	//! set. Rotation shifts the newest slot first, so a restore of revision 2 has its slot overwritten by the
	//! rev1 -> rev2 shift before the rev0 -> rev1 copy runs; when that later copy fails the restore is
	//! refused, and the stage is then the only copy of the revision that was asked for. Deleting it with the
	//! attempt - what the reporting code did - loses the revision the user selected.
	void aRefusedRotationKeepsTheRevisionTheRestoreSelected()
	{
		const QString project = path(QStringLiteral("refused-rotation.mmp"));
		const QString rev0 = projectRevisionPath(project, 0);
		const QString rev1 = projectRevisionPath(project, 1);
		const QString rev2 = projectRevisionPath(project, 2);
		const QByteArray selected = documentWith(9);
		QVERIFY(writeBytes(project, documentWith(3)));
		QVERIFY(writeBytes(rev0, documentWith(2)));
		QVERIFY(writeBytes(rev1, documentWith(6)));
		QVERIFY(writeBytes(rev2, selected));

		// rev0 is made unreadable, so the rev0 -> rev1 copy fails AFTER the rev1 -> rev2 shift has already
		// overwritten the slot of the revision this restore selects.
		struct ReadableAgain
		{
			QString path;
			~ReadableAgain() { QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner); }
		} readable{rev0};
		QVERIFY(QFile::setPermissions(rev0, QFile::Permissions()));

		QString error;
		QVERIFY2(!restoreProjectRevision(project, 2, &error),
			"a refused rotation was reported as a completed restore");
		QVERIFY(!error.isEmpty());

		// The shift really did put revision 1's bytes in the slot revision 2 held...
		QCOMPARE(readBytes(rev2), documentWith(6));
		// ...so the stage is the only copy of the bytes that were asked for, and the refusal keeps it.
		const QStringList stages = QDir(m_dir.path()).entryList(
			QStringList{QStringLiteral("refused-rotation.mmp.rev2.restore*")}, QDir::Files | QDir::Hidden,
			QDir::Name);
		QCOMPARE(stages.size(), 1);
		QCOMPARE(readBytes(m_dir.path() + QLatin1Char('/') + stages.first()), selected);

		// A retry of the same revision installs the bytes the refused attempt selected, not the ones the
		// shift left in the slot.
		QVERIFY(QFile::setPermissions(rev0, QFile::ReadOwner | QFile::WriteOwner));
		QVERIFY2(restoreProjectRevision(project, 2, &error), qPrintable(error));
		QCOMPARE(readBytes(project), selected);
	}

	//! A restore installs the whole revision, so the policy's per-revision cap binds it as it binds a
	//! rotation. A revision over MaxRevisionBytes is refused BEFORE anything is staged, rotated or replaced:
	//! the live file and the retained set keep their bytes, and no stage is left behind.
	void anOversizedRevisionIsRefusedBeforeTheRetainedSetMoves()
	{
		const QString project = path(QStringLiteral("oversize-revision.mmp"));
		const QByteArray oversized = bytesOfSize(ProjectRevisionPolicy::MaxRevisionBytes + 1);
		const QByteArray live = documentWith(3);
		QVERIFY(writeBytes(project, live));
		QVERIFY(writeBytes(projectRevisionPath(project, 0), documentWith(2)));
		QVERIFY(writeBytes(projectRevisionPath(project, 1), documentWith(4)));
		// Revision 2 is the oversized one: the rotation only reads the slots below it, so restoring it
		// reaches the install with nothing else refusing it.
		QVERIFY(writeBytes(projectRevisionPath(project, 2), oversized));

		QString error;
		QVERIFY(!restoreProjectRevision(project, 2, &error));
		QVERIFY(!error.isEmpty());
		QCOMPARE(readBytes(project), live);
		QCOMPARE(readBytes(projectRevisionPath(project, 0)), documentWith(2));
		QCOMPARE(readBytes(projectRevisionPath(project, 1)), documentWith(4));
		QCOMPARE(readBytes(projectRevisionPath(project, 2)), oversized);
		QVERIFY(!anyRestoreStageIn(m_dir.path(), QStringLiteral("oversize-revision.mmp")));
	}

	//! A retry consumes a pending stage an earlier attempt left. A stage over the cap is refused the same
	//! way: the bytes the restore would install are the stage, so the live file must not be replaced with
	//! them, and the stage itself is kept for the user.
	void anOversizedPendingStageIsNotInstalled()
	{
		const QString project = path(QStringLiteral("oversize-stage.mmp"));
		const QByteArray live = documentWith(3);
		const QString pending = projectRevisionPath(project, 0)
			+ QStringLiteral(".restore.4c0c55e0-809d-4923-a9d0-c708fd0adf7c");
		QVERIFY(writeBytes(project, live));
		QVERIFY(writeBytes(projectRevisionPath(project, 0), live));
		QVERIFY(writeBytes(projectRevisionPath(project, 1), documentWith(2)));
		QVERIFY(writeBytes(pending, bytesOfSize(ProjectRevisionPolicy::MaxRevisionBytes + 1)));

		QString error;
		QVERIFY(!restoreProjectRevision(project, 0, &error));
		QVERIFY(!error.isEmpty());
		QCOMPARE(readBytes(project), live);
		QVERIFY(QFileInfo::exists(pending));
	}

	//! The cap is a bound, not a ban: a revision of exactly MaxRevisionBytes is a legitimate revision and
	//! must still restore over the live file.
	void aRevisionExactlyAtTheCapStillRestores()
	{
		const QString project = path(QStringLiteral("at-cap-revision.mmp"));
		const QByteArray atCap = bytesOfSize(ProjectRevisionPolicy::MaxRevisionBytes);
		QVERIFY(writeBytes(project, documentWith(3)));
		QVERIFY(writeBytes(projectRevisionPath(project, 1), atCap));

		QString error;
		QVERIFY2(restoreProjectRevision(project, 1, &error), qPrintable(error));
		QCOMPARE(readBytes(project), atCap);
	}

	//! A zero-byte stage an earlier attempt left behind holds nothing to install (cluster 714): the
	//! restore of that revision is refused typed BEFORE the live file is rotated in, so a valid project
	//! is not replaced by nothing, and the empty stage is removed so a retry cannot install it either.
	void anEmptyPendingStageIsRefusedAndTheLiveProjectKept()
	{
		const QString project = path(QStringLiteral("empty-stage.mmp"));
		const QByteArray live = documentWith(3);
		const QString legacy = projectRevisionPath(project, 0) + QStringLiteral(".restore");
		const QString unique = projectRevisionPath(project, 0)
			+ QStringLiteral(".restore.4c0c55e0-809d-4923-a9d0-c708fd0adf7c");
		for (const QString& pending : {legacy, unique})
		{
			QVERIFY(writeBytes(project, live));
			QVERIFY(writeBytes(projectRevisionPath(project, 0), documentWith(2)));
			QVERIFY(writeBytes(pending, QByteArray()));

			QString error;
			QVERIFY(!restoreProjectRevision(project, 0, &error));
			QVERIFY2(error.contains(QStringLiteral("empty")), qPrintable(error));
			QCOMPARE(readBytes(project), live);
			QCOMPARE(readBytes(projectRevisionPath(project, 0)), documentWith(2));
			QVERIFY2(!QFileInfo::exists(pending), qPrintable(pending + QStringLiteral(" was not removed")));
		}
	}

#ifdef Q_OS_WIN
	//! Windows: the replacing rename used to narrow the staged and destination paths through the ANSI
	//! code page, so a project under a directory name that code page cannot represent could not be
	//! rotated or restored. The name carries an emoji, which no ANSI code page holds. Where the code
	//! page does hold every character (UTF-8), the defect cannot occur and the test skips.
	void rotationAndRestoreUseUnicodePathOutsideAnsiCodePage()
	{
		const QString dirName = QStringLiteral("proj-\U0001F3B5");
		if (QString::fromLocal8Bit(dirName.toLocal8Bit()) == dirName)
		{
			QSKIP("the active ANSI code page represents every character of the directory name");
		}
		QVERIFY(QDir(m_dir.path()).mkdir(dirName));
		const QString project = m_dir.path() + QLatin1Char('/') + dirName + QStringLiteral("/song.mmp");

		QVERIFY(writeBytes(project, documentWith(3)));
		bool refused = false;
		QString reason;
		QCOMPARE(rotateProjectRevision(project, &refused, &reason), 1);
		QVERIFY2(!refused, qPrintable(reason));
		QCOMPARE(readBytes(projectRevisionPath(project, 0)), documentWith(3));

		// The second rotation shifts revision 0 to revision 1 and replaces revision 0: both renames run.
		QVERIFY(writeBytes(project, documentWith(4)));
		QCOMPARE(rotateProjectRevision(project, &refused, &reason), 2);
		QVERIFY2(!refused, qPrintable(reason));
		QCOMPARE(readBytes(projectRevisionPath(project, 0)), documentWith(4));
		QCOMPARE(readBytes(projectRevisionPath(project, 1)), documentWith(3));

		// The restore replaces the live file with revision 1 through the same rename.
		QVERIFY(writeBytes(project, documentWith(5)));
		QString error;
		QVERIFY2(restoreProjectRevision(project, 1, &error), qPrintable(error));
		QCOMPARE(readBytes(project), documentWith(3));
	}
#endif
};

QTEST_GUILESS_MAIN(ProjectRevisionRestoreStagingTest)

#include "ProjectRevisionRestoreStagingTest.moc"
