/*
 * VideoTestClipPortabilityTest.cpp - the test-clip fixture is usable in a build without FFmpeg
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
 *
 */
/*! Cluster ci-6422986f4db9676f: the hosted linux-arm64 job configures without FFmpeg (Ubuntu's arm64
 *  image has no libav*-dev), and there VideoClipsTest.cpp and VideoClipsGuiTest.cpp still call
 *  videotest::makeClip. A helper declared only under ZENE_HAVE_FFMPEG makes those always-built test
 *  sources fail to compile, which no runtime availability check can prevent.
 *
 *  The seam below selects the fixture's no-FFmpeg branch in any build, so this regression exercises
 *  that branch deterministically: it runs on the FFmpeg-enabled developer and CI trees too, instead
 *  of depending on which optional packages a machine happens to have. The case is the ordinary
 *  no-FFmpeg path for every always-built video test: the helper exists, declines cleanly, and leaves
 *  no file behind, so the caller's own availability check turns the case into a skip. */

#include <QtTest>

#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#define ZENE_TEST_VIDEO_CLIP_WITHOUT_FFMPEG
#include "VideoTestClip.h"

class VideoTestClipPortabilityTest : public QObject
{
	Q_OBJECT

private slots:
	//! The no-FFmpeg branch declares makeClip and refuses the work without touching the filesystem.
	void makeClipIsDeclaredWithoutFfmpegAndRefuses()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("clip.mkv"));
		QVERIFY(!videotest::makeClip(path));
		QVERIFY(!QFileInfo::exists(path));

		// The clip's shape and palette stay known in every build (VideoTrackTest compares against
		// them even when it skips the decode).
		QCOMPARE(videotest::Width, 160);
		QCOMPARE(videotest::Height, 90);
		QCOMPARE(videotest::Fps, 25);
		QCOMPARE(videotest::Frames, 50);
		QCOMPARE(videotest::Rate, 44100);
		QCOMPARE(videotest::AudioChunk, 1024);
		QCOMPARE(static_cast<int>(videotest::Palette::RedBlue), 0);
		QCOMPARE(static_cast<int>(videotest::Palette::GreenYellow), 1);
	}

	//! Both call-site spellings build: the default palette and the explicit second palette.
	void makeClipAcceptsBothPalettesAndAnEmptyPath()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		QVERIFY(!videotest::makeClip(QString()));
		QVERIFY(!videotest::makeClip(dir.filePath(QStringLiteral("missing/clip.mkv")), videotest::Palette::RedBlue));
		QVERIFY(!videotest::makeClip(dir.filePath(QStringLiteral("missing/second.mkv")), videotest::Palette::GreenYellow));
	}
};

QTEST_MAIN(VideoTestClipPortabilityTest)
#include "VideoTestClipPortabilityTest.moc"
