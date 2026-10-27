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

#include <memory>
#include <new>

class StereoDelayTest : public QObject
{
	Q_OBJECT

private:
	// A ring clamped to one frame must still act as a one-frame delay line:
	// the first input comes back out on the next tick, and the write cursor
	// never leaves the ring. tick() divides by the ring length, so reaching
	// it with a zero-length ring is the crash this guards against.
	static void checkOneFrameDelay(lmms::StereoDelay& delay)
	{
		QCOMPARE(delay.m_maxLength, 1);

		lmms::SampleFrame first{0.5f, 0.25f};
		delay.tick(first);
		QCOMPARE(first[0], 0.0f);
		QCOMPARE(first[1], 0.0f);
		QCOMPARE(delay.m_writeIndex, 0);

		lmms::SampleFrame second{};
		delay.tick(second);
		QCOMPARE(second[0], 0.5f);
		QCOMPARE(second[1], 0.25f);
		QCOMPARE(delay.m_writeIndex, 0);
	}

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

	void zeroMaxTimeClampsToOneFrameRing()
	{
		lmms::StereoDelay delay{0, 48000};
		checkOneFrameDelay(delay);
	}

	void zeroSampleRateClampsToOneFrameRing()
	{
		lmms::StereoDelay delay{20, 0};
		checkOneFrameDelay(delay);
	}

	void negativeArgumentsClampToOneFrameRing()
	{
		// new SampleFrame[negative] throws bad_array_new_length (a bad_alloc);
		// a fixed constructor must not throw here at all.
		std::unique_ptr<lmms::StereoDelay> delay;
		try
		{
			delay = std::make_unique<lmms::StereoDelay>(-1, 48000);
		}
		catch (const std::bad_alloc&)
		{
		}
		QVERIFY(delay != nullptr);
		checkOneFrameDelay(*delay);
	}

	void zeroSampleRateReconfigurationKeepsRingUsable()
	{
		lmms::StereoDelay delay{1, 48000};
		delay.setLength(24000.0f);
		delay.setSampleRate(0);
		QVERIFY(delay.m_length <= static_cast<float>(delay.m_maxLength));
		checkOneFrameDelay(delay);
	}
};

QTEST_GUILESS_MAIN(StereoDelayTest)

#include "StereoDelay.moc"
