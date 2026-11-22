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

#include <chrono>
#include <limits>
#include <type_traits>

#include <limits>

#include "ControlReversibility.h"
#include "DeprecationHelper.h"
#include "Engine.h"
#include "MicroTimer.h"
#include "ScriptClock.h"
#include "Song.h"

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

//! How many times the clock has actually RUN a hook body since it started -
//! fires + errors + busy_skips. A spurious edge is the defect, and every path
//! out of a fire moves one of the three (this suite runs no Lua engine, so the
//! run lands in errors or busy_skips, not in fires).
int hookRuns(const ScriptClock& clock)
{
	const QJsonObject counters = clock.stateJson().value(QStringLiteral("counters")).toObject();
	return counters.value(QStringLiteral("fires")).toInt()
		+ counters.value(QStringLiteral("errors")).toInt()
		+ counters.value(QStringLiteral("busy_skips")).toInt();
}

} // namespace

class ScriptClockTest : public QObject
{
	Q_OBJECT

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

	//! Elapsed microseconds must saturate before int overflows after about 36 minutes.
	void elapsedMicrosecondsSaturateAtIntMax();

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

	//! Regridding at the largest tick cannot represent a later boundary, so it
	//! must saturate instead of wrapping the stored next position.
	void maximumPositionSaturatesGrid();

	//! A crossing excess larger than int is reported with the bounded maximum.
	void coalescedCountSaturates();

	//! Degenerate strides (meter arithmetic that produced 0) detect nothing
	//! rather than dividing by zero.
	void degenerateStridesAreInert();

	//! The registry accepts a schedule, refuses the typed junk (unknown hook,
	//! empty source, oversized source, a source that does not compile), and
	//! reports it all back through stateJson - none of which needs an engine.
	void registryRefusalsAreTyped();

	//! The source cap counts UTF-8 BYTES, not UTF-16 code units: a multi-byte
	//! source inside the unit count but over the byte cap is refused (with the
	//! byte count in the message), and one inside the byte cap is accepted.
	void byteCapCountsBytes();

	//! The state reports `source_bytes` as the stored source's UTF-8 size, the
	//! same unit the cap is measured in, both per id and in stateJson.
	void scheduleStateReportsUtf8Bytes();

	//! Unique schedules stop at the registry limit, while a live edit still
	//! replaces an existing entry at the limit.
	void registryCountIsBounded();

	//! Re-scheduling the same id REPLACES the definition in place (the live
	//! edit) and keeps the id's place; unscheduling takes it off and the
	//! counters reset with the slot.
	void registryReplaceAndRemove();

	//! A transport schedule added while the song is ALREADY playing must not
	//! fire on the clock's first poll: the grid was never primed, so it read
	//! "stopped -> playing" as a play edge that never happened.
	void aScheduleAddedDuringPlaybackDoesNotFireAPlayEdge();

	//! Removing the last schedule stops the timer. A later schedule resumes
	//! polling, and must not fire bar/beat boundaries the song crossed while
	//! nothing was scheduled.
	void resumingAfterAnEmptyPeriodDoesNotFireStaleBoundaries();

	//! A script's own log text must not retype its failure: a scheduled script
	//! that prints the engine's budget wording and then fails for an unrelated
	//! reason is a "script" failure, not a "budget" one - the KIND comes from
	//! the raw run error, while the folded lastError still shows the log line.
	void aLogLineCannotMasqueradeAsABudgetFailure();

	//! The contract table classes the group's three ids the way the card
	//! demands: two snapshots with their inverse named, one read that mutates
	//! nothing. Read out of the table itself, not restated from this file.
	void a16RowsCarryTheGroup();
};

void ScriptClockTest::elapsedMicrosecondsSaturateAtIntMax()
{
	static_assert(std::is_standard_layout_v<MicroTimer>);
	MicroTimer timer;
	using TimePoint = std::chrono::steady_clock::time_point;
	// The standard-layout timer's first member is its start time.
	const auto start = std::chrono::steady_clock::now() - std::chrono::minutes{37};
	*reinterpret_cast<TimePoint*>(&timer) = start;
	QCOMPARE(timer.elapsed(), std::numeric_limits<int>::max());
}

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

void ScriptClockTest::maximumPositionSaturatesGrid()
{
	ScriptClockGrid grid;
	const qint64 maximum = std::numeric_limits<qint64>::max();

	const ScriptTickEvents atLimit = grid.advance(true, maximum, 1, 1);
	QCOMPARE(atLimit.bars, 0);
	QCOMPARE(atLimit.beats, 0);
	QCOMPARE(grid.nextBar(), maximum);
	QCOMPARE(grid.nextBeat(), maximum);
}

