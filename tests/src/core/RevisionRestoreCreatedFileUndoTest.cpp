/*
 * RevisionRestoreCreatedFileUndoTest.cpp - what control.undo of a revisions.restore
 *                            removes when the restore CREATED the project file
 *                            (cluster new-p15-27, BUG 9).
 *
 * The claim under test: a restore to a path that had no file is undone by removing
 * the file the restore wrote - and only that. The undo used to remove whatever was
 * at the path when it ran, so a different project written there after the restore
 * was deleted with it. Undo now removes the file only while it still holds the
 * bytes the restore wrote; anything else is left in place and reported as a typed
 * refusal, never as a silent success.
 *
 * Everything runs through the registry, the way a socket client does it, against
 * real files in a temp directory. The dangling-symlink and directory variants of
 * the same rule live beside their siblings in RevisionTimelineTest.
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

#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"

using namespace lmms;
using namespace lmms::control;

namespace
{

//! A minimal, REAL project document with \a notes notes, so two documents differ
//! by their bytes.
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

//! A command dispatched the way a socket client dispatches it.
ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace

class RevisionRestoreCreatedFileUndoTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;

	QString path(const QString& name) const
	{
		return m_dir.path() + QLatin1Char('/') + name;
	}

	//! Restores the `.bak` over \a project, which must have no file on disk, so the
	//! restore CREATES the project file with the backup's bytes.
	void restoreCreatesProject(const QString& project)
	{
		QVERIFY(writeBytes(project + QStringLiteral(".bak"), documentWith(1)));
		QVERIFY(!QFileInfo::exists(project));
		const ControlResult restored = run(QStringLiteral("revisions.restore"),
			QJsonObject{{QStringLiteral("project"), project},
				{QStringLiteral("id"), QStringLiteral("backup")}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(readBytes(project), documentWith(1));
	}

private slots:

	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		// The handlers read the autosave file too; point them at this test's own
		// directory so a machine's real recover.mmp is never read.
		qputenv("ZENE_RECOVERY_FILE", path(QStringLiteral("recover.mmp")).toUtf8());
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The normal path, unchanged: the restore created the file and nothing has
	//! written to it since, so undo removes it and reports success.
	void undoRemovesARestoreCreatedFileThatStillHoldsTheRestoredBytes()
	{
		const QString project = path(QStringLiteral("unchanged-created.mmp"));
		restoreCreatesProject(project);

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QVERIFY2(!QFileInfo::exists(project), "undo did not remove the file the restore created");
	}

	//! BUG 9 (cluster new-p15-27): the restore created the project file, then a
	//! DIFFERENT project was written to the same path. Undo used to remove whatever
	//! was there, so the newer project was deleted. Undo now leaves it in place and
	//! refuses, typed, naming the restore.
	void undoLeavesAProjectWrittenAtThePathAfterTheRestore()
	{
		const QString project = path(QStringLiteral("replaced-created.mmp"));
		restoreCreatesProject(project);
		QVERIFY(writeBytes(project, documentWith(7)));

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(!undone.ok, "control.undo answered success although the file at the path is NOT the "
			"one the restore created - undo deleted a newer project");
		QCOMPARE(undone.errorKind, ControlErrorKind::Refused);
		QVERIFY2(undone.errorMessage.contains(QStringLiteral("revisions.restore")),
			qPrintable(undone.errorMessage));
		QVERIFY2(QFileInfo::exists(project), "undo removed the newer project written after the restore");
		QCOMPARE(readBytes(project), documentWith(7));
	}

	//! The path was emptied after the restore. There is nothing for undo to remove,
	//! so it still succeeds, as it did before the fix.
	void undoOfARestoreCreatedFileWithNothingLeftStillSucceeds()
	{
		const QString project = path(QStringLiteral("emptied-created.mmp"));
		restoreCreatesProject(project);
		QVERIFY(QFile::remove(project));

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QVERIFY(!QFileInfo::exists(project));
	}
};

QTEST_GUILESS_MAIN(RevisionRestoreCreatedFileUndoTest)

#include "RevisionRestoreCreatedFileUndoTest.moc"
