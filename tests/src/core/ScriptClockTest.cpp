/*
 * ScriptClockTest.cpp - the pure half of scheduled Lua evaluation (board card
 *                       #708): the boundary grid, the schedule registry and the
 *                       A16 rows the livecode.* group carries.
 *
 * What this file can prove WITHOUT an audio device or a lua run: the three
 * hooks' boundary rules (edges, crossings, seeks, the per-poll cap) on
 * synthetic transport observations fed to the same ScriptClockGrid the clock
 * drives, the registry's schedule-time refusals (a compile check happens in
 * setSchedule itself, on a scratch state - no engine involved), and that the
 * contract table classes the group's three ids the way the card demands. What
 * it deliberately does NOT prove - the fire running real Lua through the
 * worker, the budget bound, the live edits on every bar - is the registered
 * transcript's half: tests/control-livecode-commands.py (ctest
 * ControlLivecodeCommands).
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program (see COPYING); if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "ControlReversibility.h"
#include "ScriptClock.h"

using namespace lmms;

namespace
{

//! One stopped observation: the grid parks and reports nothing.
void park(ScriptClockGrid& grid)
{
	grid.advance(false, 0, 192, 48);
}

//! The schedule \a id in a livecode.get_state-shaped payload, or {} when the
//! payload does not carry it (the caller then fails on the empty object).
QJsonObject entryFor(const QJsonObject& state, const QString& id)
{
	const QJsonArray schedules = state.value(QStringLiteral("schedules")).toArray();
	for (const QJsonValue& value : schedules)
	{
		const QJsonObject one = value.toObject();
		if (one.value(QStringLiteral("id")).toString() == id) { return one; }
	}
	return QJsonObject();
}

} // namespace

class ScriptClockTest : public QObject
{
	Q_OBJECT

private slots:
	//! A transport hook's half: the EDGE is detected from the flag, from a
	//! parked grid and from a playing one, with no bar or beat crossing
	//! smuggled in with it.
	void playAndStopEdges();

	//! A bar fires when the observation interval (last, now] CONTAINS a bar
	//! boundary - not when the position merely moves.
	void barAndBeatCrossings();

	//! A seek backwards re-anchors the grid where playback is and fires
	//! nothing: a seek is not a boundary playback crossed.
	void seekBackwardsRegridsWithoutFiring();

	//! A meter change re-anchors the strides (the old next-bar tick is not a
	//! bar in the new meter) and fires nothing.
	void meterChangeRegridsWithoutFiring();

	//! More than CrossCap crossings in ONE observation - a big seek, or a
	//! control thread blocked inside a long script.run - fire the cap and
	//! count the rest as coalesced instead of firing a thousand stale bars.
	void overshootCapCoalesces();

	//! Degenerate strides (meter arithmetic that produced 0) detect nothing
	//! rather than dividing by zero.
	void degenerateStridesAreInert();

	//! The registry accepts a schedule, refuses the typed junk (unknown hook,
	//! empty source, oversized source, a source that does not compile), and
	//! reports it all back through stateJson - none of which needs an engine.
	void registryRefusalsAreTyped();

	//! Re-scheduling the same id REPLACES the definition in place (the live
	//! edit) and keeps the id's place; unscheduling takes it off and the
	//! counters reset with the slot.
	void registryReplaceAndRemove();

	//! The contract table classes the group's three ids the way the card
	//! demands: two snapshots with their inverse named, one read that mutates
	//! nothing. Read out of the table itself, not restated from this file.
	void a16RowsCarryTheGroup();
};

void ScriptClockTest::playAndStopEdges()
{
	ScriptClockGrid grid;
	park(grid);

	const ScriptTickEvents started = grid.advance(true, 0, 192, 48);
	QVERIFY(started.playEdge);
	QVERIFY(!started.stopEdge);
	QCOMPARE(started.bars, 0);
	QCOMPARE(started.beats, 0);
	QCOMPARE(started.regrids, 1);

	const ScriptTickEvents playing = grid.advance(true, 40, 192, 48);
	QVERIFY(!playing.playEdge);
	QVERIFY(!playing.stopEdge);
	QCOMPARE(playing.bars, 0);

	const ScriptTickEvents stopped = grid.advance(false, 40, 192, 48);
	QVERIFY(stopped.stopEdge);
	QVERIFY(!stopped.playEdge);
	QCOMPARE(stopped.bars, 0);
	QCOMPARE(stopped.beats, 0);
}

void ScriptClockTest::barAndBeatCrossings()
{
	ScriptClockGrid grid;
	park(grid);
	grid.advance(true, 0, 192, 48);   // regrid at 0: next bar 192, next beat 48

	// Inside the first bar: beats may cross, bars may not.
	const ScriptTickEvents early = grid.advance(true, 49, 192, 48);
	QCOMPARE(early.beats, 1);         // crossed 48
	QCOMPARE(early.bars, 0);

	// Up to and just past the bar line: exactly one bar.
	const ScriptTickEvents barLine = grid.advance(true, 193, 192, 48);
	QCOMPARE(barLine.bars, 1);        // crossed 192
	QCOMPARE(barLine.beats, 3);       // crossed 96, 144, 192

	// Three more bars in one observation. (The beat stride is small enough
	// that this step also overshoots the BEAT cap - coalescing is asserted on
	// its own below - so only the bar side is pinned here.)
	const ScriptTickEvents run = grid.advance(true, 192 + 3 * 192, 192, 48);
	QCOMPARE(run.bars, 3);
	QCOMPARE(grid.nextBar(), static_cast<qint64>(192 + 4 * 192));
}

void ScriptClockTest::seekBackwardsRegridsWithoutFiring()
{
	ScriptClockGrid grid;
	park(grid);
	grid.advance(true, 500, 192, 48);       // playing, well past bar 2

	const ScriptTickEvents seek = grid.advance(true, 10, 192, 48);
	QCOMPARE(seek.bars, 0);
	QCOMPARE(seek.beats, 0);
	QVERIFY(seek.regrids >= 1);
	QCOMPARE(grid.nextBar(), static_cast<qint64>(192));

	// After the seek the next real boundary still fires.
	const ScriptTickEvents after = grid.advance(true, 193, 192, 48);
	QCOMPARE(after.bars, 1);
}

void ScriptClockTest::meterChangeRegridsWithoutFiring()
{
	ScriptClockGrid grid;
	park(grid);
	grid.advance(true, 0, 192, 48);

	const ScriptTickEvents changed = grid.advance(true, 100, 288, 72);
	QCOMPARE(changed.bars, 0);
	QCOMPARE(changed.beats, 0);
	QCOMPARE(changed.regrids, 1);
	QCOMPARE(grid.nextBar(), static_cast<qint64>(288));
}

void ScriptClockTest::overshootCapCoalesces()
{
	ScriptClockGrid grid;
	park(grid);
	grid.advance(true, 0, 192, 48);

	// Ten bars in one observation: the cap is CrossCap, the grid re-anchors
	// where playback landed, and the dropped bar crossings are counted (the
	// shared counter also holds the beat crossings this jump overshot - at
	// LEAST the ten-bar excess must be in it).
	const ScriptTickEvents jump = grid.advance(true, 10 * 192 + 5, 192, 48);
	QCOMPARE(jump.bars, ScriptClockGrid::CrossCap);
	QVERIFY(jump.coalesced >= 10 - ScriptClockGrid::CrossCap);
	QCOMPARE(grid.nextBar(), static_cast<qint64>(11 * 192));
}

void ScriptClockTest::degenerateStridesAreInert()
{
	ScriptClockGrid grid;
	park(grid);
	grid.advance(true, 50, 192, 48);

	const ScriptTickEvents zero = grid.advance(true, 400, 0, 48);
	QCOMPARE(zero.bars, 0);
	QCOMPARE(zero.beats, 0);
}

void ScriptClockTest::registryRefusalsAreTyped()
{
	ScriptClock& clock = ScriptClock::instance();
	QString error;

	QVERIFY2(!clock.setSchedule(QStringLiteral("lc-t-empty"), QString{},
		QStringLiteral("bar"), 0, &error), qPrintable(error));
	QVERIFY(error.contains(QStringLiteral("empty")));

	error.clear();
	QVERIFY2(!clock.setSchedule(QStringLiteral("lc-t-hook"), QStringLiteral("return 1"),
		QStringLiteral("thursday"), 0, &error), qPrintable(error));
	QVERIFY(error.contains(QStringLiteral("unknown hook")));

	error.clear();
	QVERIFY2(!clock.setSchedule(QStringLiteral("lc-t-big"), QString(ScriptClock::SourceByteCap + 1,
		QLatin1Char('x')), QStringLiteral("bar"), 0, &error), qPrintable(error));
	QVERIFY(error.contains(QStringLiteral("capped at")));

	error.clear();
	QVERIFY2(!clock.setSchedule(QStringLiteral("lc-t-parse"), QStringLiteral("if then end"),
		QStringLiteral("bar"), 0, &error), qPrintable(error));
	QVERIFY(error.contains(QStringLiteral("does not compile")));

	// The good path, and what stateJson reports of it.
	error.clear();
	QVERIFY2(clock.setSchedule(QStringLiteral("lc-t-ok"), QStringLiteral("return 1"),
		QStringLiteral("beat"), 1000, &error), qPrintable(error));
	QVERIFY(clock.hasSchedule(QStringLiteral("lc-t-ok")));
	const QJsonObject state = clock.stateJson();
	QCOMPARE(state.value(QStringLiteral("poll_ms")).toInt(), ScriptClock::PollMs);
	const QJsonObject entry = entryFor(state, QStringLiteral("lc-t-ok"));
	QCOMPARE(entry.value(QStringLiteral("hook")).toString(), QStringLiteral("beat"));
	QCOMPARE(entry.value(QStringLiteral("budget")).toInt(), 1000);

	QVERIFY2(clock.removeSchedule(QStringLiteral("lc-t-ok"), &error), qPrintable(error));
	QVERIFY(!clock.hasSchedule(QStringLiteral("lc-t-ok")));
	QVERIFY2(!clock.removeSchedule(QStringLiteral("lc-t-ok"), &error),
		"unscheduling an id that is not on the clock must refuse, typed");
	QVERIFY(error.contains(QStringLiteral("no scheduled script")));
}

void ScriptClockTest::registryReplaceAndRemove()
{
	ScriptClock& clock = ScriptClock::instance();
	QString error;

	QVERIFY(clock.setSchedule(QStringLiteral("lc-t-r"), QStringLiteral("return 'first'"),
		QStringLiteral("bar"), 0, &error));
	const QJsonObject first = clock.scheduleState(QStringLiteral("lc-t-r"));
	QCOMPARE(first.value(QStringLiteral("source")).toString(), QStringLiteral("return 'first'"));

	// The live edit: same id, new source/hook/budget, one slot.
	QVERIFY(clock.setSchedule(QStringLiteral("lc-t-r"), QStringLiteral("return 'second'"),
		QStringLiteral("transport"), 7, &error));
	QCOMPARE(clock.scheduleCount(), 1);
	const QJsonObject second = clock.scheduleState(QStringLiteral("lc-t-r"));
	QCOMPARE(second.value(QStringLiteral("source")).toString(), QStringLiteral("return 'second'"));
	QCOMPARE(second.value(QStringLiteral("hook")).toString(), QStringLiteral("transport"));
	QCOMPARE(second.value(QStringLiteral("budget")).toInt(), 7);

	QVERIFY(clock.removeSchedule(QStringLiteral("lc-t-r"), &error));
	QVERIFY(!clock.hasSchedule(QStringLiteral("lc-t-r")));
	QVERIFY2(clock.scheduleState(QStringLiteral("lc-t-r")).isEmpty(),
		"a removed schedule must report no state, so an undo's before-state cannot lie");
}

void ScriptClockTest::a16RowsCarryTheGroup()
{
	const control::ReversibilityTable& table = control::ReversibilityTable::instance();

	const control::ReversibilityEntry* schedule = table.lookup(QStringLiteral("livecode.schedule"));
	QVERIFY(schedule != nullptr);
	QCOMPARE(schedule->cls, control::ReversibilityClass::Snapshot);
	QVERIFY(schedule->reversible);
	QVERIFY2(schedule->mechanism.contains(QStringLiteral("control.undo")),
		qPrintable(schedule->mechanism));

	const control::ReversibilityEntry* unschedule = table.lookup(QStringLiteral("livecode.unschedule"));
	QVERIFY(unschedule != nullptr);
	QCOMPARE(unschedule->cls, control::ReversibilityClass::Snapshot);
	QVERIFY(unschedule->reversible);

	const control::ReversibilityEntry* state = table.lookup(QStringLiteral("livecode.get_state"));
	QVERIFY(state != nullptr);
	QCOMPARE(state->cls, control::ReversibilityClass::NotMutating);
	QVERIFY(!state->reversible);
}

QTEST_GUILESS_MAIN(ScriptClockTest)
#include "ScriptClockTest.moc"
