/*
 * StemQualityTest.cpp - the stem-separation quality table and hop formula (Q050-062.1)
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

#include "StemSeparation/StemTypes.h"

using namespace lmms;

class StemQualityTest : public QObject
{
	Q_OBJECT
private slots:
	//! Standard is the legacy segment / 2 for every segment (odd ones too), the
	//! others overlap 25% / 75%, and no hop is ever 0.
	void testHopFormula()
	{
		for (const int segment : {2, 3, 7, 1024, 16384, HTDemucsSegmentFrames, 44100 * 600})
		{
			QCOMPARE(stemHopFrames(segment, StemQuality::Standard), segment / 2);
			QVERIFY(stemHopFrames(segment, StemQuality::High) >= 1);
			QVERIFY(stemHopFrames(segment, StemQuality::High) <= stemHopFrames(segment, StemQuality::Standard));
			QVERIFY(stemHopFrames(segment, StemQuality::Fast) >= stemHopFrames(segment, StemQuality::Standard));
		}
		QCOMPARE(stemHopFrames(16384, StemQuality::Fast), 12288);
		QCOMPARE(stemHopFrames(16384, StemQuality::High), 4096);
		QCOMPARE(stemHopFrames(HTDemucsSegmentFrames, StemQuality::Fast), 257985);
		QCOMPARE(stemHopFrames(HTDemucsSegmentFrames, StemQuality::High), 85995);
	}

	//! The names the surface and the CLI share, the overlap each reports, and
	//! the default a caller that does not choose gets.
	void testNamesOverlapAndDefault()
	{
		QCOMPARE(QString::fromLatin1(stemQualityName(StemQuality::Fast)), QStringLiteral("fast"));
		QCOMPARE(QString::fromLatin1(stemQualityName(StemQuality::Standard)), QStringLiteral("standard"));
		QCOMPARE(QString::fromLatin1(stemQualityName(StemQuality::High)), QStringLiteral("high"));
		QCOMPARE(stemOverlapFraction(StemQuality::Fast), 0.25);
		QCOMPARE(stemOverlapFraction(StemQuality::Standard), 0.5);
		QCOMPARE(stemOverlapFraction(StemQuality::High), 0.75);
		QCOMPARE(static_cast<int>(DefaultStemQuality), static_cast<int>(StemQuality::Standard));
	}
};

QTEST_GUILESS_MAIN(StemQualityTest)
#include "StemQualityTest.moc"
