// Copyright (c) 2026 Zene Studio contributors
// SPDX-License-Identifier: GPL-2.0-or-later

//! Cluster new-p21-187: verify sfizz's HistoricalBuffer resize capacity through
//! its public push and average operations.

#include <QtTest>

#include "HistoricalBuffer.h"

class HistoricalBufferResizeTest : public QObject
{
	Q_OBJECT

private slots:
	void pushAndCircularOverwriteUseTheConfiguredCapacity()
	{
		sfz::HistoricalBuffer<float> buffer{ 3 };
		buffer.push(1.0f);
		buffer.push(2.0f);
		buffer.push(3.0f);
		QCOMPARE(buffer.getAverage(), 2.0f);

		buffer.push(4.0f);
		QCOMPARE(buffer.getAverage(), 3.0f);
	}

	void growingUpdatesTheCapacityUsedByPush()
	{
		sfz::HistoricalBuffer<float> buffer{ 2 };
		buffer.resize(4);
		buffer.push(2.0f);
		buffer.push(4.0f);
		buffer.push(6.0f);
		QCOMPARE(buffer.getAverage(), 3.0f);

		buffer.push(8.0f);
		QCOMPARE(buffer.getAverage(), 5.0f);
		buffer.push(10.0f);
		QCOMPARE(buffer.getAverage(), 7.0f);
	}

	void shrinkingUpdatesTheCapacityUsedByPush()
	{
		sfz::HistoricalBuffer<float> buffer{ 4 };
		buffer.resize(2);
		buffer.push(10.0f);
		buffer.push(20.0f);
		buffer.push(30.0f);
		QCOMPARE(buffer.getAverage(), 25.0f);
	}
};

QTEST_APPLESS_MAIN(HistoricalBufferResizeTest)

#include "HistoricalBufferResizeTest.moc"
