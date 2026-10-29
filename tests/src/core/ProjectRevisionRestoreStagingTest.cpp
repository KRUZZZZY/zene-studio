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
