/*
 * VideoAudioNameTest.cpp - the default WAV an extraction writes never names a file that is already there
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

/*! cluster new-p9-66 (video.extract_audio, BUG 8): firstFreeAudioPath is the name the default WAV gets. The last
 *  candidate used to be returned even when a file already held it, so an extraction overwrote that WAV. Each case
 *  below creates its taken names as empty files in a temporary folder - no FFmpeg, no clock, no engine.
 *
 *  cluster new-p9-67 (BUG 9): the WAV only appears when its decode commits, so a file existing was not enough - two
 *  extractions asked for before either had written its WAV were given the same path. A name is therefore claimed
 *  when it is handed out, and the claim ends with the extraction that took it (extractVideoAudio). No thread, no
 *  clock: the cases below claim and release in order. */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "VideoAudioExtract.h"

using lmms::extractVideoAudio;
using lmms::firstFreeAudioPath;
using lmms::VideoAudio;

namespace
{

//! An empty file called @a name in @a dir: a taken default name.
void take(const QDir& dir, const QString& name)
{
	QFile file(dir.filePath(name));
	QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
}

} // namespace

class VideoAudioNameTest : public QObject
{
	Q_OBJECT

private slots:
	void noNameTakenGivesTheStem()
	{
		QTemporaryDir tmp;
		QVERIFY(tmp.isValid());
		const QDir dir(tmp.path());
		QCOMPARE(firstFreeAudioPath(dir, QStringLiteral("clip-audio"), 3), dir.filePath(QStringLiteral("clip-audio.wav")));
	}

	void takenNamesAreSkipped()
	{
		QTemporaryDir tmp;
		QVERIFY(tmp.isValid());
		const QDir dir(tmp.path());
		take(dir, QStringLiteral("clip-audio.wav"));
		take(dir, QStringLiteral("clip-audio-2.wav"));
		QCOMPARE(firstFreeAudioPath(dir, QStringLiteral("clip-audio"), 100), dir.filePath(QStringLiteral("clip-audio-3.wav")));
	}

	//! The last name in range is a candidate too: with -2 taken and -3 free, -3 is the answer, not the taken -2.
	void lastNameInRangeIsTried()
	{
		QTemporaryDir tmp;
		QVERIFY(tmp.isValid());
		const QDir dir(tmp.path());
		take(dir, QStringLiteral("clip-audio.wav"));
		take(dir, QStringLiteral("clip-audio-2.wav"));
		QCOMPARE(firstFreeAudioPath(dir, QStringLiteral("clip-audio"), 3), dir.filePath(QStringLiteral("clip-audio-3.wav")));
	}

	//! Every name in range taken: no name is returned, so the caller refuses instead of overwriting -3.wav.
	void everyNameTakenGivesNoName()
	{
		QTemporaryDir tmp;
		QVERIFY(tmp.isValid());
		const QDir dir(tmp.path());
		take(dir, QStringLiteral("clip-audio.wav"));
		take(dir, QStringLiteral("clip-audio-2.wav"));
		take(dir, QStringLiteral("clip-audio-3.wav"));
		QVERIFY(firstFreeAudioPath(dir, QStringLiteral("clip-audio"), 3).isEmpty());
	}

	/*! BUG 9: the search runs when the request arrives, but the WAV appears only when its decode commits. Two
	 *  extractions started before either committed used to get <stem>.wav, and the second replaced the first. */
	void twoSearchesBeforeAWavExistsGetDifferentNames()
	{
		QTemporaryDir tmp;
		QVERIFY(tmp.isValid());
		const QDir dir(tmp.path());
		const QString first = firstFreeAudioPath(dir, QStringLiteral("clip-audio"), 100);
		const QString second = firstFreeAudioPath(dir, QStringLiteral("clip-audio"), 100);
		QVERIFY2(!QFileInfo::exists(first), "the case needs the first WAV to be unwritten");
		QCOMPARE(first, dir.filePath(QStringLiteral("clip-audio.wav")));
		QCOMPARE(second, dir.filePath(QStringLiteral("clip-audio-2.wav")));
	}

	//! A name an extraction in flight holds is as taken as one on disk: no name again, not one already speaking.
	void everyNameClaimedGivesNoName()
	{
		QTemporaryDir tmp;
		QVERIFY(tmp.isValid());
		const QDir dir(tmp.path());
		const QString first = firstFreeAudioPath(dir, QStringLiteral("clip-audio"), 3);
		const QString second = firstFreeAudioPath(dir, QStringLiteral("clip-audio"), 3);
		const QString third = firstFreeAudioPath(dir, QStringLiteral("clip-audio"), 3);
		QCOMPARE(first, dir.filePath(QStringLiteral("clip-audio.wav")));
		QCOMPARE(second, dir.filePath(QStringLiteral("clip-audio-2.wav")));
		QCOMPARE(third, dir.filePath(QStringLiteral("clip-audio-3.wav")));
		QVERIFY(firstFreeAudioPath(dir, QStringLiteral("clip-audio"), 3).isEmpty());
	}

	//! The claim ends with its extraction: one that wrote nothing (here: no such video) gives its name back.
	void aFailedExtractionGivesItsNameBack()
	{
		QTemporaryDir tmp;
		QVERIFY(tmp.isValid());
		const QDir dir(tmp.path());
		const QString claimed = firstFreeAudioPath(dir, QStringLiteral("clip-audio"), 100);
		QCOMPARE(claimed, dir.filePath(QStringLiteral("clip-audio.wav")));
		VideoAudio audio;
		QString error;
		QVERIFY2(!extractVideoAudio(dir.filePath(QStringLiteral("no-such-video.mkv")), claimed, &audio, &error),
			"a missing video was reported as a successful extraction");
		QVERIFY(!error.isEmpty());
		QVERIFY2(!QFileInfo::exists(claimed), "the failed extraction left a WAV");
		QCOMPARE(firstFreeAudioPath(dir, QStringLiteral("clip-audio"), 100), claimed);
	}

	//! A claim is not a file: a name a file holds stays taken after the extraction that claimed it has ended.
	void aNameItsExtractionWroteStaysTaken()
	{
		QTemporaryDir tmp;
		QVERIFY(tmp.isValid());
		const QDir dir(tmp.path());
		const QString claimed = firstFreeAudioPath(dir, QStringLiteral("clip-audio"), 100);
		take(dir, QStringLiteral("clip-audio.wav"));
		VideoAudio audio;
		QString error;
		QVERIFY(!extractVideoAudio(dir.filePath(QStringLiteral("no-such-video.mkv")), claimed, &audio, &error));
		QCOMPARE(firstFreeAudioPath(dir, QStringLiteral("clip-audio"), 100), dir.filePath(QStringLiteral("clip-audio-2.wav")));
	}

	//! The range the control command uses (100): the stem and -2 to -100 all taken leaves nothing free.
	void defaultRangeExhaustedGivesNoName()
	{
		QTemporaryDir tmp;
		QVERIFY(tmp.isValid());
		const QDir dir(tmp.path());
		take(dir, QStringLiteral("clip-audio.wav"));
		for (int n = 2; n <= 100; ++n) { take(dir, QStringLiteral("clip-audio-%1.wav").arg(n)); }
		QVERIFY(firstFreeAudioPath(dir, QStringLiteral("clip-audio"), 100).isEmpty());
	}
};

QTEST_GUILESS_MAIN(VideoAudioNameTest)
#include "VideoAudioNameTest.moc"
