/*
 * RevisionRestoreUndoAfterSaveTest.cpp - the undo of a revision restore after the
 *                    keep-3 set has rotated (cluster new-p9-58, BUG 2).
 *
 * The claim under test: undoing a `revisions.restore` (or its sibling
 * `project.restore_revision`) puts back the file that restore REPLACED, whatever
 * has happened to the keep-3 set since. The undo used to restore revision 0 by
 * NUMBER, and revision 0 is a POSITION: the next rotation - the one every save
 * performs, and the one a second restore performs - shifts the replaced file out
 * of slot 0, so the undo wrote a file the restore never touched over the live one
 * and answered success. The undo now restores the retained revision that holds the
 * bytes the restore replaced, and REFUSES typed when no retained revision holds
 * them any more, rather than overwrite the project with someone else's bytes.
 *
 * Everything runs through the registry, the way a socket client does it, against
 * real files in a temp directory. The rotation a save performs is driven through
 * the same `rotateProjectRevision` call `project.save` makes: driving the command
 * itself would put the save's own recorded inverse on top of the restore's step,
 * and the step under test is the RESTORE's, so the save's command record is left
 * out of the way and the rotation it performs is kept.
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

//! One save's effect on the disk: the live file is rotated into the keep-3 set
//! (exactly the rotation project.save runs before it writes) and the new bytes
//! land at the path. True when the rotation kept a revision and the write
//! landed - a save that kept nothing would make the case below prove nothing.
bool rotateAndWrite(const QString& project, const QByteArray& written)
{
	bool refused = false;
	QString reason;
	const int kept = rotateProjectRevision(project, &refused, &reason);
	return !refused && kept > 0 && writeBytes(project, written);
}

} // namespace

class RevisionRestoreUndoAfterSaveTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;

	QString path(const QString& name) const
	{
		return m_dir.path() + QLatin1Char('/') + name;
	}

	//! A project on disk (3 notes) whose keep-3 set holds 2 notes and 1 note, so a
	//! restore has two distinguishable revisions to choose from.
	void writeThreeRevisionFixture(const QString& project)
	{
		QVERIFY(writeBytes(project, documentWith(3)));
		QVERIFY(writeBytes(projectRevisionPath(project, 0), documentWith(2)));
		QVERIFY(writeBytes(projectRevisionPath(project, 1), documentWith(1)));
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

	//! The normal path, unchanged: nothing rotates between the restore and the
	//! undo, so the replaced file is still revision 0 and the undo puts it back.
	void undoWithoutARotationRestoresTheReplacedFile()
	{
		const QString project = path(QStringLiteral("plain.mmp"));
		writeThreeRevisionFixture(project);

		const ControlResult restored = run(QStringLiteral("revisions.restore"),
			QJsonObject{{QStringLiteral("project"), project},
				{QStringLiteral("id"), QStringLiteral("rev0")}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(readBytes(project), documentWith(2));

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QCOMPARE(readBytes(project), documentWith(3));
	}

	//! BUG 2 (cluster new-p9-58): a save rotates the keep-3 set between the
	//! restore and its undo, so revision 0 now holds the RESTORED document while
	//! the file the restore replaced moved to revision 1. The undo used to write
	//! revision 0 - a file this restore never replaced - over the live project and
	//! report `undone: true`. It must put the replaced file back.
	void undoAfterASaveRotatedTheSetRestoresTheReplacedFile()
	{
		const QString project = path(QStringLiteral("rotated.mmp"));
		writeThreeRevisionFixture(project);

		const ControlResult restored = run(QStringLiteral("revisions.restore"),
			QJsonObject{{QStringLiteral("project"), project},
				{QStringLiteral("id"), QStringLiteral("rev0")}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(readBytes(project), documentWith(2));

		// The save: the keep-3 set rotates (revision 0 becomes the file the save
		// replaced, the file the restore replaced moves to revision 1) and the
		// saved bytes land at the path.
		QVERIFY(rotateAndWrite(project, documentWith(4)));
		QCOMPARE(readBytes(project), documentWith(4));
		QCOMPARE(readBytes(projectRevisionPath(project, 0)), documentWith(2));
		QCOMPARE(readBytes(projectRevisionPath(project, 1)), documentWith(3));

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QVERIFY2(readBytes(project) == documentWith(3),
			"control.undo of revisions.restore did not put back the file the restore replaced - it "
			"restored revision 0 by NUMBER, and that slot now holds the restored document");
	}

	//! The same defect in the sibling command (project.restore_revision), which
	//! records the same shape of step and must behave the same way.
	void undoOfProjectRestoreRevisionAfterASaveRotatedTheSetRestoresTheReplacedFile()
	{
		const QString project = path(QStringLiteral("sibling-rotated.mmp"));
		writeThreeRevisionFixture(project);

		const ControlResult restored = run(QStringLiteral("project.restore_revision"),
			QJsonObject{{QStringLiteral("path"), project}, {QStringLiteral("revision"), 0}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(readBytes(project), documentWith(2));

		QVERIFY(rotateAndWrite(project, documentWith(4)));
		QCOMPARE(readBytes(projectRevisionPath(project, 0)), documentWith(2));
		QCOMPARE(readBytes(projectRevisionPath(project, 1)), documentWith(3));

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QVERIFY2(readBytes(project) == documentWith(3),
			"control.undo of project.restore_revision did not put back the file the restore "
			"replaced - it restored revision 0 by NUMBER, and that slot now holds the restored "
			"document");
	}

	//! Registry-only route to the same rotation: a SECOND restore rotates the set
	//! (that is what every restore does before it writes), so the first restore's
	//! undo no longer finds its file in revision 0. Both undos are driven through
	//! control.undo, the way a client does it.
	void undoAfterASecondRestoreRotatedTheSetRestoresTheReplacedFile()
	{
		const QString project = path(QStringLiteral("second-restore.mmp"));
		writeThreeRevisionFixture(project);
		QVERIFY(writeBytes(project + QStringLiteral(".bak"), documentWith(9)));

		const ControlResult first = run(QStringLiteral("revisions.restore"),
			QJsonObject{{QStringLiteral("project"), project},
				{QStringLiteral("id"), QStringLiteral("rev0")}});
		QVERIFY2(first.ok, qPrintable(first.errorMessage));
		QCOMPARE(readBytes(project), documentWith(2));

		const ControlResult second = run(QStringLiteral("revisions.restore"),
			QJsonObject{{QStringLiteral("project"), project},
				{QStringLiteral("id"), QStringLiteral("backup")}});
		QVERIFY2(second.ok, qPrintable(second.errorMessage));
		QCOMPARE(readBytes(project), documentWith(9));

		// The second restore's undo: its own replaced file is still revision 0.
		const ControlResult undoneSecond = run(QStringLiteral("control.undo"));
		QVERIFY2(undoneSecond.ok, qPrintable(undoneSecond.errorMessage));
		QCOMPARE(readBytes(project), documentWith(2));

		// The first restore's undo: revision 0 holds the restored document now, and
		// the file the first restore replaced is one slot up.
		const ControlResult undoneFirst = run(QStringLiteral("control.undo"));
		QVERIFY2(undoneFirst.ok, qPrintable(undoneFirst.errorMessage));
		QVERIFY2(readBytes(project) == documentWith(3),
			"control.undo of the FIRST revisions.restore wrote revision 0 - the second restore's "
			"displaced file - over the live project instead of the file the first restore replaced");
	}

	//! Exhaustion: three saves push the replaced file out of the keep-3 set
	//! entirely. No retained revision holds those bytes any more, so the undo
	//! REFUSES typed and names the command - it does not overwrite the live project
	//! with whatever revision 0 happens to hold.
	void undoRefusesTypedWhenTheReplacedBytesLeftTheKeepThreeSet()
	{
		const QString project = path(QStringLiteral("evicted.mmp"));
		writeThreeRevisionFixture(project);

		const ControlResult restored = run(QStringLiteral("revisions.restore"),
			QJsonObject{{QStringLiteral("project"), project},
				{QStringLiteral("id"), QStringLiteral("rev0")}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(readBytes(project), documentWith(2));

		QVERIFY(rotateAndWrite(project, documentWith(4)));
		QVERIFY(rotateAndWrite(project, documentWith(5)));
		QVERIFY(rotateAndWrite(project, documentWith(6)));
		const QByteArray before = readBytes(project);
		for (int revision = 0; revision < ProjectRevisionPolicy::Keep; ++revision)
		{
			QVERIFY2(readBytes(projectRevisionPath(project, revision)) != documentWith(3),
				"a retained revision still holds the replaced file, so this case proves nothing");
		}

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(!undone.ok, "control.undo answered success although the bytes the restore "
			"replaced are no longer retained - it wrote another revision over the live project");
		QCOMPARE(undone.errorKind, ControlErrorKind::Refused);
		QVERIFY2(undone.errorMessage.contains(QStringLiteral("revisions.restore")),
			qPrintable(undone.errorMessage));
		QCOMPARE(readBytes(project), before);
	}
};

QTEST_GUILESS_MAIN(RevisionRestoreUndoAfterSaveTest)

#include "RevisionRestoreUndoAfterSaveTest.moc"
