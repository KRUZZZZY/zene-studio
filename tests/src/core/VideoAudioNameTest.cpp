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
 *  below creates its taken names as empty files in a temporary folder - no FFmpeg, no clock, no engine. */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "VideoAudioExtract.h"

using lmms::firstFreeAudioPath;

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
