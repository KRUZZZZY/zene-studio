/*
 * BounceInPlaceTest.cpp - the trim half of a bounce, which is the half that can
 *                          be exercised without a render.
 *
 * BounceInPlace::renderTracks runs the product's own CLI render as a CHILD
 * PROCESS, so it cannot be driven from a test binary; trimWav is the piece the
 * range half of a bounce is made of, and its header says it is public for
 * exactly this reason. The fixture WAV is the byte-written one the browser
 * tests use, so the file under test is one libsndfile's assertions in those
 * tests already validate.
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

#include <filesystem>
#include <system_error>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include "BounceInPlace.h"
#include "BrowserTestSupport.h"

using namespace lmms;
using namespace browserfixture;

namespace
{
constexpr qint64 kFixtureFrames = 2048;
}

class BounceInPlaceTest : public QObject
{
	Q_OBJECT
private slots:

	//! The trim itself: a range of the source, copied to another file, and the
	//! window clamped to what the source actually has. This is the positive
	//! control - a guard that refuses a self-trim must not refuse a real one.
	void trimWavCopiesTheRangeToAnotherFile()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString source = QDir(dir.path()).filePath(QStringLiteral("source.wav"));
		QVERIFY(writeWave(source, WaveSpec()));

		const QString target = QDir(dir.path()).filePath(QStringLiteral("range.wav"));
		qint64 written = -1;
		QString error;
		QVERIFY2(BounceInPlace::trimWav(source, target, 512, 1024, &written, &error),
			qPrintable(error));
		QVERIFY(error.isEmpty());
		QCOMPARE(written, qint64(1024));
		QCOMPARE(BounceInPlace::wavFrameCount(target), qint64(1024));
		// The source is untouched by a trim to somewhere else.
		QCOMPARE(BounceInPlace::wavFrameCount(source), kFixtureFrames);
	}

	/*! Trimming a file ONTO ITSELF: the source is opened for reading and the
	 *  target is then opened for writing, which truncates the very file still
	 *  being read. The call cannot recover the audio, so it has to refuse
	 *  instead - and it has to refuse WITHOUT touching the file, because the
	 *  caller asked for a copy and lost the original otherwise.
	 */
	void trimWavRefusesToWriteOntoItsOwnSource()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString source = QDir(dir.path()).filePath(QStringLiteral("source.wav"));
		QVERIFY(writeWave(source, WaveSpec()));
		const QString before = BounceInPlace::sha256OfFile(source);
		QVERIFY(!before.isEmpty());

		qint64 written = -1;
		QString error;
		QVERIFY2(!BounceInPlace::trimWav(source, source, 0, 1024, &written, &error),
			"trimming a WAV onto itself reported success");
		QVERIFY2(!error.isEmpty(), "the refusal carried no reason");
		QCOMPARE(written, qint64(0));
		// The source audio is still there: not truncated, not rewritten.
		QCOMPARE(BounceInPlace::wavFrameCount(source), kFixtureFrames);
		QCOMPARE(BounceInPlace::sha256OfFile(source), before);
	}

	/*! The same file reached by a DIFFERENT name is still the same file: a
	 *  relative path, a "./" spelling or a symlink resolve to the one file
	 *  libsndfile would truncate. Unix-only, because on Windows QFile::link
	 *  writes a .lnk shortcut, which is a different object.
	 */
	void trimWavRefusesASymlinkThatNamesTheSource()
	{
#ifndef Q_OS_UNIX
		QSKIP("symlinks are exercised on Unix only: QFile::link writes a .lnk shortcut on Windows");
#else
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString source = QDir(dir.path()).filePath(QStringLiteral("source.wav"));
		QVERIFY(writeWave(source, WaveSpec()));
		const QString before = BounceInPlace::sha256OfFile(source);

		const QString link = QDir(dir.path()).filePath(QStringLiteral("link.wav"));
		QFile::link(source, link);
		QVERIFY2(QFileInfo(link).isSymLink(), "the fixture symlink was not created");

		qint64 written = -1;
		QString error;
		QVERIFY2(!BounceInPlace::trimWav(source, link, 0, 1024, &written, &error),
			"trimming onto a symlink of the source reported success");
		QVERIFY(!error.isEmpty());
		QCOMPARE(BounceInPlace::sha256OfFile(source), before);
		QCOMPARE(BounceInPlace::wavFrameCount(source), kFixtureFrames);
#endif
	}

	/*! A HARD link is a second name for the same inode: its canonical path is its
	 *  own, so a path comparison alone lets the trim open it for writing and
	 *  truncate the audio still being read. std::filesystem::create_hard_link
	 *  works on Unix and NTFS alike; a filesystem that refuses it is skipped.
	 */
	void trimWavRefusesAHardLinkOfTheSource()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString source = QDir(dir.path()).filePath(QStringLiteral("source.wav"));
		QVERIFY(writeWave(source, WaveSpec()));
		const QString before = BounceInPlace::sha256OfFile(source);
		QVERIFY(!before.isEmpty());

		const QString link = QDir(dir.path()).filePath(QStringLiteral("hardlink.wav"));
		std::error_code ec;
		std::filesystem::create_hard_link(source.toLocal8Bit().constData(), link.toLocal8Bit().constData(), ec);
		if (ec) { QSKIP("this filesystem cannot make a hard link, so the case cannot be exercised here"); }

		qint64 written = -1;
		QString error;
		QVERIFY2(!BounceInPlace::trimWav(source, link, 0, 1024, &written, &error),
			"trimming onto a hard link of the source reported success");
		QVERIFY2(!error.isEmpty(), "the refusal carried no reason");
		QCOMPARE(written, qint64(0));
		QCOMPARE(BounceInPlace::sha256OfFile(source), before);
		QCOMPARE(BounceInPlace::wavFrameCount(source), kFixtureFrames);
	}
};

QTEST_GUILESS_MAIN(BounceInPlaceTest)
#include "BounceInPlaceTest.moc"