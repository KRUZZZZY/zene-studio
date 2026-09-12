/*
 * ProjectRecoveryTest.cpp - the recovery-file decision, exercised headless
 *
 * Copyright (c) 2026 Zene Studio developers
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
 *
 */

#include "ProjectRecovery.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace lmms::ProjectRecovery;

namespace
{

//! Readable failure text: the verdict and the reason the production code gave.
QString why(const RecoveryDecision& decision)
{
	return QStringLiteral("%1: %2")
		.arg(QString::fromLatin1(recoveryVerdictName(decision.verdict)), decision.detail);
}

//! A recovery file of `project` written at `autosaved`, with the project file
//! itself last saved at `saved` (invalid = the project file is not there).
RecoveryInfo recoveryOf(const QString& recoveryFile, const QString& project,
					const QDateTime& autosaved, const QDateTime& saved)
{
	RecoveryInfo info;
	info.fileExists = true;
	info.fileSize = 4096;
	info.fileModified = autosaved;
	info.hasIdentity = true;
	info.sourceProject = project;
	info.sourceProjectExists = saved.isValid();
	info.sourceProjectModified = saved;
	return info;
}

QDateTime at(int day, int hour)
{
	// Local time is deliberate: every comparison here is between *instants*
	// (QDateTime's comparators and the production code's file times are both
	// instant-based), so naming a zone adds nothing but a version dependency.
	return QDateTime(QDate(2026, 9, day), QTime(hour, 0, 0));
}

//! Write `bytes` to `path` and give it the mtime `stamp`.
bool writeWithTime(const QString& path, const QByteArray& bytes, const QDateTime& stamp)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	if (file.write(bytes) != bytes.size()) { return false; }
	// flush() first: QFile buffers, and close() would flush AFTER setFileTime(),
	// letting the kernel stamp the mtime to "now" and quietly invalidating every
	// freshness comparison the test is about.
	file.flush();
	// setFileTime() acts on the open descriptor (utimensat(fd)); on a closed
	// QFile it fails, which is what a plain "returns true" assumption misses.
	const bool stamped = file.setFileTime(stamp, QFileDevice::FileModificationTime);
	file.close();
	return stamped;
}

} // namespace

class ProjectRecoveryTest : public QObject
{
	Q_OBJECT

private slots:
	// ---------------------------------------------------------------- offer --

	void freshRecoveryOfTheProjectBeingOpenedIsOffered()
	{
		const RecoveryDecision d = decideRecovery(
			recoveryOf("/tmp/rec.mmp", "/home/u/songs/Track.mmp", at(11, 12), at(11, 11)),
			"/home/u/songs/Track.mmp");
		QVERIFY2(d.offered(), qPrintable(why(d)));
		QCOMPARE(d.projectLabel, QString("Track.mmp"));
	}

	void aLaunchThatNamesNoProjectIsOfferedItsRecovery()
	{
		// Plain relaunch: there is no project to disagree with, so the recovery
		// file IS the session to offer -- "recover last session".
		const RecoveryDecision d = decideRecovery(
			recoveryOf("/tmp/rec.mmp", "/home/u/songs/Track.mmp", at(11, 12), at(11, 11)), "");
		QVERIFY2(d.offered(), qPrintable(why(d)));
		QCOMPARE(d.projectLabel, QString("Track.mmp"));
	}

	void aProjectWithNoFilenameYetIsOffered()
	{
		// Identity present but empty: an autosave of an untitled project. It must
		// still be offered, and it must not be labelled as some other project.
		const RecoveryDecision d = decideRecovery(
			recoveryOf("/tmp/rec.mmp", "", at(11, 12), QDateTime()), "/home/u/songs/Track.mmp");
		QVERIFY2(d.offered(), qPrintable(why(d)));
		QVERIFY(d.projectLabel.isEmpty());
	}

	void anUpstreamStyleRecoveryWithoutIdentityIsStillOffered()
	{
		// Back-compat: a recovery file written before this sidecar existed (or by
		// upstream LMMS) carries no identity. Not knowing which project it is must
		// not throw a user's unsaved session away.
		RecoveryInfo info;
		info.fileExists = true;
		info.fileSize = 4096;
		info.fileModified = at(11, 12);
		const RecoveryDecision d = decideRecovery(info, "/home/u/songs/Track.mmp");
		QVERIFY2(d.offered(), qPrintable(why(d)));
	}

	// ------------------------------------------- negative controls (safety) ---

	void aRecoveryOlderThanTheProjectIsNotOffered()
	{
		// NEGATIVE CONTROL 1. The project was saved AFTER the recovery was
		// written, so the file on disk already holds at least as much: offering
		// the recovery would silently take the user backwards.
		const RecoveryDecision d = decideRecovery(
			recoveryOf("/tmp/rec.mmp", "/home/u/songs/Track.mmp",
				at(11, 10), // autosaved at 10:00
				at(11, 11)), // project saved at 11:00
			"/home/u/songs/Track.mmp");
		QCOMPARE(QString::fromLatin1(recoveryVerdictName(d.verdict)), QString("Stale"));
		QVERIFY(!d.offered());
	}

