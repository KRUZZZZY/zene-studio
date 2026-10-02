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

#include "NoteGrid.h"
#include "NoteLanes.h"

using lmms::gui::NoteFold;
using lmms::gui::NoteGrid;
using lmms::gui::NoteLane;

class NoteGridTest : public QObject
{
	Q_OBJECT

private slots:
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

	void rowsRunFromTheHighestKeyDown()
	{
		NoteGrid g;
		QCOMPARE(g.rowCount(), 128);
		QCOMPARE(g.rows().front(), 127);
		QCOMPARE(g.rowOfKey(127), 0);
		QCOMPARE(g.rowOfKey(0), 127);
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
