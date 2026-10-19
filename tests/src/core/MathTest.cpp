/*
 * MathTest.cpp
 *
 * Copyright (c) 2023 Johannes Lorenz <jlsf2013$users.sourceforge.net, $=@>
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

#include <QObject>
#include <QProcess>
#include <limits>
#include <type_traits>
#include <QtTest>

#include <cmath>
#include <limits>
#include <numbers>

#include "lmms_math.h"
// Expose only Lfo's state in this translation unit so tests can seed it
// deterministically against both the fixed and original implementations.
#define private public
#include "../../../plugins/Delay/Lfo.h"
#undef private
#include "../../../plugins/Delay/Lfo.cpp"

static void initializeLfoStateForTest(lmms::Lfo& lfo)
{
	constexpr int sampleRate = 48000;
	lfo.m_frequency = 1.0;
	lfo.m_phase = 0.0;
	lfo.m_twoPiOverSr = 2 * std::numbers::pi_v<float> / sampleRate;
	lfo.m_increment = lfo.m_frequency * lfo.m_twoPiOverSr;
	lfo.m_samplerate = sampleRate;
}

class MathTest : public QObject
{
	Q_OBJECT
private slots:
	void FastRandExtremeExclusiveRangeTest()
	{
		using namespace lmms;
		const auto value = fastRand(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
		QVERIFY(value > std::numeric_limits<int>::min());
	}

	void FastRandExtremeInclusiveRangeTest()
	{
		using namespace lmms;
		const auto value = fastRandInc(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
		QVERIFY(value > std::numeric_limits<int>::min());
	}

	void FastRandNarrowRangeReturnTypeTest()
	{
		using namespace lmms;
		using ShortLimits = std::numeric_limits<short>;
		static_assert(std::is_same_v<decltype(fastRand(ShortLimits::min(), ShortLimits::max())), int>);
		static_assert(std::is_same_v<decltype(fastRandInc(ShortLimits::min(), ShortLimits::max())), int>);
		const auto exclusive = fastRand(ShortLimits::min(), ShortLimits::max());
		const auto inclusive = fastRandInc(ShortLimits::min(), ShortLimits::max());
		QVERIFY(exclusive >= ShortLimits::min());
		QVERIFY(exclusive < ShortLimits::max());
		QVERIFY(inclusive >= ShortLimits::min());
		QVERIFY(inclusive <= ShortLimits::max());
	}

	void NumDigitsTest()
	{
		using namespace lmms;
		QCOMPARE(numDigitsAsInt(1.f), 1);
		QCOMPARE(numDigitsAsInt(9.9f), 2);
		QCOMPARE(numDigitsAsInt(10.f), 2);
		QCOMPARE(numDigitsAsInt(0.f), 1);
		QCOMPARE(numDigitsAsInt(-100.f), 4);
		QCOMPARE(numDigitsAsInt(-99.f), 3);
		QCOMPARE(numDigitsAsInt(-0.4f), 1); // there is no "-0" for LED spinbox
		QCOMPARE(numDigitsAsInt(-0.99f), 2);
		QCOMPARE(numDigitsAsInt(1000000000), 10);
		QCOMPARE(numDigitsAsInt(-1000000000), 11);
		QCOMPARE(numDigitsAsInt(900000000), 9);
		QCOMPARE(numDigitsAsInt(-900000000), 10);
	}

	void NonFiniteLfoFrequencyIsIgnored()
	{
		lmms::Lfo lfo(48000);
		initializeLfoStateForTest(lfo);
		lfo.setFrequency(std::numeric_limits<double>::quiet_NaN());

		for (int i = 0; i < 4; ++i)
		{
			QVERIFY(std::isfinite(lfo.tick()));
		}
	}

	void ZeroLfoSampleRateIsIgnored()
	{
		lmms::Lfo lfo(48000);
		initializeLfoStateForTest(lfo);
		lfo.setSampleRate(0);
		lfo.setFrequency(0.0);

		for (int i = 0; i < 4; ++i)
		{
			QVERIFY(std::isfinite(lfo.tick()));
		}
	}

	void FastRandIncReversedUnsignedBoundsTest()
	{
		using namespace lmms;
		for (int i = 0; i < 32; ++i)
		{
			const auto result = fastRandInc(10u, 3u);
			QVERIFY(result >= 3u && result <= 10u);
		}
	}

	void OneInZeroTest()
	{
		QProcess process;
		process.start(QCoreApplication::applicationFilePath(), {"--one-in-zero-child"});
		QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
		QVERIFY2(process.waitForFinished(), qPrintable(process.errorString()));
		QCOMPARE(process.exitStatus(), QProcess::NormalExit);
		QCOMPARE(process.exitCode(), 0);
	}
};

int main(int argc, char* argv[])
{
	QCoreApplication application(argc, argv);
	if (argc == 2 && QByteArray(argv[1]) == "--one-in-zero-child")
	{
		return lmms::oneIn(0) ? 1 : 0;
	}

	MathTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "MathTest.moc"
