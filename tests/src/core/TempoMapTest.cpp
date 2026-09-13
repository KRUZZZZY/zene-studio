/*
 * TempoMapTest.cpp - the tempo map: its shape, its arithmetic, its effect on the
 *                    transport, and the byte-identity of the empty-map path.
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

// The claims, each measured rather than argued (docs/TEMPO-MAP.md):
//
//  1. A map changes the tempo at the RIGHT TICK: on and after an event the
//     event's value, before the first event the GLOBAL value, and the tempo and
//     the metre are independent step functions over the one event list.
//  2. The ticks <-> time conversion is EXACT at and around every event:
//     `secondsAtTick` is continuous at an event (the time already elapsed when
//     the event is reached cannot depend on the tempo chosen AT it), the rate
//     changes immediately after it, and `tickAtSeconds` inverts it exactly on
//     the tick grid.
//  3. The EMPTY (and the INACTIVE) map is the pre-change arithmetic VERBATIM,
//     bit for bit, against the engine's own expression.
//  4. The map reaches the TIMING PATH: with it active the transport really
//     retimes - `Engine::framesPerTick()`, which every tick/frame conversion in
//     this engine reads, is the map's own answer - and with it off nothing at
//     all changes.
//
// Claim 5 - a project with NO map loads and re-saves BYTE-IDENTICALLY, and a map
// round-trips through the project file - is the other half of the proof and
// lives in tests/src/core/TempoMapPersistenceTest.cpp, so both files stay inside
// the 500-line new-file cap.

#include <QtTest>

#include <cmath>

#include "AudioEngine.h"
#include "Engine.h"
#include "Song.h"
#include "TempoMap.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

constexpr int kMappedTempo = 90;
constexpr int kSecondTempo = 180;

//! The engine's own pre-change expression, transcribed from
//! Engine::updateFramesPerTick() so the "verbatim" claim has something to be
//! compared against that is not the code under test's own helper.
float legacyFramesPerTick(sample_rate_t sampleRate, int bpm)
{
	return sampleRate * 60.0f * 4 / DefaultTicksPerBar / bpm;
}

sample_rate_t engineSampleRate()
{
	return Engine::audioEngine()->outputSampleRate();
}

//! The global tempo model's value, READ from the live song rather than assumed:
//! it is the map's out-of-range answer, and every comparison against it has to
//! be against the engine's own state.
int kGlobalTempo()
{
	return static_cast<int>(Engine::getSong()->getTempo());
}


//! The three-event map every test below works from: a tempo at tick 0, a metre
//! at 384, a tempo at 768 - so events that carry only one half sit between
//! events that carry the other, and the two step functions have to be skips.
TempoMap threeEventMap()
{
	TempoMap map;
	TempoMapEvent first;
	first.tick = 0;
	first.hasTempo = true;
	first.tempo = kMappedTempo;
	TempoMapEvent middle;
	middle.tick = 384;
	middle.hasTimeSignature = true;
	middle.numerator = 3;
	middle.denominator = 4;
	TempoMapEvent last;
	last.tick = 768;
	last.hasTempo = true;
	last.tempo = kSecondTempo;
	const bool built = map.addEvent(first) && map.addEvent(middle) && map.addEvent(last);
	Q_ASSERT(built);
	(void)built;
	map.setActive(true);
	return map;
}

/*! Play \a blocks audio periods and answer the ticks the play head moved.
 *
 *  The map's effect on the timing path IS the frame/tick rate the transport
 *  advances at, so this measures the behaviour rather than a proxy for it. The
 *  engine scalar is reset to the engine's own expression first, so each run
 *  starts from a known state instead of from the previous run's.
 */
tick_t advancePlayhead(Song* song, int blocks)
{
	Engine::updateFramesPerTick();
	song->stop();
	song->setPlayPos(0);
	song->playSong();
	for (int i = 0; i < blocks; ++i) { song->processNextBuffer(); }
	const tick_t advanced = song->getPlayPos().getTicks();
	song->stop();
	return advanced;
}


} // namespace


class TempoMapTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The dummy device thread would otherwise drive processNextBuffer()
		// behind this test's back; the transport is driven synchronously here.
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase()
	{
		Engine::getSong()->stop();
		Engine::destroy();
	}

	//! (1) The tempo at a tick is the last event at or before it, and the GLOBAL
	//! value before the first one - per property, so the metre event between the
	//! two tempo events does not disturb the tempo, and the reverse also holds.
	void theTempoChangesAtTheRightTick()
	{
		const TempoMap map = threeEventMap();
		QCOMPARE(map.tempoAtTick(0, kGlobalTempo()), kMappedTempo);
		QCOMPARE(map.tempoAtTick(383, kGlobalTempo()), kMappedTempo);
		QCOMPARE(map.tempoAtTick(384, kGlobalTempo()), kMappedTempo);
		QCOMPARE(map.tempoAtTick(767, kGlobalTempo()), kMappedTempo);
		QCOMPARE(map.tempoAtTick(768, kGlobalTempo()), kSecondTempo);
		// Past the last event the last event holds: 100 bars later it is still 180.
		QCOMPARE(map.tempoAtTick(100 * DefaultTicksPerBar, kGlobalTempo()), kSecondTempo);

		const TempoMapTimeSignature global{ 4, 4 };
		QCOMPARE(map.timeSignatureAtTick(0, global), global);
		QCOMPARE(map.timeSignatureAtTick(383, global), global);
		const TempoMapTimeSignature threeFour{ 3, 4 };
		QCOMPARE(map.timeSignatureAtTick(384, global), threeFour);
		QCOMPARE(map.timeSignatureAtTick(768, global), threeFour);
		QCOMPARE(map.timeSignatureAtTick(100000, global), threeFour);

		// BEFORE the first event the GLOBAL value is in force, which is what
		// stops an event at bar 16 from retiming bars 1-15.
		TempoMap late;
		TempoMapEvent event;
		event.tick = 4 * DefaultTicksPerBar;
		event.hasTempo = true;
		event.tempo = 200;
		QVERIFY(late.addEvent(event));
		late.setActive(true);
		QCOMPARE(late.tempoAtTick(0, kGlobalTempo()), kGlobalTempo());
		QCOMPARE(late.tempoAtTick(4 * DefaultTicksPerBar - 1, kGlobalTempo()), kGlobalTempo());
		QCOMPARE(late.tempoAtTick(4 * DefaultTicksPerBar, kGlobalTempo()), 200);
	}

	//! (1b) An INACTIVE map is never consulted, whatever it holds; the same map
	//! switched on is. This is what makes "the map is off" and "there is no map"
	//! the same state for every reader.
	void anInactiveMapAnswersTheGlobalValueEverywhere()
	{
		TempoMap map = threeEventMap();
		QCOMPARE(map.tempoAtTick(768, kGlobalTempo()), kSecondTempo);
		map.setActive(false);
		for (const tick_t tick : { 0, 383, 384, 768, 100000 })
		{
			QCOMPARE(map.tempoAtTick(tick, kGlobalTempo()), kGlobalTempo());
		}
		const TempoMapTimeSignature global{ 4, 4 };
		QCOMPARE(map.timeSignatureAtTick(768, global), global);
		// The events survive the switch: they are still project state.
		QCOMPARE(map.size(), 3);
		QVERIFY(map.shouldPersist());
	}

	//! (3) THE BYTE-IDENTITY PATH, arithmetic half: an empty or inactive map
	//! evaluates the engine's own expressions, bit for bit.
	void theEmptyMapIsThePreChangeArithmeticVerbatim()
	{
		const TempoMap empty;
		TempoMap inactive;
		QVERIFY(inactive.set(threeEventMap().all()));
		QVERIFY(!inactive.active());
		QCOMPARE(inactive.size(), 3);

		const sample_rate_t rate = engineSampleRate();
		for (const tick_t tick : { 0, 1, 191, 192, 767, 768, 100000 })
		{
			for (const int bpm : { static_cast<int>(MinTempo), 100, 140, static_cast<int>(MaxTempo) })
			{
				// framesPerTickAtTick: the engine's expression, verbatim.
				const float expected = legacyFramesPerTick(rate, bpm);
				QVERIFY2(empty.framesPerTickAtTick(tick, rate, bpm) == expected,
					qPrintable(QStringLiteral("the empty map's frames-per-tick at %1 is not the "
						"engine's own expression").arg(tick)));
				QVERIFY2(inactive.framesPerTickAtTick(tick, rate, bpm) == expected,
					qPrintable(QStringLiteral("an INACTIVE map changed the frames-per-tick at %1")
						.arg(tick)));

				// secondsAtTick: TimePos's own conversion, verbatim.
				const double seconds =
					TimePos::ticksToMilliseconds(tick, static_cast<bpm_t>(bpm)) / 1000.0;
				QVERIFY2(empty.secondsAtTick(tick, bpm) == seconds,
					qPrintable(QStringLiteral("the empty map's ticks-to-seconds at %1 is not "
						"TimePos's conversion").arg(tick)));
				QVERIFY2(inactive.secondsAtTick(tick, bpm) == seconds,
					qPrintable(QStringLiteral("an INACTIVE map changed ticks-to-seconds at %1")
						.arg(tick)));
			}
		}
		// The same object answers differently with the map on, so the equality
		// above is a property of the OFF state and not a vacuous one.
		inactive.setActive(true);
		QCOMPARE(inactive.tempoAtTick(768, kGlobalTempo()), kSecondTempo);
		QVERIFY(inactive.secondsAtTick(768, kGlobalTempo())
			!= TimePos::ticksToMilliseconds(768, static_cast<bpm_t>(kGlobalTempo())) / 1000.0);
	}

	//! (2) The conversion is exact AT and AROUND each event: continuous at it,
	//! a different rate immediately after it, and exactly invertible.
	void theTickToTimeConversionIsExactAtEveryEvent()
	{
		const TempoMap map = threeEventMap();
		const double perTickAfterFirst = 1.25 / kMappedTempo;
		const double perTickAfterLast = 1.25 / kSecondTempo;

		// CONTINUITY at the events: the time already elapsed when tick T is
		// reached cannot depend on the tempo chosen AT T.
		QCOMPARE(map.secondsAtTick(0, kGlobalTempo()), 0.0);
		QCOMPARE(map.secondsAtTick(767, kGlobalTempo()), 767 * perTickAfterFirst);
		QCOMPARE(map.secondsAtTick(768, kGlobalTempo()), 768 * perTickAfterFirst);
		QCOMPARE(map.secondsAtTick(769, kGlobalTempo()),
			768 * perTickAfterFirst + perTickAfterLast);

		// ...and the RATE changes immediately after the event, which is what
		// makes this a tempo change and not a rounding difference.
		const double beforeStep =
			map.secondsAtTick(768, kGlobalTempo()) - map.secondsAtTick(767, kGlobalTempo());
		const double afterStep =
			map.secondsAtTick(769, kGlobalTempo()) - map.secondsAtTick(768, kGlobalTempo());
		QVERIFY2(std::fabs(beforeStep - perTickAfterFirst) < 1e-12,
			"the step before the event is not the old tempo");
		QVERIFY2(std::fabs(afterStep - perTickAfterLast) < 1e-12,
			"the step after the event is not the new tempo");

		// EXACTNESS of the inverse on the tick grid, at and around every event.
		const sample_rate_t rate = engineSampleRate();
		for (const tick_t tick : { 0, 1, 191, 383, 384, 385, 767, 768, 769, 1000, 100000 })
		{
			const double seconds = map.secondsAtTick(tick, kGlobalTempo());
			QCOMPARE(map.tickAtSeconds(seconds, kGlobalTempo()), tick);
			if (tick > 0)
			{
				// The largest tick whose elapsed time is still <= the last
				// instant before `tick` is the one before it - the property the
				// inverse is defined by, checked from both sides of each event.
				QCOMPARE(map.tickAtSeconds(seconds - 1e-9, kGlobalTempo()), tick - 1);
			}
			QCOMPARE(map.tickAtSeconds(map.secondsAtTick(tick + 1, kGlobalTempo()), kGlobalTempo()),
				tick + 1);

			// Frames: the same conversion scaled by the sample rate, and the
			// frame -> tick inverse lands on the tick it came from. The frame
			// count is rounded UP because a fractional frame is not addressable
			// (one tick is 55+ frames at any tempo this engine allows, so a whole
			// frame can never reach the next tick).
			const double frames = map.framesAtTick(tick, rate, kGlobalTempo());
			QCOMPARE(frames, seconds * static_cast<double>(rate));
			QCOMPARE(map.tickAtFrame(static_cast<f_cnt_t>(std::ceil(frames)), rate, kGlobalTempo()),
				tick);
		}
	}

	//! (4) The map REACHES THE TIMING PATH. `Engine::framesPerTick()` is the
	//! scalar every tick/frame conversion in the engine reads, so asserting it
	//! against the map's own answer asserts that the follower ran - and the play
	//! head moving twice as far in the same wall time is the behavioural half.
	void theMapRetimesThePlayheadAndAnInactiveOneDoesNot()
	{
		Song* song = Engine::getSong();
		const sample_rate_t rate = engineSampleRate();
		const int global = song->getTempo();
		const float unmappedRate = legacyFramesPerTick(rate, global);

		song->stop();
		const tick_t unmappedTicks = advancePlayhead(song, 64);
		QVERIFY2(unmappedTicks > 0, "the transport did not advance at all");
		QVERIFY2(Engine::framesPerTick() == unmappedRate,
			"the unmapped frame/tick rate is not the engine's own expression");

		// The same event, the map switched OFF: nothing about the transport may
		// change. This is the byte-identity claim on the timing path.
		TempoMapEvent event;
		event.tick = 0;
		event.hasTempo = true;
		event.tempo = global * 2;
		QVERIFY(song->tempoMap().edit([&event](TempoMap& map) { return map.addEvent(event); }));
		QVERIFY(!song->tempoMap().map().active());
		QCOMPARE(song->tempoAtTick(0), global);
		QCOMPARE(advancePlayhead(song, 64), unmappedTicks);
		QVERIFY2(Engine::framesPerTick() == unmappedRate,
			"an INACTIVE map moved the engine's timing scalar");

		// Switched ON, the timeline obeys it: twice the tempo is twice the ticks
		// in the same number of frames, within a block of quantisation.
		QVERIFY(song->tempoMap().edit([](TempoMap& map) { map.setActive(true); return true; }));
		QCOMPARE(song->tempoAtTick(0), global * 2);
		const tick_t mappedTicks = advancePlayhead(song, 64);
		QVERIFY2(Engine::framesPerTick()
				== song->tempoMap().map().framesPerTickAtTick(0, rate, global),
			"the engine's timing scalar is not the map's own answer at the play head");
		QVERIFY2(mappedTicks + 2 >= unmappedTicks * 2 && mappedTicks <= unmappedTicks * 2 + 2,
			qPrintable(QStringLiteral("64 blocks advanced %1 ticks unmapped and %2 mapped; "
				"twice the tempo must move the play head about twice as far")
				.arg(unmappedTicks).arg(mappedTicks)));

		// And clearing the map puts the transport back exactly where it was.
		QVERIFY(song->tempoMap().edit([](TempoMap& map) { map.clear(); return true; }));
		QCOMPARE(advancePlayhead(song, 64), unmappedTicks);
		QVERIFY2(Engine::framesPerTick() == unmappedRate,
			"clearing the map did not restore the engine's timing scalar");
	}

	//! The bounds are one rule in one place, a rejected event changes nothing,
	//! and add-or-replace merges PER PROPERTY.
	void invalidEventsAreRefusedAndChangeNothing()
	{
		TempoMap map;
		TempoMapEvent empty;
		empty.tick = 0;
		QVERIFY(!map.addEvent(empty));  // neither half
		QVERIFY(map.empty());
		QVERIFY(!TempoMap::validEvent(empty));

		TempoMapEvent tooFast;
		tooFast.hasTempo = true;
		tooFast.tempo = TempoMapMaxTempo + 1;
		QVERIFY(!map.addEvent(tooFast));
		TempoMapEvent tooSlow;
		tooSlow.hasTempo = true;
		tooSlow.tempo = TempoMapMinTempo - 1;
		QVERIFY(!map.addEvent(tooSlow));
		QVERIFY(map.empty());

		TempoMapEvent oddMetre;
		oddMetre.hasTimeSignature = true;
		oddMetre.numerator = 7;
		oddMetre.denominator = 3;  // not a power of two
		QVERIFY(!map.addEvent(oddMetre));
		TempoMapEvent badNumerator;
		badNumerator.hasTimeSignature = true;
		badNumerator.numerator = TempoMapMaxNumerator + 1;
		badNumerator.denominator = 4;
		QVERIFY(!map.addEvent(badNumerator));
		QVERIFY(map.empty());

		TempoMapEvent negative;
		negative.tick = -1;
		negative.hasTempo = true;
		QVERIFY(!map.addEvent(negative));

		// The capacity is a bound, not a silent drop.
		for (int i = 0; i < TempoMap::MaxEvents; ++i)
		{
			TempoMapEvent event;
			event.tick = i * DefaultTicksPerBar;
			event.hasTempo = true;
			event.tempo = 100 + (i % 20);
			QVERIFY2(map.addEvent(event), "the map refused an event inside its capacity");
		}
		QCOMPARE(map.size(), TempoMap::MaxEvents);
		TempoMapEvent overflow;
		overflow.tick = TempoMap::MaxEvents * DefaultTicksPerBar;
		overflow.hasTempo = true;
		overflow.tempo = 120;
		QVERIFY(!map.addEvent(overflow));
		QCOMPARE(map.size(), TempoMap::MaxEvents);

		// add-or-replace merges PER PROPERTY: the metre at a tick survives a
		// tempo written at the same tick, and the reverse.
		TempoMap merged;
		TempoMapEvent metre;
		metre.tick = 192;
		metre.hasTimeSignature = true;
		metre.numerator = 5;
		metre.denominator = 8;
		QVERIFY(merged.addEvent(metre));
		TempoMapEvent tempo;
		tempo.tick = 192;
		tempo.hasTempo = true;
		tempo.tempo = 96;
		QVERIFY(merged.addEvent(tempo));
		QCOMPARE(merged.size(), 1);
		QCOMPARE(merged[0].tempo, 96);
		QCOMPARE(merged[0].numerator, 5);
		QCOMPARE(merged[0].denominator, 8);
		QVERIFY(merged.removeEvent(192));
		QCOMPARE(merged.size(), 0);
		QVERIFY(!merged.removeEvent(192));
	}

	//! The publisher's snapshot answers what the map held when the last edit
	//! landed - the audio thread's view of it.
	void thePublisherSnapshotSeesTheLastCompletedEdit()
	{
		TempoMapPublisher publisher;
		QCOMPARE(publisher.snapshot().size(), 0);
		QCOMPARE(publisher.snapshot().tempoAtTick(0, kGlobalTempo()), kGlobalTempo());

		const TempoMap authored = threeEventMap();
		QVERIFY(publisher.edit([&authored](TempoMap& map) { map = authored; return true; }));
		QCOMPARE(publisher.snapshot().size(), 3);
		QCOMPARE(publisher.snapshot().tempoAtTick(0, kGlobalTempo()), kMappedTempo);
		QVERIFY(publisher.snapshot().active());

		QVERIFY(!publisher.edit([](TempoMap& map) { return map.removeEvent(999); }));
		QCOMPARE(publisher.snapshot().size(), 3);
	}
};

QTEST_GUILESS_MAIN(TempoMapTest)
#include "TempoMapTest.moc"
