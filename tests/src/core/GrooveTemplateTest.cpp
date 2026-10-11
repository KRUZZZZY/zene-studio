/*
 * GrooveTemplateTest.cpp - the groove ENGINE: the value type, the extraction
 *                          and application arithmetic, and the pool's own
 *                          keying and XML form.
 *
 * Everything here runs with NO Engine, no track, no clip and no GUI: the
 * arithmetic is a set of plain functions over a note list
 * (include/GrooveTemplate.h) and the pool is a value type with an XML form
 * (include/GroovePool.h). The control SURFACE that drives them is held to
 * account by tests/src/core/ControlGrooveCommandsTest.cpp and, end to end over
 * a real socket, by the registered ctest `ControlGrooveCommands`
 * (tests/control-groove-commands.py).
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

#include <limits>

#include <QString>
#include <QStringList>
#include <QVector>

#include "GrooveTestSupport.h"

using namespace lmms;
using namespace groovetest;

class GrooveTemplateTest : public QObject
{
	Q_OBJECT
private slots:

	//! The geometry the engine accepts, and the bounds every refusal names.
	void theGeometryBoundsAreTheEnginesOwn()
	{
		QVERIFY(GrooveTemplate::isWritable(QStringLiteral("feel"), 48, 12));
		QVERIFY(!GrooveTemplate::isWritable(QString(), 48, 12));
		QVERIFY(!GrooveTemplate::isWritable(QStringLiteral("  "), 48, 12));
		QVERIFY(!GrooveTemplate::isWritable(QStringLiteral("feel"), 13, 12));   // not whole slots
		QVERIFY(!GrooveTemplate::isWritable(QStringLiteral("feel"), 0, 12));
		QVERIFY(!GrooveTemplate::isWritable(QStringLiteral("feel"), 48, 0));    // no slot width
		QVERIFY(!GrooveTemplate::isWritable(QStringLiteral("feel"),
			(GrooveTemplate::MaxSteps + 1) * 12, 12));
		QVERIFY(GrooveTemplate::isWritable(QStringLiteral("feel"),
			GrooveTemplate::MaxSteps * 12, 12));
		// A name is trimmed and bounded, not rejected at 65 characters.
		QCOMPARE(GrooveTemplate::normalisedName(QStringLiteral("  feel \n")),
			QStringLiteral("feel"));
		QCOMPARE(GrooveTemplate::normalisedName(QString(80, QLatin1Char('x'))).size(),
			GrooveTemplate::MaxNameLength);

		GrooveTemplate groove(QStringLiteral("feel"), 48, 12);
		QVERIFY(groove.valid());
		QCOMPARE(groove.slotCount(), 4);
		QVERIFY(groove.neutral());
		// HALF a slot is one tick too far: it is the boundary between two slots,
		// and a note standing on it is read back into the NEXT one, so a step of
		// half a slot would carry its notes out of their own slot (the reason is
		// proved in aStepCannotReachHalfItsSlot). A slot less one tick is the
		// largest timing offset a step may carry - past it a note would belong to
		// a different slot, which is a rearrangement. A velocity is a velocity,
		// so the only "smaller than 0" it knows is the explicit "no opinion".
		QVERIFY(groove.setStep(0, GrooveStep{5, 200}));
		QVERIFY(!groove.setStep(0, GrooveStep{6, 0}));     // exactly half a slot
		QVERIFY(!groove.setStep(0, GrooveStep{7, 0}));
		QVERIFY(!groove.setStep(0, GrooveStep{0, static_cast<int>(MaxVolume) + 1}));
		QVERIFY(!groove.setStep(0, GrooveStep{0, -2}));
		QVERIFY(!groove.setStep(4, GrooveStep{0, 0}));
		QCOMPARE(groove.step(0), (GrooveStep{5, 200}));   // the refused writes did nothing
		QVERIFY(!groove.neutral());
		QVERIFY(groove.setStep(0, GrooveStep{0, GrooveStep::NoVelocityOpinion}));
		QVERIFY(groove.step(0).neutral());
		QVERIFY(groove.setStep(0, GrooveStep{5, 200}));
		QVERIFY(groove.step(0) == (GrooveStep{5, 200}));
	}

	//! Integer offsets of two ticks are inside half of an odd five-tick slot.
	void anOddStepAcceptsOffsetsInsideItsHalfSlot()
	{
		GrooveTemplate groove(QStringLiteral("odd"), 20, 5);
		const bool positive = groove.setStep(0, GrooveStep{2, 100});
		const bool negative = groove.setStep(1, GrooveStep{-2, 100});
		QVERIFY2(positive && negative, "a two-tick offset is inside half of a five-tick slot");
		QVERIFY(!groove.setStep(2, GrooveStep{3, 100}));
		QVERIFY(!groove.setStep(3, GrooveStep{-3, 100}));
	}

	/*! THE EXTRACTION RULE. Each slot's step is the MEAN signed deviation of
	 *  the notes that fell in it, and their mean velocity RELATIVE to the clip's
	 *  own mean - so the template is a shape and not a loudness. */
	void extractionReadsTheMeanDeviationPerSlot()
	{
		NoteTable source;
		fillFeelClip(&source);

		GrooveTemplate captured;
		int read = 0;
		QVERIFY(extractGroove(source.vector(), QStringLiteral("feel"), 48, 12, &captured, &read));
		QCOMPARE(read, 4);
		QCOMPARE(captured.slotCount(), 4);
		// Notes at 9 / 26 / 34 / 51 land in slots 1 / 2 / 3 / 0 (modulo 4), so
		// each slot's velocity is exactly that note's own.
		QCOMPARE(captured.step(0), (GrooveStep{3, 100}));
		QCOMPARE(captured.step(1), (GrooveStep{-3, 120}));
		QCOMPARE(captured.step(2), (GrooveStep{2, 80}));
		QCOMPARE(captured.step(3), (GrooveStep{-2, 100}));

		// A clip with no notes has no feel to capture: refused, and the
		// caller's template is untouched.
		NoteTable empty;
		GrooveTemplate untouched;
		QVERIFY(!extractGroove(empty.vector(), QStringLiteral("feel"), 48, 12, &untouched, &read));
		QCOMPARE(read, 0);
		QVERIFY(!untouched.valid());

		// A slot nothing landed in is NEUTRAL, not skipped: the slot count is
		// the template's shape, and an unplayed slot says NOTHING about velocity
		// (a velocity of 0 there would silence the notes that land on it in the
		// clip the groove is applied to).
		NoteTable sparse;
		sparse.add(60, 12, 12, 100);
		GrooveTemplate twoSlots;
		QVERIFY(extractGroove(sparse.vector(), QStringLiteral("one"), 48, 12, &twoSlots, &read));
		QCOMPARE(read, 1);
		QCOMPARE(twoSlots.slotCount(), 4);
		QCOMPARE(twoSlots.step(1), (GrooveStep{0, 100}));
		QCOMPARE(twoSlots.step(0), (GrooveStep{0, GrooveStep::NoVelocityOpinion}));
		QVERIFY(twoSlots.step(0).neutral());
		QVERIFY(!twoSlots.neutral());
	}

	//! Extraction preserves the furthest integer offsets inside an odd slot's half-width.
	void extractionKeepsOffsetsInsideAnOddStepHalfSlot()
	{
		NoteTable source;
		source.add(60, 2, 5, 100);    // slot 0, +2
		source.add(62, 3, 5, 100);    // slot 1, -2
		source.add(64, 12, 5, 100);   // slot 2, +2
		source.add(65, 13, 5, 100);   // slot 3, -2

		GrooveTemplate captured;
		QVERIFY(extractGroove(source.vector(), QStringLiteral("odd"), 20, 5, &captured, nullptr));
		QCOMPARE(captured.step(0), (GrooveStep{2, 100}));
		QCOMPARE(captured.step(1), (GrooveStep{-2, 100}));
		QCOMPARE(captured.step(2), (GrooveStep{2, 100}));
		QCOMPARE(captured.step(3), (GrooveStep{-2, 100}));
	}

	/*! THE APPLICATION: a note moves `strength` of the way to its slot's target
	 *  (`pos + round(strength * (target - pos))`), at strength 1 it lands on the
	 *  target exactly, and a second application has nothing to do. */
	void applicationIsExactAndIdempotent()
	{
		NoteTable source;
		fillFeelClip(&source);
		GrooveTemplate captured;
		QVERIFY(extractGroove(source.vector(), QStringLiteral("feel"), 48, 12, &captured, nullptr));

		NoteTable straight;
		straight.add(60, 0, 12, 100);
		straight.add(62, 12, 12, 100);
		straight.add(64, 24, 12, 100);
		straight.add(65, 36, 12, 100);

		QCOMPARE(applyGroove(straight.vector(), captured, 1.0f), 4);
		QVERIFY2((takeOf(straight.vector()) == QVector<NoteAt>{{3, 100}, {9, 120}, {26, 80}, {34, 100}}),
			qPrintable(describe(takeOf(straight.vector()))));
		QCOMPARE(applyGroove(straight.vector(), captured, 1.0f), 0);   // idempotent

		// Half strength travels half the way (12 -> 9 is -3, so round(-1.5) is
		// -2), and moves the velocity half the 20 it would have moved.
		NoteTable half;
		half.add(62, 12, 12, 100);
		QCOMPARE(applyGroove(half.vector(), captured, 0.5f), 1);
		QVERIFY2((takeOf(half.vector()) == QVector<NoteAt>{{10, 110}}),
			qPrintable(describe(takeOf(half.vector()))));

		// Strength 0 is a no-op. A groove that says nothing is NOT a no-op at
		// strength 1: applying a groove lands every note on its slot, so a
		// neutral one is the plain grid quantise (which is what makes
		// "quantise to the grid" and "quantise to this groove" one operation).
		QCOMPARE(applyGroove(half.vector(), captured, 0.0f), 0);
		GrooveTemplate neutral(QStringLiteral("straight"), 48, 12);
		QCOMPARE(applyGroove(half.vector(), neutral, 1.0f), 1);
		QVERIFY2((takeOf(half.vector()) == QVector<NoteAt>{{12, 110}}),
			qPrintable(describe(takeOf(half.vector()))));

		// A slot velocity is an absolute target inside the engine's own 0..200,
		// so a groove carried onto a louder clip pulls the note DOWN to it (the
		// mirror of a quiet clip being pushed up), and half the strength is half
		// the distance.
		GrooveTemplate loud(QStringLiteral("loud"), 12, 12);
		QVERIFY(loud.setStep(0, GrooveStep{0, 150}));
		NoteTable quiet;
		quiet.add(60, 0, 12, 190);
		QCOMPARE(applyGroove(quiet.vector(), loud, 1.0f), 1);
		QCOMPARE(static_cast<int>(quiet.vector()[0]->getVolume()), 150);
		NoteTable halfLoud;
		halfLoud.add(60, 0, 12, 190);
		QCOMPARE(applyGroove(halfLoud.vector(), loud, 0.5f), 1);
		QCOMPARE(static_cast<int>(halfLoud.vector()[0]->getVolume()), 170);
	}

	//! The pool: the name is the key, and its XML form round-trips exactly.
	/*! The RANDOM amount is a REPRODUCIBLE jitter, which is the whole reason it
	 *  is drawn from the seed and the note's identity rather than from hidden
	 *  state: an agent that liked a take can ask for it again. Three claims, and
	 *  the third is the one that protects every existing caller - with the amount
	 *  left at its default the groove lands exactly where it did before this
	 *  argument existed. */
	void theRandomAmountIsReproducibleAndDoesNotDisturbTheDefault()
	{
		const auto straight = []() {
			NoteTable table;
			table.add(60, 0, 12, 100);
			table.add(62, 12, 12, 100);
			table.add(64, 24, 12, 100);
			table.add(65, 36, 12, 100);
			return table;
		};

		NoteTable feelSource;
		fillFeelClip(&feelSource);
		GrooveTemplate captured;
		QVERIFY(extractGroove(feelSource.vector(), QStringLiteral("feel"), 48, 12, &captured, nullptr));

		// The default: no random at all.
		NoteTable exact = straight();
		applyGroove(exact.vector(), captured, 1.0f);
		const QVector<NoteAt> grooveOnly = takeOf(exact.vector());

		// The same seed twice is the same take, note for note.
		NoteTable first = straight();
		applyGroove(first.vector(), captured, 1.0f, 6, 0, 12345u);
		NoteTable second = straight();
		applyGroove(second.vector(), captured, 1.0f, 6, 0, 12345u);
		QVERIFY2(takeOf(first.vector()) == takeOf(second.vector()),
			qPrintable(describe(takeOf(first.vector()))));

		// It is a real jitter: the take is not the groove's own landing.
		QVERIFY2(takeOf(first.vector()) != grooveOnly,
			"the random amount did not move a single note");

		// A different seed is a different take.
		NoteTable other = straight();
		applyGroove(other.vector(), captured, 1.0f, 6, 0, 999u);
		QVERIFY2(takeOf(other.vector()) != takeOf(first.vector()),
			"two different seeds produced the same take");
	}

	void thePoolIsKeyedByItsNameAndRoundTrips()
	{
		GroovePool pool;
		bool replaced = false;
		QVERIFY(pool.set(GrooveTemplate(QStringLiteral("shuffle"), 48, 12), &replaced));
		QCOMPARE(replaced, false);
		QVERIFY(pool.find(QStringLiteral("shuffle")) != nullptr);
		QCOMPARE(pool.size(), 1);

		GrooveTemplate other(QStringLiteral("shuffle"), 24, 12);
		QVERIFY(other.setStep(1, GrooveStep{-2, 7}));
		QVERIFY(pool.set(other, &replaced));
		QCOMPARE(replaced, true);
		QCOMPARE(pool.size(), 1);                                   // replaced, not appended
		QCOMPARE(pool.find(QStringLiteral("shuffle"))->lengthTicks(), tick_t(24));

		// A rename onto a name that is TAKEN is refused: the name is the key,
		// so it would destroy that groove.
		QVERIFY(pool.set(GrooveTemplate(QStringLiteral("swing"), 12, 12), nullptr));
		QVERIFY(!pool.rename(QStringLiteral("swing"), QStringLiteral("shuffle")));
		QVERIFY(pool.find(QStringLiteral("swing")) != nullptr);
		QVERIFY(pool.rename(QStringLiteral("swing"), QStringLiteral("swing")));
		QVERIFY(pool.rename(QStringLiteral("swing"), QStringLiteral("  swing  ")));
		QVERIFY(pool.rename(QStringLiteral("swing"), QStringLiteral("other")));
		QVERIFY(pool.find(QStringLiteral("other")) != nullptr);

		const QString xml = pool.toXml();
		GroovePool restored;
		QVERIFY(restored.fromXml(xml));
		QCOMPARE(restored.size(), 2);
		QVERIFY(restored.find(QStringLiteral("other")) != nullptr);
		const GrooveTemplate* shuffle = restored.find(QStringLiteral("shuffle"));
		QVERIFY(shuffle != nullptr);
		QCOMPARE(shuffle->slotCount(), 2);
		QCOMPARE(shuffle->step(1), (GrooveStep{-2, 7}));
		QVERIFY(restored.remove(QStringLiteral("other")));
		QCOMPARE(restored.size(), 1);
		QVERIFY(!restored.remove(QStringLiteral("other")));

		// RESET ON ABSENCE, and it is the rule the PROJECT FILE depends on: the
		// element is written only when the pool is non-empty, so restoring
		// nothing has to reach an EMPTY pool rather than leave what was there.
		QVERIFY(!restored.fromXml(QString()));
		QVERIFY(restored.empty());
		QVERIFY(!restored.shouldPersist());

		GroovePool full;
		for (int index = 0; index < GroovePool::MaxTemplates; ++index)
		{
			QVERIFY(full.set(GrooveTemplate(QStringLiteral("g%1").arg(index), 12, 12), nullptr));
		}
		QVERIFY(!full.set(GrooveTemplate(QStringLiteral("one-too-many"), 12, 12), nullptr));
		QCOMPARE(full.size(), GroovePool::MaxTemplates);
		// ... and REPLACING a name that is already there is still allowed when
		// the pool is full, which is what makes extract-over-a-name usable.
		QVERIFY(full.set(GrooveTemplate(QStringLiteral("g0"), 24, 12), nullptr));
		QCOMPARE(full.find(QStringLiteral("g0"))->lengthTicks(), tick_t(24));
	}

	/*! A step's timing may not REACH half its slot width, and the reason is the
	 *  nearest-slot rule: exactly half a slot is the BOUNDARY between two slots,
	 *  and a note sitting on the boundary is read back into the NEXT one. A step
	 *  of +half would therefore carry every note one slot along, and a second
	 *  application would carry it one slot more - the no-op at full strength
	 *  (GrooveTemplate.h, applyGroove) would not hold. */
	void aStepCannotReachHalfItsSlot()
	{
		GrooveTemplate groove(QStringLiteral("feel"), 48, 12);
		QVERIFY(groove.setStep(0, GrooveStep{5, 100}));
		QVERIFY(!groove.setStep(0, GrooveStep{6, 100}));      // exactly half: the boundary
		QVERIFY(!groove.setStep(0, GrooveStep{-6, 100}));     // ... and the other side of it
		QCOMPARE(groove.step(0), (GrooveStep{5, 100}));       // the refused writes did nothing

		// The largest shift that IS accepted keeps its note in its own slot, so
		// the same groove applied again has nothing left to do.
		GrooveTemplate late(QStringLiteral("late"), 48, 12);
		QVERIFY(late.setStep(0, GrooveStep{5, GrooveStep::NoVelocityOpinion}));
		NoteTable notes;
		notes.add(60, 0, 12, 100);
		QCOMPARE(applyGroove(notes.vector(), late, 1.0f), 1);
		QVERIFY2((takeOf(notes.vector()) == QVector<NoteAt>{{5, 100}}),
			qPrintable(describe(takeOf(notes.vector()))));
		QCOMPARE(applyGroove(notes.vector(), late, 1.0f), 0);
		QVERIFY2((takeOf(notes.vector()) == QVector<NoteAt>{{5, 100}}),
			qPrintable(describe(takeOf(notes.vector()))));
	}

	/*! The grid arithmetic is done wider than a tick and narrowed back CLAMPED,
	 *  so a note at the very end of the timeline reads its own slot instead of
	 *  wrapping around into a negative one. */
	void theGridArithmeticDoesNotOverflowAtTheEndOfTheTimeline()
	{
		GrooveTemplate groove(QStringLiteral("end"), 48, 12);
		QVERIFY(groove.setStep(0, GrooveStep{0, 100}));
		const tick_t last = std::numeric_limits<tick_t>::max();
		// 2147483647 = 12 * 178956970 + 7, and 7 is past half a slot, so the slot
		// it falls in is the one starting at 2147483652 - a tick PAST the end of
		// the representable timeline. The answer is the last tick itself: a grid
		// line that cannot be represented clamps, where tick_t arithmetic wrapped
		// it into 2147483644 (the wrong slot, three ticks before the note).
		QCOMPARE(groove.gridTickFor(last), last);
		QCOMPARE(groove.targetTickFor(last), last);

		// Extraction reads a slot's deviation the same wide way: a note at the
		// end of the timeline must not turn a slot's shift into garbage.
		NoteTable notes;
		notes.add(60, 0, 12, 100);
		notes.add(62, static_cast<int>(last), 12, 100);
		GrooveTemplate captured;
		int read = 0;
		QVERIFY(extractGroove(notes.vector(), QStringLiteral("end"), 48, 12, &captured, &read));
		QCOMPARE(read, 2);
		for (int slot = 0; slot < captured.slotCount(); ++slot)
		{
			QVERIFY2(captured.step(slot).timing >= -6 && captured.step(slot).timing <= 6,
				"a slot's shift came out of the arithmetic as garbage");
		}
	}

	//! A step saved at exactly half a slot (accepted before the half-slot bound)
	//! loads one tick inside its slot rather than being dropped; a step wider
	//! than half a slot was never valid and still leaves the slot neutral.
	void aSavedHalfSlotStepLoadsInsideItsSlot()
	{
		QDomDocument doc;
		QDomElement groove = doc.createElement(QStringLiteral("groove"));
		groove.setAttribute(QStringLiteral("name"), QStringLiteral("feel"));
		groove.setAttribute(QStringLiteral("length"), 48);
		groove.setAttribute(QStringLiteral("step"), 12);
		groove.setAttribute(QStringLiteral("slots"), 4);
		const auto step = [&doc, &groove](int slot, int timing) {
			QDomElement element = doc.createElement(QStringLiteral("step"));
			element.setAttribute(QStringLiteral("slot"), slot);
			element.setAttribute(QStringLiteral("timing"), timing);
			element.setAttribute(QStringLiteral("velocity"), 90);
			groove.appendChild(element);
		};
		step(0, 6);    // exactly +half
		step(1, -6);   // exactly -half
		step(2, 7);    // wider than half: never valid
		doc.appendChild(groove);

		GrooveTemplate loaded;
		QVERIFY(loaded.loadXml(groove));
		QCOMPARE(loaded.step(0), (GrooveStep{5, 90}));
		QCOMPARE(loaded.step(1), (GrooveStep{-5, 90}));
		QCOMPARE(loaded.step(2), GrooveStep());
	}

	//! A step whose slot TEXT is not a number is skipped, not read as slot zero.
	void aMalformedSlotTextIsNotSlotZero()
	{
		QDomDocument doc;
		QDomElement groove = doc.createElement(QStringLiteral("groove"));
		groove.setAttribute(QStringLiteral("name"), QStringLiteral("feel"));
		groove.setAttribute(QStringLiteral("length"), 48);
		groove.setAttribute(QStringLiteral("step"), 12);
		groove.setAttribute(QStringLiteral("slots"), 4);
		QDomElement malformed = doc.createElement(QStringLiteral("step"));
		malformed.setAttribute(QStringLiteral("slot"), QStringLiteral("bad"));
		malformed.setAttribute(QStringLiteral("timing"), 3);
		malformed.setAttribute(QStringLiteral("velocity"), 90);
		groove.appendChild(malformed);
		doc.appendChild(groove);

		GrooveTemplate loaded;
		QVERIFY(loaded.loadXml(groove));          // the geometry is still readable
		QCOMPARE(loaded.slotCount(), 4);
		// The entry names no slot, so it names no slot: slot zero is untouched
		// and the whole template says nothing about timing or velocity.
		QVERIFY2(loaded.step(0).neutral(), "a malformed slot text overwrote slot zero");
		QVERIFY2(loaded.neutral(), "a malformed slot text was applied to some slot");
	}

	/*! The RANDOM timing amount is pinned to the grid SLOT, not to the note's
	 *  current position - the same rule the humanise draw follows
	 *  (NoteTransform.cpp). So at full strength a repeat of the same call rolls
	 *  the same jitter and leaves every position where the first roll put it. */
	void theRandomTimingIsPinnedToTheSlot()
	{
		NoteTable source;
		fillFeelClip(&source);
		GrooveTemplate captured;
		QVERIFY(extractGroove(source.vector(), QStringLiteral("feel"), 48, 12, &captured, nullptr));

		NoteTable first;
		first.add(60, 0, 12, 100);
		first.add(62, 12, 12, 100);
		first.add(64, 24, 12, 100);
		first.add(65, 36, 12, 100);
		QVERIFY(applyGroove(first.vector(), captured, 1.0f, 2, 0, 12345u) > 0);
		const QVector<NoteAt> landed = takeOf(first.vector());
		QVERIFY2((landed != QVector<NoteAt>{{3, 100}, {9, 120}, {26, 80}, {34, 100}}),
			"the random amount did not roll a single note");

		QCOMPARE(applyGroove(first.vector(), captured, 1.0f, 2, 0, 12345u), 0);
		QVERIFY2(takeOf(first.vector()) == landed, qPrintable(describe(takeOf(first.vector()))));
	}

	//! Luna 3176: a strength that is not a NUMBER cannot be clamped - std::clamp
	//! hands a NaN back, and std::lround of a NaN has no result, so a note would
	//! be given a position or velocity out of thin air. NoteTransform::quantizeNotes
	//! already reads a NaN strength as 0 (NoteTransform.cpp:275); applyGroove has to
	//! say the same thing.
	void aStrengthThatIsNotANumberMovesNothing()
	{
		NoteTable source;
		fillFeelClip(&source);
		GrooveTemplate captured;
		QVERIFY(extractGroove(source.vector(), QStringLiteral("feel"), 48, 12, &captured, nullptr));

		const auto oneNote = []() {
			NoteTable table;
			table.add(62, 12, 12, 100);
			return table;
		};

		// The strength-0 early return in applyGroove is the deterministic probe.
		// `amount <= 0.0f` is false for a NaN (every NaN comparison is), so a NaN
		// slips past a guard that a plain 0 stops at. Against a NEUTRAL groove a
		// strength of 0 with a random amount is a guaranteed no-op, so anything the
		// NaN call changes is the NaN alone - no reliance on lround(NaN).
		GrooveTemplate neutral(QStringLiteral("straight"), 48, 12);
		QVERIFY(neutral.neutral());

		NoteTable zero = oneNote();
		const QVector<NoteAt> untouched = takeOf(zero.vector());
		QCOMPARE(applyGroove(zero.vector(), neutral, 0.0f, 6, 20, 12345u), 0);
		QVERIFY2(takeOf(zero.vector()) == untouched,
			qPrintable(describe(takeOf(zero.vector()))));

		NoteTable nan = oneNote();
		QVERIFY2(applyGroove(nan.vector(), neutral, std::numeric_limits<float>::quiet_NaN(),
			       6, 20, 12345u) == 0,
			"a NaN strength is not 0: it passed the amount <= 0 early return that a "
			"plain 0 strength stops at");
		QVERIFY2(takeOf(nan.vector()) == untouched,
			qPrintable(describe(takeOf(nan.vector()))));
	}
};

QTEST_GUILESS_MAIN(GrooveTemplateTest)
#include "GrooveTemplateTest.moc"
