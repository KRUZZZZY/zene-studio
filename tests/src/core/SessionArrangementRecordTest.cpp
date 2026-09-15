/*
 * SessionArrangementRecordTest.cpp - Arrangement Record's ring, and the
 *                                     engine-driven behaviour that feeds it
 *                                     (task #641, the #596 engine half).
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

//! WHAT THIS FILE PROVES. Arrangement Record is a performance tap: the audio
//! thread pushes one event per launch and per stop into a bounded ring
//! (include/SessionArrangementRecorder.h) and the model thread lands it. So this
//! file drives the ring directly (arming, the bounded drop, the NON-destructive
//! snapshot a land pass decides on, disarm-keeps-the-performance) and then
//! drives the SCHEDULER period by period to produce the events:
//!
//!   * the #596 acceptance in miniature - one launch request, then nothing but
//!     the clock, with the armed cell's `next` chain walking its column across
//!     four clips and one stop/start pair recorded per move;
//!   * a `stop` chain ending the cell, with its pair closed on the grid line;
//!   * a REFUSED action (a jump outside the grid) firing once per action time
//!     rather than once per audio period - the rule that keeps a chain which can
//!     do nothing from spinning on the audio path;
//!   * a reset recording the stop of every slot it ends, which is the engine
//!     half of session.back_to_arrangement and what leaves a land pass with
//!     nothing open to refuse.
//!
//! The pure Follow Action decisions are proven beside this file, in
//! tests/src/core/SessionFollowTest.cpp - the split is the file-length
//! ratchet's (these two were one file and measured 667 lines).

#include <QtTest>

#include <cstdint>

#include "SessionArrangementRecorder.h"
#include "SessionFollow.h"
#include "SessionScheduler.h"

using namespace lmms;

namespace
{

constexpr tick_t kTicksPerBar = 192;
constexpr tick_t kTickStep = 4;
constexpr f_cnt_t kFramesPerPeriod = 16;
constexpr float kFramesPerTick = 4.0f;
constexpr int kSceneCount = 4;
constexpr std::size_t kCapacity = SessionArrangementRecorder::Capacity;

SessionClockContext clock(tick_t ticks)
{
	SessionClockContext ctx;
	ctx.positionTicks = ticks;
	ctx.ticksPerBar = kTicksPerBar;
	ctx.framesPerTick = kFramesPerTick;
	ctx.transportRunning = true;
	return ctx;
}

FollowAction action(FollowAction::Type type, double chance, bool linked, double timeBars,
	int jumpTo)
{
	FollowAction out;
	out.type = type;
	out.chance = chance;
	out.linked = linked;
	out.timeBars = timeBars;
	out.jumpTo = jumpTo;
	return out;
}

//! A one-entry chain, which is the plain "always this" action.
FollowPlan single(const FollowAction& entry, int sceneCount)
{
	FollowPlan plan;
	plan.enabled = true;
	plan.count = 1;
	plan.entries[0] = entry;
	plan.sceneCount = sceneCount;
	return plan;
}

//! Drives the scheduler one period at a time from `from` to `to` inclusive.
void drive(SessionScheduler& scheduler, SessionClockContext& ctx, tick_t from, tick_t to)
{
	for (tick_t position = from; position <= to; position += kTickStep)
	{
		ctx.positionTicks = position;
		scheduler.processAudio(ctx, kFramesPerPeriod);
	}
}

} // namespace

class SessionArrangementRecordTest : public QObject
{
	Q_OBJECT

private slots:
	//! Nothing is recorded while the tap is disarmed, and a FULL ring drops and
	//! counts rather than growing.
	void recorderRequiresArmingAndDropsWhenFull()
	{
		SessionArrangementRecorder recorder;
		QVERIFY(!recorder.armed());
		QVERIFY(!recorder.recordStart(0, 0, 0));
		QCOMPARE(recorder.recorded(), std::uint64_t(0));
		QCOMPARE(recorder.dropped(), std::uint64_t(0));
		QCOMPARE(recorder.pending(), std::size_t(0));

		recorder.setArmed(true);
		QVERIFY(recorder.armed());
		for (std::size_t i = 0; i < kCapacity; ++i)
		{
			QVERIFY2(recorder.recordStart(0, 0, static_cast<tick_t>(i)), "the ring refused a slot");
		}
		QCOMPARE(recorder.recorded(), static_cast<std::uint64_t>(kCapacity));
		// The ring is bounded: one more event is DROPPED and counted, never
		// grown into.
		QVERIFY(!recorder.recordStart(0, 0, 999));
		QCOMPARE(recorder.dropped(), std::uint64_t(1));
		QCOMPARE(recorder.recorded(), static_cast<std::uint64_t>(kCapacity));
	}

	//! FIFO order, and the snapshot reads without consuming: the pair a land
	//! pass decides on is the pair it then consumes.
	void recorderSnapshotIsNonDestructiveAndOrdered()
	{
		SessionArrangementRecorder recorder;
		recorder.setArmed(true);
		QVERIFY(recorder.recordStart(2, 1, 96));
		QVERIFY(recorder.recordStop(2, 1, 288));
		QVERIFY(recorder.recordStart(0, 0, 288));
		QCOMPARE(recorder.pending(), std::size_t(3));

		SessionArrangementRecorder::Event events[kCapacity];
		const std::size_t seen = recorder.snapshot(events, kCapacity);
		QCOMPARE(seen, std::size_t(3));
		// The snapshot did NOT consume anything.
		QCOMPARE(recorder.pending(), std::size_t(3));
		QVERIFY(events[0].started);
		QCOMPARE(events[0].track, 2);
		QCOMPARE(events[0].scene, 1);
		QCOMPARE(static_cast<int>(events[0].tick), 96);
		QVERIFY(!events[1].started);
		QCOMPARE(static_cast<int>(events[1].tick), 288);
		QVERIFY(events[2].started);
		QCOMPARE(events[2].track, 0);
		QCOMPARE(static_cast<int>(events[2].tick), 288);

		// A bounded snapshot copies the OLDEST events and stops.
		SessionArrangementRecorder::Event two[2];
		QCOMPARE(recorder.snapshot(two, 2), std::size_t(2));
		QCOMPARE(two[0].track, 2);
		QVERIFY(!two[1].started);

		// Consuming is FIFO over the same order the snapshot reported.
		SessionArrangementRecorder::Event popped;
		QVERIFY(recorder.pop(popped));
		QCOMPARE(popped.track, 2);
		QVERIFY(popped.started);
		QVERIFY(recorder.pop(popped));
		QVERIFY(!popped.started);
		QVERIFY(recorder.pop(popped));
		QCOMPARE(popped.track, 0);
		QVERIFY(!recorder.pop(popped));
		QCOMPARE(recorder.pending(), std::size_t(0));
	}

	//! Disarming KEEPS what the ring carries: the performance is landed, not
	//! dropped (session.arrangement_record_arm's contract).
	void recorderDisarmKeepsThePerformance()
	{
		SessionArrangementRecorder recorder;
		recorder.setArmed(true);
		QVERIFY(recorder.recordStart(1, 0, 0));
		QVERIFY(recorder.recordStop(1, 0, 192));
		recorder.setArmed(false);
		QVERIFY(!recorder.armed());
		QCOMPARE(recorder.pending(), std::size_t(2));
		SessionArrangementRecorder::Event event;
		QVERIFY(recorder.pop(event));
		QVERIFY(event.started);
		QCOMPARE(recorder.recorded(), std::uint64_t(2));
		// ...and nothing new is taped while it is disarmed.
		QVERIFY(!recorder.recordStart(1, 1, 192));
		QCOMPARE(recorder.pending(), std::size_t(1));
	}

	//! THE #596 ACCEPTANCE, in miniature: one launch request, then nothing but
	//! the clock. EVERY cell of the column carries the same `next` chain (which
	//! is the per-CLIP model Live uses and the data layer persists: the chain
	//! belongs to the clip that is playing, so the next clip's own action takes
	//! over when the column moves), and the armed column walks scene 0 -> 1 ->
	//! 2 -> 3 with the ring carrying one stop/start pair per move - which is
	//! what "launches, moves" means in SPEC §4.1.
	void followChainRunsHandsFreeAcrossClips()
	{
		SessionScheduler scheduler;
		scheduler.arrangementRecorder().setArmed(true);

		for (int scene = 0; scene < kSceneCount; ++scene)
		{
			QVERIFY(scheduler.requestFollowPlan(0, scene,
				single(action(FollowAction::Type::Next, 1.0, false, 1.0, 0), kSceneCount)));
		}
		// One launch, and no other request for the rest of the test.
		QVERIFY(scheduler.requestLaunch(0, 0, LaunchMode::Trigger, LaunchQuantisation::None));

		SessionClockContext ctx = clock(0);
		// Install the plans and start the clip.
		scheduler.processAudio(ctx, kFramesPerPeriod);
		QCOMPARE(scheduler.armedFollowCells(), 4);
		// bits (0 * 8 + 0) .. (0 * 8 + 3)
		QCOMPARE(scheduler.armedFollowCellsMask(), std::uint64_t(0xf));
		QVERIFY(scheduler.trackIsSessionActive(0));

		// Four bars of periods: the chain fires at 192, 384, 576 and 768.
		drive(scheduler, ctx, kTickStep, 4 * kTicksPerBar);
		QCOMPARE(static_cast<int>(scheduler.followFires()), 4);
		QCOMPARE(scheduler.completedLaunches(), std::uint64_t(5)); // the launch + 4 moves
		const std::uint64_t last = scheduler.lastFollowFire();
		QCOMPARE(static_cast<int>(followFireOutcome(last)),
			static_cast<int>(FollowOutcome::SwitchScene));
		QCOMPARE(followFireIndex(last), 0);
		QCOMPARE(followFireTargetScene(last), 0); // 3 -> next wraps to 0
		// The LAST fire's action time is the fourth bar line: a fire reports the
		// tick it was SCHEDULED for, not the period that noticed it.
		QCOMPARE(static_cast<int>(followFireTick(last)), 4 * kTicksPerBar);
		QVERIFY(scheduler.trackIsSessionActive(0));

		// The recorded performance: start(0) stop(0) start(1) stop(1) start(2)
		// stop(2) start(3) stop(3) start(0) - nine events, four closed pairs.
		SessionArrangementRecorder::Event events[kCapacity];
		const std::size_t count = scheduler.arrangementRecorder().snapshot(events, kCapacity);
		QCOMPARE(count, std::size_t(9));
		QVERIFY(events[0].started);
		QCOMPARE(events[0].scene, 0);
		QCOMPARE(static_cast<int>(events[0].tick), 0);
		int stops = 0;
		int startedScenes[4] = {-1, -1, -1, -1};
		int index = 0;
		for (std::size_t i = 0; i < count; ++i)
		{
			if (!events[i].started) { ++stops; }
			else if (index < 4) { startedScenes[index++] = events[i].scene; }
		}
		QCOMPARE(stops, 4);
		QCOMPARE(startedScenes[0], 0);
		QCOMPARE(startedScenes[1], 1);
		QCOMPARE(startedScenes[2], 2);
		QCOMPARE(startedScenes[3], 3);
		// Every stop is the scene that was playing, so the pairs are 0-0, 1-1,
		// 2-2 and 3-3 and the FIRST four events alternate start/stop.
		for (std::size_t i = 0; i < 8; i += 2)
		{
			QVERIFY(events[i].started);
			QVERIFY(!events[i + 1].started);
			QCOMPARE(events[i].scene, events[i + 1].scene);
		}
	}

	//! The chain belongs to the CLIP that is playing: a column whose NEXT cell
	//! carries no plan stops following there. This semantic decides how many
	//! cells a chain has to be armed on, and it was MEASURED rather than assumed
	//! - the first revision of followChainRunsHandsFreeAcrossClips armed one
	//! cell and the column stopped after the first move.
	void aCellWithNoPlanDoesNotFollow()
	{
		SessionScheduler scheduler;
		QVERIFY(scheduler.requestFollowPlan(0, 0,
			single(action(FollowAction::Type::Next, 1.0, false, 1.0, 0), kSceneCount)));
		QVERIFY(scheduler.requestLaunch(0, 0, LaunchMode::Trigger, LaunchQuantisation::None));
		SessionClockContext ctx = clock(0);
		scheduler.processAudio(ctx, kFramesPerPeriod);
		drive(scheduler, ctx, kTickStep, 4 * kTicksPerBar);
		// One move - into the cell that has no plan - and nothing after it.
		QCOMPARE(static_cast<int>(scheduler.followFires()), 1);
		QCOMPARE(followFireTargetScene(scheduler.lastFollowFire()), 1);
		QVERIFY(scheduler.trackIsSessionActive(0));
	}

	//! A `stop` chain ends the cell (and records only its stop: the start that
	//! opened the pair is already in the ring), at the ACTION time and not at
	//! the period that noticed it.
	void followStopEndsTheClip()
	{
		SessionScheduler scheduler;
		scheduler.arrangementRecorder().setArmed(true);
		QVERIFY(scheduler.requestFollowPlan(1, 0,
			single(action(FollowAction::Type::Stop, 1.0, false, 1.0, 0), kSceneCount)));
		QVERIFY(scheduler.requestLaunch(1, 0, LaunchMode::Trigger, LaunchQuantisation::None));

		SessionClockContext ctx = clock(0);
		scheduler.processAudio(ctx, kFramesPerPeriod);
		QVERIFY(scheduler.trackIsSessionActive(1));
		drive(scheduler, ctx, kTickStep, kTicksPerBar + kTickStep);
		QCOMPARE(static_cast<int>(scheduler.followFires()), 1);
		QCOMPARE(static_cast<int>(followFireOutcome(scheduler.lastFollowFire())),
			static_cast<int>(FollowOutcome::Stop));
		QCOMPARE(static_cast<int>(scheduler.slotPhase(1, 0)), static_cast<int>(SlotPhase::Idle));
		QVERIFY(!scheduler.trackIsSessionActive(1));
		SessionArrangementRecorder::Event events[kCapacity];
		const std::size_t count = scheduler.arrangementRecorder().snapshot(events, kCapacity);
		QCOMPARE(count, std::size_t(2));
		QVERIFY(events[0].started);
		QVERIFY(!events[1].started);
		QCOMPARE(static_cast<int>(events[1].tick), kTicksPerBar);
	}

	//! A REFUSED action (a jump outside the grid) fires ONCE PER ACTION TIME and
	//! not once per audio period: the rule that keeps a chain that can do
	//! nothing from spinning on the audio path.
	void refusedFollowFiresOncePerActionTime()
	{
		SessionScheduler scheduler;
		QVERIFY(scheduler.requestFollowPlan(0, 0,
			single(action(FollowAction::Type::Jump, 1.0, false, 1.0, kSceneCount + 5),
				kSceneCount)));
		QVERIFY(scheduler.requestLaunch(0, 0, LaunchMode::Trigger, LaunchQuantisation::None));
		SessionClockContext ctx = clock(0);
		scheduler.processAudio(ctx, kFramesPerPeriod);
		drive(scheduler, ctx, kTickStep, 2 * kTicksPerBar);
		// Two action times in two bars: exactly two fires, not the 96 periods.
		QCOMPARE(static_cast<int>(scheduler.followFires()), 2);
		QCOMPARE(static_cast<int>(followFireOutcome(scheduler.lastFollowFire())),
			static_cast<int>(FollowOutcome::Refused));
		QCOMPARE(followFireTargetScene(scheduler.lastFollowFire()), -1);
		// The cell is still playing: a refused action does not stop anything.
		QVERIFY(scheduler.trackIsSessionActive(0));
	}

	//! A reset ENDS the performance for every slot it drops, so the ring holds a
	//! matched pair rather than a start whose stop never came. This is the
	//! engine half of session.back_to_arrangement, and what makes
	//! session.arrangement_record_land able to land the performance it ends.
	void resetRecordsTheStopsItEnds()
	{
		SessionScheduler scheduler;
		scheduler.arrangementRecorder().setArmed(true);
		QVERIFY(scheduler.requestLaunch(0, 0, LaunchMode::Trigger, LaunchQuantisation::None));
		QVERIFY(scheduler.requestLaunch(1, 1, LaunchMode::Trigger, LaunchQuantisation::None));
		SessionClockContext ctx = clock(0);
		scheduler.processAudio(ctx, kFramesPerPeriod);
		ctx.positionTicks = kTicksPerBar;
		scheduler.processAudio(ctx, kFramesPerPeriod);
		QCOMPARE(scheduler.activeSlotCount(), 2);

		scheduler.reset();
		scheduler.processAudio(ctx, kFramesPerPeriod);
		QCOMPARE(scheduler.activeSlotCount(), 0);
		QCOMPARE(scheduler.armedFollowCells(), 0);
		QCOMPARE(scheduler.armedFollowCellsMask(), std::uint64_t(0));

		// Four events - two starts and, for each, the stop the reset recorded -
		// so every start is paired and a land pass has nothing open to refuse.
		SessionArrangementRecorder::Event events[kCapacity];
		const std::size_t count = scheduler.arrangementRecorder().snapshot(events, kCapacity);
		QCOMPARE(count, std::size_t(4));
		int starts = 0;
		int stops = 0;
		for (std::size_t i = 0; i < count; ++i)
		{
			if (events[i].started) { ++starts; } else { ++stops; }
		}
		QCOMPARE(starts, 2);
		QCOMPARE(stops, 2);
		// ...and the ring KEEPS the performance across the reset: the feature
		// exists to land it, so a reset that cleared the ring would be the data
		// loss it is there to prevent.
		QCOMPARE(scheduler.arrangementRecorder().pending(), std::size_t(4));
	}
};

QTEST_GUILESS_MAIN(SessionArrangementRecordTest)
#include "SessionArrangementRecordTest.moc"