	void aRecoveryAsNewAsTheProjectIsNotOffered()
	{
		// Equal stamps are treated as stale for the same reason: there is nothing
		// in the recovery the project file does not already have.
		const RecoveryDecision d = decideRecovery(
			recoveryOf("/tmp/rec.mmp", "/home/u/songs/Track.mmp", at(11, 11), at(11, 11)),
			"/home/u/songs/Track.mmp");
		QCOMPARE(QString::fromLatin1(recoveryVerdictName(d.verdict)), QString("Stale"));
		QVERIFY(!d.offered());
	}

	void aRecoveryOfAnotherProjectIsNotOfferedAsRecoveryOfThisOne()
	{
		// NEGATIVE CONTROL 2. Launching with an explicit project must not be
		// hijacked by a recovery of a different one. The recovery is newer than
		// its own project, so only the identity check can refuse it here.
		const RecoveryDecision d = decideRecovery(
			recoveryOf("/tmp/rec.mmp", "/home/u/songs/Other.mmp", at(11, 12), at(11, 11)),
			"/home/u/songs/Track.mmp");
		QCOMPARE(QString::fromLatin1(recoveryVerdictName(d.verdict)), QString("OtherProject"));
		QVERIFY(!d.offered());
		// it still says whose recovery it is, so a caller can report it honestly
		QCOMPARE(d.projectLabel, QString("Other.mmp"));
	}

	void aStaleRecoveryOfAnotherProjectIsRefusedForBothReasons()
	{
		// Both safety rules point the same way; whichever is evaluated first, the
		// answer must be "not offered".
		const RecoveryDecision d = decideRecovery(
			recoveryOf("/tmp/rec.mmp", "/home/u/songs/Other.mmp", at(11, 10), at(11, 11)),
			"/home/u/songs/Track.mmp");
		QVERIFY(!d.offered());
		QVERIFY(d.verdict == RecoveryVerdict::OtherProject || d.verdict == RecoveryVerdict::Stale);
	}

	void aMissingRecoveryFileIsNotOffered()
	{
		RecoveryInfo info;
		const RecoveryDecision d = decideRecovery(info, "/home/u/songs/Track.mmp");
		QCOMPARE(QString::fromLatin1(recoveryVerdictName(d.verdict)), QString("NotPresent"));
		QVERIFY(!d.offered());
	}

	void anEmptyRecoveryFileIsNotOffered()
	{
		// A zero-byte recover.mmp is a write that never finished: it holds nothing.
		RecoveryInfo info;
		info.fileExists = true;
		info.fileSize = 0;
		info.fileModified = at(11, 12);
		const RecoveryDecision d = decideRecovery(info, "/home/u/songs/Track.mmp");
		QCOMPARE(QString::fromLatin1(recoveryVerdictName(d.verdict)), QString("Empty"));
		QVERIFY(!d.offered());
	}

	void aRecoveryWhoseProjectFileIsGoneIsStillOffered()
	{
		// The project was deleted or moved away, but the crash still lost work:
		// freshness cannot be decided, so the recovery stands on its own.
		const RecoveryDecision d = decideRecovery(
			recoveryOf("/tmp/rec.mmp", "/home/u/songs/Gone.mmp", at(11, 12), QDateTime()),
			"/home/u/songs/Gone.mmp");
		QVERIFY2(d.offered(), qPrintable(why(d)));
	}

	// ---------------------------------------------------------------- sidecar --

	void theIdentitySidecarRoundTrips()
	{
		const QString text = formatRecoveryIdentity("/home/u/songs/Track.mmp", at(11, 12));
		QString project;
		QDateTime saved;
		QVERIFY(parseRecoveryIdentity(text, project, saved));
		QCOMPARE(project, QString("/home/u/songs/Track.mmp"));
		QCOMPARE(saved, at(11, 12));
	}

	void aValueContainingEqualsSurvivesTheRoundTrip()
	{
		const QString odd = "/home/u/my=songs/Track.mmp";
		QString project;
		QDateTime saved;
		QVERIFY(parseRecoveryIdentity(formatRecoveryIdentity(odd, at(11, 12)), project, saved));
		QCOMPARE(project, odd);
	}

	void anAbsentPathStillRoundTrips()
	{
		QString project("unset");
		QDateTime saved;
		QVERIFY(parseRecoveryIdentity(formatRecoveryIdentity("", at(11, 12)), project, saved));
		QVERIFY(project.isEmpty());
		QCOMPARE(saved, at(11, 12));
	}

	void anUnreadableIdentityIsTreatedAsAbsent_data()
	{
		QTest::addColumn<QString>("text");
		QTest::newRow("empty") << QString("");
		QTest::newRow("garbage") << QString("not a sidecar at all\n");
		QTest::newRow("future version")
			<< QString("version=99\nproject=/a.mmp\nsavedUTC=2026-09-11T12:00:00Z\n");
		QTest::newRow("no version") << QString("project=/a.mmp\nsavedUTC=2026-09-11T12:00:00Z\n");
		QTest::newRow("unparseable time") << QString("version=1\nproject=/a.mmp\nsavedUTC=soon\n");
		QTest::newRow("no time") << QString("version=1\nproject=/a.mmp\n");
	}

