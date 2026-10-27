/*
 * TimelineTest.cpp
 *
 * Copyright (c) 2025 Keratin
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


#include <QtTest>
#include "Timeline.h"

#include "Metronome.h"
#include "Song.h"

class TimelineTest : public QObject
{
	Q_OBJECT
public:
	bool positionChangedReceived;
	bool positionJumpedReceived;
	void resetReceived()
	{
		positionChangedReceived = false;
		positionJumpedReceived = false;
	}

private slots:
	void onPositionChanged() { positionChangedReceived = true; }
	void onPositionJumped() { positionJumpedReceived = true; }

private slots:

	void initTestCase()
	{
		using namespace lmms;
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		using namespace lmms;
		Engine::destroy();
	}

	void JumpedTests()
	{
		using namespace lmms;

		Timeline timeline = Timeline();
		connect(&timeline, &Timeline::positionChanged, this, &TimelineTest::onPositionChanged);
		connect(&timeline, &Timeline::positionJumped, this, &TimelineTest::onPositionJumped);

		// By default, setting the ticks is treated as a forceful jump
		resetReceived();
		timeline.setTicks(10);
		QCOMPARE(timeline.ticks(), 10);
		QVERIFY(positionChangedReceived);
		QVERIFY(positionJumpedReceived);

		// However, using incrementTicks will not emit positionJumped.
		resetReceived();
		timeline.incrementTicks(10);
		QCOMPARE(timeline.ticks(), 20);
		QVERIFY(positionChangedReceived);
		QVERIFY(!positionJumpedReceived);
	}

	void ElapsedTimeTests()
	{
		using namespace lmms;

		Timeline timeline = Timeline();
		connect(&timeline, &Timeline::positionChanged, this, &TimelineTest::onPositionChanged);
		connect(&timeline, &Timeline::positionJumped, this, &TimelineTest::onPositionJumped);

		// Forecefully setting the ticks to 0 should reset the elapsed time
		timeline.setTicks(0);
		QCOMPARE(timeline.getElapsedSeconds(), 0);

		// Setting the ticks to a nonzero value should reset the elapsed time to that tick's time based on the current tempo
		Engine::getSong()->setTempo(240);
		double secondsPerTick = 60.0f / Engine::getSong()->getTempo() * 4 / DefaultTicksPerBar;
		timeline.setTicks(10);
		double initialElapsedSeconds = timeline.getElapsedSeconds();
		QCOMPARE(static_cast<int>(timeline.getElapsedSeconds() * 1000), static_cast<int>(10 * secondsPerTick * 1000)); // Rouding to milliseconds to prevent double comparison issues

		// Changing the tempo and then non-forcefully incrementing the ticks will increase the elapsed time based on the new tempo
		Engine::getSong()->setTempo(60);
		secondsPerTick = 60.0f / Engine::getSong()->getTempo() * 4 / DefaultTicksPerBar;
		timeline.incrementTicks(5);
		QCOMPARE(static_cast<int>(timeline.getElapsedSeconds() * 1000), static_cast<int>((initialElapsedSeconds + 5 * secondsPerTick) * 1000));

		// Forcefully setting the ticks (such as dragging the playhead with the mouse) will reset the elapsed time based on the global position and current tempo
		Engine::getSong()->setTempo(180);
		secondsPerTick = 60.0f / Engine::getSong()->getTempo() * 4 / DefaultTicksPerBar;
		timeline.setTicks(25);
		QCOMPARE(static_cast<int>(timeline.getElapsedSeconds() * 1000), static_cast<int>(25 * secondsPerTick * 1000));
	}

	// Metronome::processTick divides by beatsPerBar and takes a modulo by ticksPerBar / beatsPerBar. Both come from the
	// song's time-signature model, which clamps every write to 1..32, so neither reaches those operations as zero.
	void MetronomeTickMathTests()
	{
		using namespace lmms;
		Song* const song = Engine::getSong();
		MeterModel& meter = song->getTimeSigModel();

		meter.setNumerator(0);
		QCOMPARE(meter.getNumerator(), 1);
		meter.setNumerator(1000);
		QCOMPARE(meter.getNumerator(), 32);
		meter.setDenominator(0);
		QCOMPARE(meter.getDenominator(), 1);
		meter.setDenominator(1000);
		QCOMPARE(meter.getDenominator(), 32);

		// Every signature the model can hold: ticksPerBeat stays at least one tick and processTick walks the whole bar
		// without trapping. The metronome stays inactive, so no click is built and the audio path is not run here.
		Metronome metronome;
		for (int numerator = 1; numerator <= 32; ++numerator)
		{
			for (int denominator = 1; denominator <= 32; ++denominator)
			{
				meter.setNumerator(numerator);
				meter.setDenominator(denominator);

				const int ticksPerBar = song->ticksPerBar();
				const int beatsPerBar = meter.getNumerator();
				QVERIFY(beatsPerBar >= 1);
				QVERIFY(ticksPerBar >= beatsPerBar);
				QVERIFY(ticksPerBar / beatsPerBar >= 1);

				for (int tick = 0; tick < ticksPerBar; ++tick)
				{
					metronome.processTick(tick, ticksPerBar, beatsPerBar, 0);
				}
			}
		}

		meter.setNumerator(4);
		meter.setDenominator(4);
	}

};

QTEST_GUILESS_MAIN(TimelineTest)
#include "TimelineTest.moc"
