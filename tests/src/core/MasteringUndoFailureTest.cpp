/*
 * MasteringUndoFailureTest.cpp - a mastering undo reports the file operations it
 *                                 could not complete (cluster 4056, BUG 8)
 *
 * `recordMasteringUndo` records the inverse of one `mastering.run`: remove every
 * file the run created, write back every file the directory already held. The
 * closure used to ignore the result of `QFile::remove()` and `QFile::write()`, so
 * an undo over a read-only directory answered "undone" while the run's outputs
 * were still on disk. The failure now travels through the channel the rest of the
 * undo system uses (`recordActionRestoreFailure`, read back by `control.undo` as a
 * typed `Refused`), and this file holds both halves to account on real files:
 *
 *   - a removal the filesystem refused, and a write it refused, are REPORTED;
 *   - an undo that can do its work is silent and byte-exact.
 *
 * The failure is provoked with directory and file permissions, so it needs no
 * timing, no sleep and no race. Root ignores those permissions, so the failure
 * cases skip there (QSKIP) rather than assert a behaviour the kernel does not
 * implement for euid 0.
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
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

#include "ControlMasteringSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "ProjectJournal.h"

using namespace lmms;
// The mastering capture/undo helpers and the action-restore failure channel all
// live in lmms::control; the test drives them directly rather than through a
// command so it can read the recorded reason back itself.
using namespace lmms::control;

namespace
{

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

#ifndef Q_OS_UNIX
//! Only the POSIX permission model can make a removal fail deterministically.
void skipUnlessPermissionsCanRefuse()
{
	QSKIP("file permissions cannot refuse a removal on this platform");
}
#else
void skipUnlessPermissionsCanRefuse()
{
	if (::geteuid() == 0) { QSKIP("euid 0 ignores the permission bits this test refuses with"); }
}
#endif

} // namespace

class MasteringUndoFailureTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_root;

	//! The directory one run masters into, and the two paths this test watches.
	QString m_runDir;
	QString m_created;   //!< the file the run creates (absent before the run)
	QString m_replaced;  //!< the file the run overwrites (held bytes before it)
	QByteArray m_original;  //!< what m_replaced held before the run

	//! Lays down one run's effect on disk - `mix.wav` overwritten with new bytes,
	//! `alt.wav` created - and records its inverse from the before-state the
	//! product's own capture reads. False means a step of the SETUP failed; the
	//! assertions are in the caller because QVERIFY needs a void function.
	bool layDownRun(const QString& name)
	{
		m_runDir = m_root.path() + QLatin1Char('/') + name;
		if (!QDir().mkpath(m_runDir)) { return false; }
		m_created = m_runDir + QStringLiteral("/alt.wav");
		m_replaced = m_runDir + QStringLiteral("/mix.wav");
		m_original = QByteArray("RIFF-before-the-run");
		if (!writeBytes(m_replaced, m_original)) { return false; }

		QMap<QString, QByteArray> before;
		ControlResult error;
		if (!captureMasteringWavDirectory(m_runDir, &before, &error)) { return false; }
		if (before.value(m_replaced) != m_original) { return false; }

		// The run itself: the mastered candidate appears, the previous one is
		// replaced. `created` is what the run added, which is what undo removes.
		if (!writeBytes(m_replaced, QByteArray("RIFF-after-the-run"))) { return false; }
		if (!writeBytes(m_created, QByteArray("RIFF-the-run-created-this"))) { return false; }
		recordMasteringUndo(QStringList{m_created}, before);
		return Engine::projectJournal() != nullptr && Engine::projectJournal()->canUndo();
	}

	//! Gives the run directory owner read+traverse but NOT write: creating and
	//! removing an entry inside it is then refused by the kernel, byte writes to
	//! an already-open file are not. Cleared again in cleanup() so QTemporaryDir
	//! can delete the tree.
	void makeDirectoryUnwritable()
	{
		QVERIFY(QFile::setPermissions(m_runDir,
			QFileDevice::ReadOwner | QFileDevice::ExeOwner));
		QVERIFY2(!(QFile::permissions(m_runDir) & QFileDevice::WriteOwner),
			"the directory is still owner-writable, so the removal could not be refused");
	}

	void cleanup() { QFile::setPermissions(m_runDir, QFileDevice::ReadOwner
		| QFileDevice::WriteOwner | QFileDevice::ExeOwner); }

private slots:

	void initTestCase()
	{
		QVERIFY(m_root.isValid());
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! BUG 8, half one: the run created a file and the filesystem refuses to
	//! remove it. Before the fix the undo was silent about it.
	void undoReportsACreatedFileItCouldNotRemove()
	{
		skipUnlessPermissionsCanRefuse();
		QVERIFY(layDownRun(QStringLiteral("cannot-remove")));
		makeDirectoryUnwritable();
		clearActionRestoreFailure();

		Engine::projectJournal()->undo();

		const QString reason = takeActionRestoreFailure();
		QVERIFY2(!reason.isEmpty(), "undo removed nothing and reported nothing: the refusal was swallowed");
		QVERIFY2(reason.contains(m_created), qPrintable(reason));
		QVERIFY2(reason.contains(QStringLiteral("could not remove")), qPrintable(reason));
		QVERIFY2(QFileInfo::exists(m_created), "the file is gone, so the removal was not refused");
		// The other half of the same step still ran: a failure does not abort the
		// inverse, it is reported.
		QCOMPARE(readBytes(m_replaced), m_original);
	}

	//! BUG 8, half two: the file the run replaced cannot be written back.
	void undoReportsAFileItCouldNotWriteBack()
	{
		skipUnlessPermissionsCanRefuse();
		QVERIFY(layDownRun(QStringLiteral("cannot-write")));
		QVERIFY2(QFile::setPermissions(m_replaced, QFileDevice::ReadOwner),
			"could not make the file read-only");
		clearActionRestoreFailure();

		Engine::projectJournal()->undo();

		const QString reason = takeActionRestoreFailure();
		QVERIFY2(!reason.isEmpty(), "the failed write-back was swallowed");
		QVERIFY2(reason.contains(m_replaced), qPrintable(reason));
		QVERIFY2(reason.contains(QStringLiteral("could not write")), qPrintable(reason));
		QVERIFY2(!QFileInfo::exists(m_created), "the created file was not removed");
	}

	//! The user's path to the same failure: `control.undo` turns the recorded
	//! reason into a typed Refused naming the file, instead of answering
	//! `undone: true` over files that are still there.
	void controlUndoReportsTheMasteringFailure()
	{
		skipUnlessPermissionsCanRefuse();
		QVERIFY(layDownRun(QStringLiteral("control-undo")));
		makeDirectoryUnwritable();

		const ControlResult undone = ControlRegistry::instance()->invoke(
			QStringLiteral("control.undo"));

		QVERIFY2(!undone.ok, "control.undo answered success while the run's output is still on disk");
		QCOMPARE(undone.errorKind, ControlErrorKind::Refused);
		QVERIFY2(undone.errorMessage.contains(m_created), qPrintable(undone.errorMessage));
		QVERIFY2(QFileInfo::exists(m_created), "the file is gone, so the removal was not refused");
	}

	//! The success path, byte-exact: the created file is removed, the replaced
	//! file holds exactly what it held before the run, and nothing is reported.
	void undoOfARunThatCouldBeTakenBackRestoresAndRemovesByteExactly()
	{
		QVERIFY(layDownRun(QStringLiteral("taken-back")));
		clearActionRestoreFailure();

		Engine::projectJournal()->undo();

		QVERIFY2(takeActionRestoreFailure().isEmpty(), "a successful undo reported a failure");
		QVERIFY2(!QFileInfo::exists(m_created), "undo did not remove the file the run created");
		QCOMPARE(readBytes(m_replaced), m_original);
	}
};

QTEST_GUILESS_MAIN(MasteringUndoFailureTest)

#include "MasteringUndoFailureTest.moc"
