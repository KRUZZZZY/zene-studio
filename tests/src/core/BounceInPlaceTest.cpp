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

#ifdef _WIN32
#include <filesystem>
#include <system_error>
#else
#include <unistd.h>
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>
#include <limits>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "BounceInPlace.h"
#include "BrowserTestSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "TempoMap.h"
#include "Track.h"

using namespace lmms;
using namespace browserfixture;

namespace
{
constexpr qint64 kFixtureFrames = 2048;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The stand-in for `zene render <project> -o <out> ...`.
int fakeRender(int argc, char** argv)
{
	QString out;
	for (int i = 1; i + 1 < argc; ++i)
	{
		if (std::strcmp(argv[i], "-o") == 0) { out = QString::fromLocal8Bit(argv[i + 1]); }
	}
	if (qEnvironmentVariableIsSet("ZENE_FAKE_RENDER_PARTIAL"))
	{
		QFile partial(out);
		if (partial.open(QIODevice::WriteOnly | QIODevice::Truncate)) { partial.write("partial render"); }
		return 3;
	}
	return writeWave(out, WaveSpec()) ? 0 : 4;
}

//! An instrument track holding one clip (a bounce of a track with no clip is refused).
Track* trackWithClip(const QString& name)
{
	Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
	track->setName(name);
	track->createClip(TimePos(0));
	return track;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

int trackCount() { return static_cast<int>(Engine::getSong()->tracks().size()); }
}

class BounceInPlaceTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;

	QString out(const QString& name) const { return QDir(m_dir.path()).filePath(name); }

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		qunsetenv("ZENE_FAKE_RENDER_PARTIAL");
		Engine::getSong()->clearProject();
	}

	//! A render that dies after writing part of its output leaves an existing destination untouched.
	void failedBounceKeepsAnExistingDestination()
	{
		Track* source = trackWithClip(QStringLiteral("Lead"));
		const QString id = control::trackIdOf(source);
		const QString wav = out(QStringLiteral("existing.wav"));
		QVERIFY(writeFile(wav, "previous output"));
		const int before = trackCount();

		qputenv("ZENE_FAKE_RENDER_PARTIAL", "1");
		const ControlResult landed = run(QStringLiteral("bounce.land"), {{QStringLiteral("track"), id}, {QStringLiteral("out"), wav}});
		qunsetenv("ZENE_FAKE_RENDER_PARTIAL");

		QCOMPARE(landed.errorKind, ControlErrorKind::Refused);
		QVERIFY2(landed.errorMessage.contains(QStringLiteral("render failed")), qPrintable(landed.errorMessage));
		QCOMPARE(trackCount(), before);
		QCOMPARE(readFile(wav), QByteArray("previous output"));
		QCOMPARE(QDir(QFileInfo(wav).absolutePath()).entryList({QStringLiteral("existing.wav.part.*")}, QDir::Files), QStringList());
	}

	//! The positive control: a successful bounce replaces the existing destination and leaves no staging debris.
	void successfulBounceReplacesAnExistingDestination()
	{
		Track* source = trackWithClip(QStringLiteral("Lead"));
		const QString id = control::trackIdOf(source);
		const QString wav = out(QStringLiteral("replace.wav"));
		QVERIFY(writeFile(wav, "previous output"));

		const ControlResult landed = run(QStringLiteral("bounce.land"), {{QStringLiteral("track"), id}, {QStringLiteral("out"), wav}});
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QVERIFY(BounceInPlace::wavFrameCount(wav) > 0);
		QVERIFY(readFile(wav) != QByteArray("previous output"));
		QCOMPARE(QDir(QFileInfo(wav).absolutePath()).entryList({QStringLiteral("replace.wav.part.*")}, QDir::Files), QStringList());
	}

	void outputPathAbsoluteSyntaxFollowsThePlatform()
	{
#ifdef Q_OS_WIN
		QVERIFY(BounceInPlace::isAbsoluteOutputPath(QStringLiteral("C:/Bounces/track.wav")));
		QVERIFY(BounceInPlace::isAbsoluteOutputPath(QStringLiteral("\\\\server\\share\\track.wav")));
#else
		QVERIFY(BounceInPlace::isAbsoluteOutputPath(QStringLiteral("/tmp/track.wav")));
		QVERIFY(!BounceInPlace::isAbsoluteOutputPath(QStringLiteral("relative/track.wav")));
#endif
	}

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
#ifdef _WIN32
		std::error_code ec;
		std::filesystem::create_hard_link(source.toLocal8Bit().constData(), link.toLocal8Bit().constData(), ec);
		const bool linked = !ec;
#else
		// link(2): std::filesystem is unavailable below the macOS 10.15 deployment target of the hosted macos-x86_64 job.
		const bool linked = ::link(QFile::encodeName(source).constData(), QFile::encodeName(link).constData()) == 0;
#endif
		if (!linked) { QSKIP("this filesystem cannot make a hard link, so the case cannot be exercised here"); }

		qint64 written = -1;
		QString error;
		QVERIFY2(!BounceInPlace::trimWav(source, link, 0, 1024, &written, &error),
			"trimming onto a hard link of the source reported success");
		QVERIFY2(!error.isEmpty(), "the refusal carried no reason");
		QCOMPARE(written, qint64(0));
		QCOMPARE(BounceInPlace::sha256OfFile(source), before);
		QCOMPARE(BounceInPlace::wavFrameCount(source), kFixtureFrames);
	}

	/*! The tick span from INT32_MIN to INT32_MAX is 2^32 - 1, which a tick_t cannot hold: the subtraction
	 *  used to happen in tick_t before the frame conversion, so it wrapped to -1 tick (undefined behaviour
	 *  in the signed form). The span must come out as its full frame count, and the reverse span as the
	 *  negated count. Ordinary ticks are unchanged, bit for bit.
	 */
	void framesBetweenTicksSpansTheWholeTickRange()
	{
		constexpr float framesPerTick = 100.0f;
		const TempoMap inactive;
		constexpr tick_t lowest = std::numeric_limits<tick_t>::min();
		constexpr tick_t highest = std::numeric_limits<tick_t>::max();
		constexpr double span = 4294967295.0;  // highest - lowest, exactly
		const double expected = span * framesPerTick;

		const double forward = BounceInPlace::framesBetweenTicks(lowest, highest, framesPerTick, inactive, 120, 44100);
		QVERIFY2(forward > 0.0, qPrintable(QString::number(forward)));
		QVERIFY2(std::abs(forward - expected) <= expected * 1e-6, qPrintable(QString::number(forward)));

		const double backward = BounceInPlace::framesBetweenTicks(highest, lowest, framesPerTick, inactive, 120, 44100);
		QVERIFY2(backward < 0.0, qPrintable(QString::number(backward)));
		QVERIFY2(std::abs(backward + expected) <= expected * 1e-6, qPrintable(QString::number(backward)));

		QCOMPARE(BounceInPlace::framesBetweenTicks(24, 120, framesPerTick, inactive, 120, 44100), 96.0 * framesPerTick);
	}
};

int main(int argc, char** argv)
{
	if (argc > 1 && std::strcmp(argv[1], "render") == 0) { return fakeRender(argc, argv); }
	QCoreApplication app(argc, argv);
	QTEST_SET_MAIN_SOURCE_PATH
	BounceInPlaceTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "BounceInPlaceTest.moc"