	void anUnreadableIdentityIsTreatedAsAbsent()
	{
		QFETCH(QString, text);
		QString project;
		QDateTime saved;
		QVERIFY(!parseRecoveryIdentity(text, project, saved));
	}

	// ------------------------------------------------------------ real files --

	void readingRealFilesFindsTheSidecarAndTheProject()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString recoveryFile = dir.filePath("recover.mmp");
		const QString project = dir.filePath("Track.mmp");

		QVERIFY(writeWithTime(project, "<lmms/>", at(11, 11)));
		QVERIFY(writeWithTime(recoveryFile, "<lmms/>", at(11, 12)));
		QVERIFY(writeRecoveryIdentity(recoveryFile, project, at(11, 12)));

		const RecoveryInfo info = readRecoveryInfo(recoveryFile);
		QVERIFY(info.fileExists);
		QVERIFY(info.fileSize > 0);
		QVERIFY(info.hasIdentity);
		QCOMPARE(info.sourceProject, project);
		QVERIFY(info.sourceProjectExists);
		QCOMPARE(info.sourceProjectModified.toSecsSinceEpoch(), at(11, 11).toSecsSinceEpoch());

		const RecoveryDecision d = decideRecovery(info, project);
		QVERIFY2(d.offered(), qPrintable(why(d)));
	}

	void readingRealFilesRefusesAStaleRecovery()
	{
		// The same setup as above with the write order reversed: the project was
		// saved after the autosave, so there is nothing to recover. This is the
		// negative control driven through the real file system rather than
		// through hand-built inputs.
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString recoveryFile = dir.filePath("recover.mmp");
		const QString project = dir.filePath("Track.mmp");

		QVERIFY(writeWithTime(recoveryFile, "<lmms/>", at(11, 10)));
		QVERIFY(writeWithTime(project, "<lmms/>", at(11, 11)));
		QVERIFY(writeRecoveryIdentity(recoveryFile, project, at(11, 10)));

		const RecoveryDecision d = decideRecovery(readRecoveryInfo(recoveryFile), project);
		QCOMPARE(QString::fromLatin1(recoveryVerdictName(d.verdict)), QString("Stale"));
		QVERIFY(!d.offered());
	}

	void readingRealFilesRefusesARecoveryOfAnotherProject()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString recoveryFile = dir.filePath("recover.mmp");
		const QString other = dir.filePath("Other.mmp");
		const QString mine = dir.filePath("Mine.mmp");

		QVERIFY(writeWithTime(other, "<lmms/>", at(11, 11)));
		QVERIFY(writeWithTime(mine, "<lmms/>", at(11, 9)));
		QVERIFY(writeWithTime(recoveryFile, "<lmms/>", at(11, 12)));
		QVERIFY(writeRecoveryIdentity(recoveryFile, other, at(11, 12)));

		const RecoveryDecision d = decideRecovery(readRecoveryInfo(recoveryFile), mine);
		QCOMPARE(QString::fromLatin1(recoveryVerdictName(d.verdict)), QString("OtherProject"));
		QVERIFY(!d.offered());
	}

	void aRecoveryFileWithNoSidecarIsReadAndOffered()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString recoveryFile = dir.filePath("recover.mmp");
		QVERIFY(writeWithTime(recoveryFile, "<lmms/>", at(11, 12)));

		const RecoveryInfo info = readRecoveryInfo(recoveryFile);
		QVERIFY(info.fileExists);
		QVERIFY(!info.hasIdentity);
		QVERIFY2(decideRecovery(info, "/home/u/songs/Track.mmp").offered(), "legacy recovery refused");
	}

	void removingTheRecoveryRemovesItsSidecarToo()
	{
		// A disk write to prove the cleanup path: leaving the sidecar behind would
		// let the NEXT launch decide about a recovery file that no longer exists.
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString recoveryFile = dir.filePath("recover.mmp");
		QVERIFY(writeWithTime(recoveryFile, "<lmms/>", at(11, 12)));
		QVERIFY(writeRecoveryIdentity(recoveryFile, dir.filePath("Track.mmp"), at(11, 12)));
		QVERIFY(QFile::exists(recoveryFile));
		QVERIFY(QFile::exists(recoveryInfoPath(recoveryFile)));

		QVERIFY(removeRecovery(recoveryFile));
		QVERIFY(!QFile::exists(recoveryFile));
		QVERIFY(!QFile::exists(recoveryInfoPath(recoveryFile)));
		QVERIFY(!QFile::exists(recoveryFile + ".info.tmp"));
	}

	void readingAnAbsentRecoveryFileSaysSo()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const RecoveryInfo info = readRecoveryInfo(dir.filePath("recover.mmp"));
		QVERIFY(!info.fileExists);
		QVERIFY(!decideRecovery(info, "").offered());
	}
};

QTEST_GUILESS_MAIN(ProjectRecoveryTest)
#include "ProjectRecoveryTest.moc"
