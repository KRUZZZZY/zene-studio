/*
 * NoteGridTest.cpp - the note editor's geometry, folding and snapping, without a display
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
 *
 */

#include <QtTest>

#include <limits>

#include "NoteGrid.h"
#include "NoteLanes.h"
#include "TempoMap.h"

using lmms::gui::NoteFold;
using lmms::gui::NoteGrid;
using lmms::gui::NoteLane;

namespace
{

//! A metre change at song tick @a tick.
lmms::TempoMapEvent metreAt(int tick, int numerator, int denominator)
{
	lmms::TempoMapEvent event;
	event.tick = tick;
	event.hasTimeSignature = true;
	event.numerator = numerator;
	event.denominator = denominator;
	return event;
}

//! The start ticks of @a bars.
QList<int> startsOf(const std::vector<NoteGrid::Bar>& bars)
{
	QList<int> starts;
	for (const NoteGrid::Bar& bar : bars) { starts << bar.tick; }
	return starts;
}

} // namespace

class NoteGridTest : public QObject
{
	Q_OBJECT

private slots:
	//! 0.5.0 W2: without a metre the grid is 4/4, as it always was.
	void theGridIsFourFourByDefault()
	{
		NoteGrid g;
		QCOMPARE(startsOf(g.barsBetween(100, 600)), (QList<int>{0, 192, 384, 576}));
		QCOMPARE(g.barsBetween(100, 600).back().index, 3);
		QCOMPARE(g.beatTicksAt(500), 48);
	}

	//! 0.5.0 W2: the bars follow the song's time signature, not 4/4.
	void theBarsFollowTheSongsMetre()
	{
		NoteGrid g;
		g.setMeter(lmms::TempoMapTimeSignature{3, 4}, lmms::TempoMap{}, 0);
		QCOMPARE(startsOf(g.barsBetween(0, 500)), (QList<int>{0, 144, 288, 432}));
		g.setMeter(lmms::TempoMapTimeSignature{6, 8}, lmms::TempoMap{}, 0);
		const std::vector<NoteGrid::Bar> bars = g.barsBetween(0, 300);
		QCOMPARE(startsOf(bars), (QList<int>{0, 144, 288}));
		QCOMPARE(bars.front().beatTicks, 24);  // an eighth
		QCOMPARE(g.beatTicksAt(200), 24);
	}

	//! 0.5.0 W2: the tempo map's metre changes start bars at their own tick, seen from the clip's start;
	//! a bar a change cuts short still counts, and an inactive map changes nothing.
	void theTempoMapsMetreChangesStartBars()
	{
		lmms::TempoMap map;
		QVERIFY(map.addEvent(metreAt(192, 3, 4)));   // a change before the clip: the clip starts in 3/4
		QVERIFY(map.addEvent(metreAt(864, 7, 8)));   // in the clip, mid-bar of 3/4 (at clip tick 480)
		NoteGrid g;
		g.setMeter(lmms::TempoMapTimeSignature{4, 4}, map, 384);
		QCOMPARE(g.meter().size(), std::size_t{1});  // inactive: the song's 4/4 throughout
		QCOMPARE(startsOf(g.barsBetween(0, 400)), (QList<int>{0, 192, 384}));
		map.setActive(true);
		g.setMeter(lmms::TempoMapTimeSignature{4, 4}, map, 384);
		const std::vector<NoteGrid::Bar> bars = g.barsBetween(0, 700);
		QCOMPARE(startsOf(bars), (QList<int>{0, 144, 288, 432, 480, 648}));
		QCOMPARE(bars[3].length, 48);    // cut short by the change
		QCOMPARE(bars[4].index, 4);      // the cut bar counted: the change starts bar 5
		QCOMPARE(bars[4].barTicks, 168);
		QCOMPARE(g.beatTicksAt(479), 48);
		QCOMPARE(g.beatTicksAt(480), 24);
		QCOMPARE(startsOf(g.barsBetween(500, 700)), (QList<int>{480, 648}));  // the bar holding the start too
	}

	void ticksAndPixelsRoundTrip()
	{
		NoteGrid g;
		g.setPixelsPerTick(0.5);
		g.setScrollTicks(96);
		QCOMPARE(g.xOfTick(96), 0.0);
		QCOMPARE(g.xOfTick(192), 48.0);
		QCOMPARE(g.tickAtX(48), 192.0);
	}

	void zoomKeepsTheTickUnderThePointer()
	{
		NoteGrid g;
		g.setPixelsPerTick(0.25);
		g.setScrollTicks(100);
		const double anchorX = 300;
		const double before = g.tickAtX(anchorX);
		g.zoomAround(1.37, anchorX);
		QVERIFY(qAbs(g.tickAtX(anchorX) - before) < 1e-6);
		g.zoomAround(1e9, anchorX);
		QCOMPARE(g.pixelsPerTick(), NoteGrid::maxPixelsPerTick);
		g.zoomAround(1e-9, anchorX);
		QCOMPARE(g.pixelsPerTick(), NoteGrid::minPixelsPerTick);
	}

	void nonFiniteZoomAndScrollValuesAreIgnored()
	{
		NoteGrid g;
		g.setPixelsPerTick(0.5);
		g.setScrollTicks(96);
		const double infinity = std::numeric_limits<double>::infinity();
		const double nan = std::numeric_limits<double>::quiet_NaN();

		for (double value : {nan, infinity, -infinity})
		{
			g.setPixelsPerTick(value);
			QCOMPARE(g.pixelsPerTick(), 0.5);
			g.setScrollTicks(value);
			QCOMPARE(g.scrollTicks(), 96.0);
		}
	}

