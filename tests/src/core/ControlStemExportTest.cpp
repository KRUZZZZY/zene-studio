/*
 * ControlStemExportTest.cpp - the render.stems verb's own regressions:
 *                               what a failed export SAYS, what it REPORTS as
 *                               written, and what it leaves on disk.
 *
 * The verb renders through a CHILD PROCESS - the shipped `exportstems` CLI
 * path (src/core/main.cpp:542) - because an in-process render would drive
 * this instance's audio device and leave it unable to quit (the rejection is
 * commented at src/core/ControlCommandsProject.cpp:327-333). A test binary is
 * not the product, so its main() answers that command line itself
 * (fakeStemExport): it writes the files the environment names, stamps them
 * with the modification time the environment names, writes the environment's
 * error text to standard error and exits with the environment's code.
 * Everything around the child is the PRODUCT's own code and runs for real -
 * the session serialisation, the child process, the listing of the output
 * directory and the reply - so each test here is a statement about the verb
 * and not about the fake.
 *
 * THE ENVIRONMENT IS THE SEAM, and it is injected, never waited for:
 *
 *   ZENE_FAKE_STEM_FILES    "name=content,name=content" - the files the child
 *                           writes into the directory it was given.
 *   ZENE_FAKE_STEM_MTIME_MS the modification time the child stamps those
 *                           files with, in milliseconds since the epoch. This
 *                           is how a filesystem whose clock does not move
 *                           between two writes is reproduced WITHOUT a race:
 *                           the stamp is a value both sides agree on.
 *   ZENE_FAKE_STEM_STDERR   the text the child writes to standard error.
 *   ZENE_FAKE_STEM_EXIT     the code the child exits with.
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

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>
#include <cstring>

#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"

using namespace lmms;

namespace
{

//! The directory the command line asks the child to write into.
QString outputDirectoryOf(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i)
	{
		const QString argument = QString::fromLocal8Bit(argv[i]);
		const bool namesTheOutput = argument == QStringLiteral("-o")
			|| argument == QStringLiteral("--output");
		if (namesTheOutput && i + 1 < argc) { return QString::fromLocal8Bit(argv[i + 1]); }
	}
	return QString();
}

//! Writes the "name=content" pairs into \a directory, stamping each file
//! with \a stamp when one is given.
bool writeStemFiles(const QString& directory, const QStringList& pairs, qint64 stamp)
{
	const QDateTime when = QDateTime::fromMSecsSinceEpoch(stamp);
	for (const QString& pair : pairs)
	{
		const int split = pair.indexOf(QLatin1Char('='));
		if (split <= 0) { continue; }
		QFile file(QDir(directory).filePath(pair.left(split)));
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
		file.write(pair.mid(split + 1).toUtf8());
		file.flush();
		// On an OPEN file: setFileTime() acts on the descriptor, so a closed
		// one may not carry the stamp (the same note as
		// tests/src/core/ProjectRecoveryTest.cpp:73).
		if (stamp > 0) { file.setFileTime(when, QFileDevice::FileModificationTime); }
		file.close();
	}
	return true;
}

//! The stand-in for `zene exportstems <project> -o <dir> ...`.
int fakeStemExport(int argc, char** argv)
{
	const QString out = outputDirectoryOf(argc, argv);
	if (out.isEmpty()) { return 2; }
	// The shipped CLI creates the directory it was given (src/core/main.cpp:1476).
	if (!QDir().mkpath(out)) { return 3; }
	const qint64 stamp = qEnvironmentVariable("ZENE_FAKE_STEM_MTIME_MS").toLongLong();
	const QStringList pairs = qEnvironmentVariable("ZENE_FAKE_STEM_FILES")
		.split(QLatin1Char(','), Qt::SkipEmptyParts);
	if (!writeStemFiles(out, pairs, stamp)) { return 3; }

	const QString error = qEnvironmentVariable("ZENE_FAKE_STEM_STDERR");
	if (!error.isEmpty())
	{
		std::fputs(qPrintable(error), stderr);
		std::fflush(stderr);
	}
	return qEnvironmentVariableIntValue("ZENE_FAKE_STEM_EXIT");
}

//! The file's content, or an empty string when it cannot be read.
QString contentOf(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	return QString::fromUtf8(file.readAll());
}

//! Writes \a content into \a path, as a stem file of an earlier export.
bool writeFile(const QString& path, const QString& content)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	return file.write(content.toUtf8()) == content.toUtf8().size();
}

} // namespace

class ControlStemExportTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;

	QString out(const QString& name) const { return QDir(m_dir.path()).filePath(name); }

	//! A session with one instrument track: the verb refuses an empty session.
	void seedSession()
	{
		Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
		track->setName(QStringLiteral("Lead"));
		track->createClip(TimePos(0));
	}

	ControlResult exportStems(const QString& directory)
	{
		return ControlRegistry::instance()->invoke(QStringLiteral("render.stems"),
			{{QStringLiteral("out"), directory}, {QStringLiteral("format"), QStringLiteral("wav")}});
	}

	//! The names the reply reports as written.
	QStringList reportedStems(const ControlResult& result)
	{
		QStringList names;
		for (const QJsonValue& value : result.result.value(QStringLiteral("stems")).toArray())
		{
			names.append(value.toString());
		}
		return names;
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		qunsetenv("ZENE_FAKE_STEM_FILES");
		qunsetenv("ZENE_FAKE_STEM_MTIME_MS");
		qunsetenv("ZENE_FAKE_STEM_STDERR");
		qunsetenv("ZENE_FAKE_STEM_EXIT");
		Engine::getSong()->clearProject();
		// A fresh output directory for every test: the verb writes into a
		// directory the CALLER names, so no test may see another's stems.
		QDir(m_dir.path()).removeRecursively();
		QVERIFY(QDir().mkpath(m_dir.path()));
	}

	//! The happy path: the stems the child wrote are the stems the reply names.
	void aSuccessfulExportNamesTheStemsItWrote()
	{
		seedSession();
		qputenv("ZENE_FAKE_STEM_FILES", "01_Lead.wav=STEM,02_Pad.wav=PAD");

		const QString directory = out(QStringLiteral("stems"));
		const ControlResult exported = exportStems(directory);
		QVERIFY2(exported.ok, qPrintable(exported.errorMessage));
		QCOMPARE(reportedStems(exported), QStringList({QStringLiteral("01_Lead.wav"),
			QStringLiteral("02_Pad.wav")}));
		QCOMPARE(exported.result.value(QStringLiteral("count")).toInt(), 2);
		QCOMPARE(contentOf(QDir(directory).filePath(QStringLiteral("01_Lead.wav"))),
			QStringLiteral("STEM"));
	}

	//! Cluster 3953: a failed export must say WHY it failed.
	void aFailedExportReportsTheChildsOwnError()
	{
		seedSession();
		qputenv("ZENE_FAKE_STEM_FILES", "01_Lead.wav=STEM");
		qputenv("ZENE_FAKE_STEM_STDERR", "fake-child: could not open the output directory");
		qputenv("ZENE_FAKE_STEM_EXIT", "3");

		const ControlResult failed = exportStems(out(QStringLiteral("stems")));
		QVERIFY(!failed.ok);
		QCOMPARE(failed.errorKind, ControlErrorKind::Refused);
		QVERIFY2(failed.errorMessage.contains(QStringLiteral("fake-child: could not open the output directory")),
			qPrintable(failed.errorMessage));
	}

	//! Cluster 4875: a rewrite is reported even when the clock did not move.
	void aRewriteIsReportedEvenWhenTheStampDoesNotMove()
	{
		seedSession();
		const QString directory = out(QStringLiteral("stems"));
		QVERIFY(QDir().mkpath(directory));
		// An earlier export's stem. The rewrite below lands on the SAME
		// modification time, the way a filesystem whose clock does not move
		// between two writes makes it land - injected, never waited for.
		const QString earlier = QDir(directory).filePath(QStringLiteral("01_Lead.wav"));
		QVERIFY(writeFile(earlier, QStringLiteral("OLD")));
		const QDateTime stamp = QFileInfo(earlier).lastModified();
		qputenv("ZENE_FAKE_STEM_MTIME_MS", QByteArray::number(stamp.toMSecsSinceEpoch()));
		qputenv("ZENE_FAKE_STEM_FILES", "01_Lead.wav=NEW,02_Pad.wav=PAD");

		const ControlResult exported = exportStems(directory);
		QVERIFY2(exported.ok, qPrintable(exported.errorMessage));
		const QStringList stems = reportedStems(exported);
		QVERIFY2(stems.contains(QStringLiteral("01_Lead.wav")),
			qPrintable(QStringLiteral("reported %1").arg(stems.join(QLatin1Char(',')))));
		QVERIFY2(stems.contains(QStringLiteral("02_Pad.wav")),
			qPrintable(QStringLiteral("reported %1").arg(stems.join(QLatin1Char(',')))));
		// The rewritten stem holds the NEW content, not the earlier one.
		QCOMPARE(contentOf(earlier), QStringLiteral("NEW"));
	}

	//! Cluster 1234: a failed render leaves the caller's earlier stems alone.
	void aFailedRenderLeavesTheOutputDirectoryAlone()
	{
		seedSession();
		const QString directory = out(QStringLiteral("stems"));
		QVERIFY(QDir().mkpath(directory));
		const QString earlier = QDir(directory).filePath(QStringLiteral("01_Lead.wav"));
		QVERIFY(writeFile(earlier, QStringLiteral("OLD")));
		// The child writes a stem and then fails: half an export.
		qputenv("ZENE_FAKE_STEM_FILES", "01_Lead.wav=PARTIAL");
		qputenv("ZENE_FAKE_STEM_EXIT", "3");

		const ControlResult failed = exportStems(directory);
		QVERIFY(!failed.ok);
		QCOMPARE(failed.errorKind, ControlErrorKind::Refused);
		// The earlier stem is untouched, and nothing else is left behind.
		QCOMPARE(contentOf(earlier), QStringLiteral("OLD"));
		QCOMPARE(QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
			QStringList({QStringLiteral("01_Lead.wav")}));
	}
};

int main(int argc, char** argv)
{
	if (argc > 1 && std::strcmp(argv[1], "exportstems") == 0) { return fakeStemExport(argc, argv); }
	QCoreApplication app(argc, argv);
	QTEST_SET_MAIN_SOURCE_PATH
	ControlStemExportTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "ControlStemExportTest.moc"
