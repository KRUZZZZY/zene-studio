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
};

QTEST_GUILESS_MAIN(ProjectRevisionRestoreStagingTest)

#include "ProjectRevisionRestoreStagingTest.moc"