	void rowsRunFromTheHighestKeyDown()
	{
		NoteGrid g;
		QCOMPARE(g.rowCount(), 128);
		QCOMPARE(g.rows().front(), 127);
		QCOMPARE(g.rowOfKey(127), 0);
		QCOMPARE(g.rowOfKey(0), 127);
		g.setRowHeight(64);
		QCOMPARE(g.rowHeight(), 40);  // the larger cap belongs to accessible mode only
		g.setRowHeight(10);
		g.setViewport(400, 300);
		QCOMPARE(g.keyAtY(5), 127);
		QCOMPARE(g.yOfKey(126), 10);
		g.setScrollY(20);
		QCOMPARE(g.keyAtY(5), 125);
		QCOMPARE(g.keyAtY(-500), -1);
	}

	void foldingToTheScaleHidesTheOtherKeys()
	{
		NoteGrid g;
		const std::array<bool, 12> major{true, false, true, false, true, true, false, true, false, true, false, true};
		g.setScaleMask(major, 0);
		g.setFold(NoteFold::Scale);
		QCOMPARE(g.rowCount(), 75);  // 7 of every 12 keys over 128: 10 octaves x 7 + C..G of the 11th
		QVERIFY(g.rowOfKey(61) < 0);  // C#
		QVERIFY(g.rowOfKey(60) >= 0);
		QVERIFY(g.inScale(64));
		QVERIFY(!g.inScale(66));
	}

	void foldingToUsedKeysShowsOnlyThose()
	{
		NoteGrid g;
		g.setUsedKeys({60, 64, 67, 64});
		g.setFold(NoteFold::Used);
		QCOMPARE(g.rowCount(), 3);
		QCOMPARE(g.rows().front(), 67);
		g.setUsedKeys({});
		// Nothing to fold to: every key is shown rather than an empty editor.
		QCOMPARE(g.rowCount(), 128);
		QCOMPARE(g.fold(), NoteFold::Used);
	}

	void foldingKeepsTheCentreKeyCentred()
	{
		NoteGrid g;
		g.setRowHeight(10);
		g.setViewport(400, 200);
		g.centreOnKey(64);
		QCOMPARE(g.keyAtY(100), 64);
		g.setUsedKeys({60, 64, 67});
		g.setFold(NoteFold::Used);  // three rows: nothing to scroll, the key is still shown
		QVERIFY(g.rowOfKey(64) >= 0);
		g.setFold(NoteFold::None);
		// Unfolding lands back where the user was - centred on the used key nearest the view's
		// centre - never at the top of the keyboard: 64 is on screen and 127 is not.
		QVERIFY(g.keyAtY(0) >= 64 && g.keyAtY(199) <= 64);
		QVERIFY(g.keyAtY(0) < 100);
	}

	void snappingRoundsAndFloors()
	{
		QCOMPARE(NoteGrid::snap(17, 12), 12);
		QCOMPARE(NoteGrid::snap(19, 12), 24);
		QCOMPARE(NoteGrid::snapDown(23, 12), 12);
		QCOMPARE(NoteGrid::snap(-7, 12), -12);
		QCOMPARE(NoteGrid::snap(17, 1), 17);
	}

	void theDrawnGridThinsOutWhenZoomedOut()
	{
		NoteGrid g;
		g.setPixelsPerTick(1.0);
		QCOMPARE(g.visibleGridTicks(12), 12);
		g.setPixelsPerTick(0.05);
		QVERIFY(g.visibleGridTicks(12) * 0.05 >= 6);
	}

	void keyNamesFollowTheEngine()
	{
		QCOMPARE(NoteGrid::keyName(57), QStringLiteral("A4"));
		QCOMPARE(NoteGrid::keyName(60), QStringLiteral("C5"));
		QVERIFY(NoteGrid::isBlackKey(61));
		QVERIFY(!NoteGrid::isBlackKey(64));
	}

	void lanesAreUniqueAndRoundTrip()
	{
		QSet<QString> ids;
		for (const auto& spec : lmms::gui::notelanes::all())
		{
			QVERIFY(!ids.contains(spec.id));
			ids.insert(spec.id);
			NoteLane lane = NoteLane::Velocity;
			QVERIFY(lmms::gui::notelanes::fromId(spec.id, lane));
			QCOMPARE(lane, spec.lane);
			QVERIFY(spec.minimum < spec.maximum);
		}
		QCOMPARE(lmms::gui::notelanes::clamp(NoteLane::Pan, 500), 100);
		QCOMPARE(lmms::gui::notelanes::clamp(NoteLane::Velocity, -3), 0);
	}

	void eachLaneWritesThroughOneCommand()
	{
		using lmms::gui::notelanes::writeFor;
		const auto velocity = writeFor(NoteLane::Velocity, "clip-1", "note-2", 140);
		QCOMPARE(velocity.command, QStringLiteral("note.edit"));
		QCOMPARE(velocity.args.value("edits").toArray().at(0).toObject().value("velocity").toInt(), 140);
		const auto chance = writeFor(NoteLane::Chance, "clip-1", "note-2", 50);
		QCOMPARE(chance.args.value("edits").toArray().at(0).toObject().value("probability").toDouble(), 0.5);
		const auto timbre = writeFor(NoteLane::Timbre, "clip-1", "note-2", 999);
		QCOMPARE(timbre.command, QStringLiteral("note.expression_set"));
		QCOMPARE(timbre.args.value("timbre").toInt(), 127);  // clamped to the lane
		QCOMPARE(writeFor(NoteLane::Pitch, "clip-1", "note-2", -50).args.value("pitch").toInt(), -50);
	}
};

QTEST_GUILESS_MAIN(NoteGridTest)
#include "NoteGridTest.moc"
