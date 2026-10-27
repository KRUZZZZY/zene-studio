/*
 * ProjectRestoreMissingTargetTest.cpp - project.restore_revision into a project
 *                            file that does not exist (cluster new-p15-24).
 *
 * The claim under test: a restore whose live target was ABSENT records an undo
 * that REMOVES the file the restore created, because there were no live bytes to
 * rotate into revision 0. Restoring revision 0 cannot undo the creation of the
 * target, and it writes whatever unrelated bytes revision 0 happens to hold.
 * The redo still re-restores the revision the call chose.
 *
 * Everything here is a real file in a temp directory, driven through the
 * registry - the path a socket client takes - so control.undo and control.redo
 * dispatch the recorded steps exactly as a user would.
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

#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectRevisions.h"
#include "ReversibilityTestSupport.h"

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

//! A command dispatched the way a socket client dispatches it.
ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace

class ProjectRestoreMissingTargetTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;

	QString path(const QString& name) const
	{
		return m_dir.path() + QLatin1Char('/') + name;
	}

private slots:

	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		// The registry refuses (busy) until the model is up, as in every
		// command-group test.
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The finding's scenario: the project was deleted but its keep-3 set
	//! survived. Restoring revision 1 creates the file; undo must return the
	//! path to its missing state, and redo must bring revision 1 back.
	void undoOfRestoreIntoMissingPathRemovesTheCreatedFile()
	{
		const QString project = path(QStringLiteral("deleted-song.mmp"));
		QVERIFY(writeBytes(projectRevisionPath(project, 0), documentWith(2)));
		QVERIFY(writeBytes(projectRevisionPath(project, 1), documentWith(1)));
		QVERIFY(!QFileInfo::exists(project));

		const ControlResult restored = run(QStringLiteral("project.restore_revision"),
			QJsonObject{{QStringLiteral("path"), project}, {QStringLiteral("revision"), 1}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(readBytes(project), documentWith(1));

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QVERIFY2(!QFileInfo::exists(project),
			"undo of a restore into a missing path left the file behind");

		const ControlResult redone = run(QStringLiteral("control.redo"));
		QVERIFY2(redone.ok, qPrintable(redone.errorMessage));
		QCOMPARE(readBytes(project), documentWith(1));
	}

	//! With no revision 0 at all, the old undo (restore revision 0) could not
	//! run: it failed, and the caller's control.undo reported that failure for a
	//! file that was simply created and should be removed.
	void undoOfRestoreIntoMissingPathWithoutRevisionZeroRemovesTheFile()
	{
		const QString project = path(QStringLiteral("no-rev0-song.mmp"));
		QVERIFY(writeBytes(projectRevisionPath(project, 1), documentWith(1)));
		QVERIFY(!QFileInfo::exists(projectRevisionPath(project, 0)));
		QVERIFY(!QFileInfo::exists(project));

		const ControlResult restored = run(QStringLiteral("project.restore_revision"),
			QJsonObject{{QStringLiteral("path"), project}, {QStringLiteral("revision"), 1}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QVERIFY2(!QFileInfo::exists(project),
			"undo of a restore into a missing path left the file behind");
	}

	//! The advertised inverse must be the one that really reverses the restore:
	//! for a missing target that is control.undo (which removes the file), not
	//! project.restore_revision revision 0 (which writes revision 0 over nothing).
	void restoreIntoMissingPathAdvertisesUndoAsItsInverse()
	{
		const QString project = path(QStringLiteral("advertised-song.mmp"));
		QVERIFY(writeBytes(projectRevisionPath(project, 1), documentWith(1)));
		QVERIFY(!QFileInfo::exists(project));

		const ControlResult restored = run(QStringLiteral("project.restore_revision"),
			QJsonObject{{QStringLiteral("path"), project}, {QStringLiteral("revision"), 1}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		// The advertised inverse as the transaction journal recorded it.
		const QJsonObject inverse = revtest::stateOf(QStringLiteral("project.restore_revision"))
			.value(QStringLiteral("inverse")).toObject();
		QVERIFY2(!inverse.isEmpty(), "project.restore_revision advertised no inverse at all");
		QCOMPARE(inverse.value(QStringLiteral("op")).toString(), QStringLiteral("control.undo"));
	}

	//! Guard: a restore over an EXISTING file keeps the old behaviour - undo puts
	//! the replaced bytes back from revision 0. Passes before and after the fix.
	void undoOfRestoreOverExistingFileRestoresThePreviousBytes()
	{
		const QString project = path(QStringLiteral("live-song.mmp"));
		QVERIFY(writeBytes(project, documentWith(3)));
		QVERIFY(writeBytes(projectRevisionPath(project, 1), documentWith(1)));

		const ControlResult restored = run(QStringLiteral("project.restore_revision"),
			QJsonObject{{QStringLiteral("path"), project}, {QStringLiteral("revision"), 1}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(readBytes(project), documentWith(1));

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QVERIFY(QFileInfo::exists(project));
		QCOMPARE(readBytes(project), documentWith(3));
	}
};

QTEST_GUILESS_MAIN(ProjectRestoreMissingTargetTest)

#include "ProjectRestoreMissingTargetTest.moc"
