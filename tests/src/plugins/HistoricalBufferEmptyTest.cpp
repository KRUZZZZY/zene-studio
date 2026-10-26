// Copyright (c) 2026 Zene Studio contributors
// SPDX-License-Identifier: GPL-2.0-or-later

//! Cluster new-p21-188: verify that construction and resize at capacity zero are
//! safe and the historical average is zero.

#include <QtTest>

#include "HistoricalBuffer.h"

class HistoricalBufferEmptyTest : public QObject
{
	Q_OBJECT

private slots:
	void emptyAverageIsZero()
	{
		sfz::HistoricalBuffer<float> constructedEmpty{ 0 };
		QCOMPARE(constructedEmpty.getAverage(), 0.0f);
		constructedEmpty.push(1.0f);
		QCOMPARE(constructedEmpty.getAverage(), 0.0f);

		sfz::HistoricalBuffer<float> resizedEmpty{ 1 };
		resizedEmpty.push(1.0f);
		resizedEmpty.resize(0);
		QCOMPARE(resizedEmpty.getAverage(), 0.0f);
		resizedEmpty.push(2.0f);
		QCOMPARE(resizedEmpty.getAverage(), 0.0f);
	}
};

QTEST_APPLESS_MAIN(HistoricalBufferEmptyTest)

#include "HistoricalBufferEmptyTest.moc"
