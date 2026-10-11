/*
 * StereoDelay.cpp - regression coverage for the delay ring's sample-rate changes.
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#define private public
#include "StereoDelay.h"
#undef private

#include <QtTest>

#include "SampleFrame.h"

class StereoDelayTest : public QObject
{
	Q_OBJECT

private slots:
	void sampleRateChangeRebuildsRingState()
	{
		lmms::StereoDelay delay{1, 48000};
		delay.setLength(24000.0f);

		lmms::SampleFrame frame{};
		delay.tick(frame);
		QCOMPARE(delay.m_writeIndex, 1);

		delay.setSampleRate(22050);
		QCOMPARE(delay.m_maxLength, 22050);
		QCOMPARE(delay.m_length, 11025.0f);
		QCOMPARE(delay.m_writeIndex, 0);

		delay.setLength(22051.0f);
		QCOMPARE(delay.m_length, 11025.0f);

		for (int sample = 0; sample < 22051; ++sample)
		{
			delay.tick(frame);
		}
		QCOMPARE(delay.m_writeIndex, 1);
	}
};

QTEST_GUILESS_MAIN(StereoDelayTest)

#include "StereoDelay.moc"
