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
		// Half a slot is the largest timing offset a step may carry: past it a
		// note would belong to a different slot, which is a rearrangement. A
		// velocity is a velocity, so the only "smaller than 0" it knows is the
		// explicit "no opinion".
		QVERIFY(groove.setStep(0, GrooveStep{6, 200}));
		QVERIFY(!groove.setStep(0, GrooveStep{7, 0}));
		QVERIFY(!groove.setStep(0, GrooveStep{0, static_cast<int>(MaxVolume) + 1}));
		QVERIFY(!groove.setStep(0, GrooveStep{0, -2}));
		QVERIFY(!groove.setStep(4, GrooveStep{0, 0}));
		QCOMPARE(groove.step(0), (GrooveStep{6, 200}));   // the refused writes did nothing
		QVERIFY(!groove.neutral());
		QVERIFY(groove.setStep(0, GrooveStep{0, GrooveStep::NoVelocityOpinion}));
		QVERIFY(groove.step(0).neutral());
		QVERIFY(groove.setStep(0, GrooveStep{6, 200}));
		QVERIFY(groove.step(0) == (GrooveStep{6, 200}));
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
};

QTEST_GUILESS_MAIN(GrooveTemplateTest)
#include "GrooveTemplateTest.moc"