void ScriptClockTest::coalescedCountSaturates()
{
	ScriptClockGrid grid;
	grid.advance(true, 0, 1, 1);

	const ScriptTickEvents jump = grid.advance(true, std::numeric_limits<qint64>::max(), 1, 1);
	QCOMPARE(jump.bars, ScriptClockGrid::CrossCap);
	QCOMPARE(jump.beats, ScriptClockGrid::CrossCap);
	QCOMPARE(jump.coalesced, std::numeric_limits<int>::max());
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

void ScriptClockTest::byteCapCountsBytes()
{
	ScriptClock& clock = ScriptClock::instance();
	QString error;

	// A Lua comment padded with 2-byte characters: 40003 UTF-16 code units
	// (inside the cap) but 80003 UTF-8 bytes (outside it). Counting units let
	// this through; the cap and its message are in bytes.
	const QString oversized = QStringLiteral("-- ") + QString(40000, QChar(0x00E9));
	QVERIFY2(!clock.setSchedule(QStringLiteral("lc-t-bytes"), oversized,
		QStringLiteral("bar"), 0, &error),
		"a source over the byte cap must be refused");
	QVERIFY(error.contains(QStringLiteral("capped at")));
	QVERIFY(error.contains(QStringLiteral("80003")));

	// A multi-byte source that fits inside the BYTE cap is still accepted.
	const QString fits = QStringLiteral("-- ") + QString(30000, QChar(0x00E9)); // 60003 bytes
	QVERIFY2(clock.setSchedule(QStringLiteral("lc-t-bytes-ok"), fits,
		QStringLiteral("bar"), 0, &error), qPrintable(error));
	QVERIFY(clock.hasSchedule(QStringLiteral("lc-t-bytes-ok")));
	QVERIFY2(clock.removeSchedule(QStringLiteral("lc-t-bytes-ok"), &error), qPrintable(error));
}

void ScriptClockTest::scheduleStateReportsUtf8Bytes()
{
	ScriptClock& clock = ScriptClock::instance();
	QString error;

	// U+00E9 is one UTF-16 unit but two UTF-8 bytes, so the source is 13 units
	// and 14 bytes: a state that counts units reports the wrong size.
	const QString source = QStringLiteral("-- ") + QChar(0x00E9) + QStringLiteral("\nreturn 1");
	const qint64 expected = source.toUtf8().size();
	QVERIFY(expected != source.size());
	const QString id = QStringLiteral("lc-t-state-bytes");
	QVERIFY2(clock.setSchedule(id, source, QStringLiteral("bar"), 0, &error), qPrintable(error));

	// Read everything first and unschedule before any assertion, so a failure
	// here cannot leave the id registered for the tests that follow.
	const qint64 perId = jsonInteger(clock.scheduleState(id).value(QStringLiteral("source_bytes")), -1);
	qint64 inState = -1;
	const QJsonArray schedules = clock.stateJson().value(QStringLiteral("schedules")).toArray();
	for (const QJsonValue& value : schedules)
	{
		const QJsonObject one = value.toObject();
		if (one.value(QStringLiteral("id")).toString() != id) { continue; }
		inState = jsonInteger(one.value(QStringLiteral("source_bytes")), -1);
	}
	QVERIFY2(clock.removeSchedule(id, &error), qPrintable(error));

	QCOMPARE(perId, expected);
	QVERIFY2(inState != -1, "stateJson must list the scheduled id");
	QCOMPARE(inState, expected);
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

void ScriptClockTest::aScheduleAddedDuringPlaybackDoesNotFireAPlayEdge()
{
	Song* song = Engine::getSong();
	QVERIFY(song != nullptr);
	ScriptClock& clock = ScriptClock::instance();
	QString error;

	// Nothing scheduled, nothing playing: the grid is parked and believes the
	// transport is stopped. That is the state a script inherits when it adds
	// its transport hook to a song that is ALREADY rolling.
	QCOMPARE(clock.scheduleCount(), 0);
	clock.pollOnce();
	QVERIFY(!clock.grid().playing());

	song->playSong();
	QVERIFY(song->isPlaying());

	QVERIFY2(clock.setSchedule(QStringLiteral("lc-t-edge"), QStringLiteral("return 1"),
		QStringLiteral("transport"), 0, &error), qPrintable(error));
	const int before = hookRuns(clock);
	clock.pollOnce();
	QCOMPARE(hookRuns(clock) - before, 0);

	song->stop();
	QVERIFY2(clock.removeSchedule(QStringLiteral("lc-t-edge"), &error), qPrintable(error));
}

void ScriptClockTest::resumingAfterAnEmptyPeriodDoesNotFireStaleBoundaries()
{
	Song* song = Engine::getSong();
	QVERIFY(song != nullptr);
	ScriptClock& clock = ScriptClock::instance();
	QString error;

	song->playSong();
	QVERIFY(song->isPlaying());
	QVERIFY2(clock.setSchedule(QStringLiteral("lc-t-bar"), QStringLiteral("return 1"),
		QStringLiteral("bar"), 0, &error), qPrintable(error));
	clock.pollOnce();   // anchors the grid where the play head IS

	// Removing the last schedule stops the timer. The grid keeps its anchor.
	QVERIFY2(clock.removeSchedule(QStringLiteral("lc-t-bar"), &error), qPrintable(error));
	QVERIFY(!clock.hasSchedule(QStringLiteral("lc-t-bar")));

	// The song plays on for five bars with nothing scheduled to notice.
	song->setPlayPos(song->getPlayPos().getTicks() + 192 * 5);

	QVERIFY2(clock.setSchedule(QStringLiteral("lc-t-bar2"), QStringLiteral("return 1"),
		QStringLiteral("bar"), 0, &error), qPrintable(error));
	const int before = hookRuns(clock);
	clock.pollOnce();
	QCOMPARE(hookRuns(clock) - before, 0);

	song->stop();
	QVERIFY2(clock.removeSchedule(QStringLiteral("lc-t-bar2"), &error), qPrintable(error));
}

void ScriptClockTest::aLogLineCannotMasqueradeAsABudgetFailure()
{
	Song* song = Engine::getSong();
	QVERIFY(song != nullptr);
	ScriptClock& clock = ScriptClock::instance();
	QString error;

	// The script LOGS the engine's own budget wording, then fails for an
	// unrelated reason. The read-back must still fold the log line into
	// last_error, but the KIND and the budget_exceeded counter must come
	// from the raw run error, not from the script's text.
	QVERIFY2(clock.setSchedule(QStringLiteral("lc-t-masq"), QStringLiteral(
		"print('instruction budget exceeded') error('unrelated boom')"),
		"transport", 0, &error), qPrintable(error));

	// Fire exactly once: park the grid, start the transport, poll the edge.
	clock.pollOnce();
	song->playSong();
	clock.pollOnce();

	const QJsonObject entry = entryFor(clock.stateJson(), QStringLiteral("lc-t-masq"));
	QCOMPARE(entry.value(QStringLiteral("errors")).toInt(), 1);
	// A failed dispatch is also a completed one: `fires` counts every dispatch
	// that completed (Ok or an error) and `errors` the failed subset, as
	// ScriptClock.h documents. Pinned so the overlap is not "fixed" away.
	QCOMPARE(entry.value(QStringLiteral("fires")).toInt(), 1);
	QCOMPARE(entry.value(QStringLiteral("budget_exceeded")).toInt(), 0);
	QCOMPARE(entry.value(QStringLiteral("last_error_kind")).toString(),
		QStringLiteral("script"));
	const QString folded = entry.value(QStringLiteral("last_error")).toString();
	QVERIFY2(folded.contains(QLatin1String("unrelated boom")),
		"the failure itself must be in the read-back");
	QVERIFY2(folded.contains(QLatin1String("instruction budget exceeded")),
		"the script's log line must still be folded into last_error");

	song->stop();
	QVERIFY2(clock.removeSchedule(QStringLiteral("lc-t-masq"), &error), qPrintable(error));
}

void ScriptClockTest::registryCountIsBounded()
{
	constexpr int ExpectedMaxSchedules = 64;
	ScriptClock& clock = ScriptClock::instance();
	QString error;
	QCOMPARE(clock.scheduleCount(), 0);

	for (int index = 0; index < ExpectedMaxSchedules; ++index)
	{
		error.clear();
		QVERIFY2(clock.setSchedule(QStringLiteral("lc-t-cap-%1").arg(index),
			QStringLiteral("return 1"), QStringLiteral("bar"), 0, &error), qPrintable(error));
	}

	// Replacing a live id does not consume another slot.
	error.clear();
	QVERIFY2(clock.setSchedule(QStringLiteral("lc-t-cap-0"), QStringLiteral("return 2"),
		QStringLiteral("beat"), 0, &error), qPrintable(error));
	QCOMPARE(clock.scheduleCount(), ExpectedMaxSchedules);

	error.clear();
	const QString overflowId = QStringLiteral("lc-t-cap-overflow");
	const bool accepted = clock.setSchedule(overflowId, QStringLiteral("return 1"),
		QStringLiteral("bar"), 0, &error);
	const QString overflowError = error;
	if (accepted)
	{
		error.clear();
		QVERIFY2(clock.removeSchedule(overflowId, &error), qPrintable(error));
	}
	for (int index = 0; index < ExpectedMaxSchedules; ++index)
	{
		error.clear();
		QVERIFY2(clock.removeSchedule(QStringLiteral("lc-t-cap-%1").arg(index), &error), qPrintable(error));
	}

	QVERIFY2(!accepted, qPrintable(overflowError));
	QVERIFY(overflowError.contains(QStringLiteral("maximum number of scheduled scripts")));
	QCOMPARE(clock.scheduleCount(), 0);
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
