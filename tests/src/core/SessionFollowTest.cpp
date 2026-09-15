/*
 * SessionFollowTest.cpp - Follow Action decisions and the plan rules
 *                          (task #641, the #596 engine half).
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

//! WHAT THIS FILE PROVES. The Follow Action decision is a PURE function
//! (include/SessionFollow.h), so all ten action types, the chance weighting, the
//! linked/unlinked timing, the published fire's packing and the audio thread's
//! random source are driven exhaustively here with no engine, no audio device
//! and no threads. What is left - the ring Arrangement Record feeds, and the
//! engine-driven behaviour a chain produces when the scheduler runs it - is
//! tests/src/core/SessionArrangementRecordTest.cpp, registered beside this one.
//!
//! The split is the file-length ratchet's: these two files were one, and it
//! measured 667 lines against the 500-line limit.

#include <QtTest>

#include <cstdint>

#include "AllocationProbe.h"
#include "SessionFollow.h"
#include "SessionScheduler.h"

using namespace lmms;

namespace
{

//! One bar of a 4/4 project: TimePos::DefaultTicksPerBar (include/TimePos.h:38).
constexpr tick_t kTicksPerBar = 192;
//! 4 ticks per period: the step SessionSchedulerTest uses.
constexpr tick_t kTickStep = 4;
constexpr f_cnt_t kFramesPerPeriod = 16;
constexpr float kFramesPerTick = 4.0f;
constexpr int kSceneCount = 4;

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
FollowPlan single(const FollowAction& entry, int sceneCount, tick_t clipLength = 0)
{
	FollowPlan plan;
	plan.enabled = true;
	plan.count = 1;
	plan.entries[0] = entry;
	plan.clipLengthTicks = clipLength;
	plan.sceneCount = sceneCount;
	return plan;
}

FollowEval evalAt(tick_t started, tick_t position, double rngUnit = 0.0, int scene = 0,
	int sceneCount = kSceneCount)
{
	FollowEval eval;
	eval.startedTick = started;
	eval.positionTicks = position;
	eval.ticksPerBar = kTicksPerBar;
	eval.scene = scene;
	eval.sceneCount = sceneCount;
	eval.rngUnit = rngUnit;
	return eval;
}

} // namespace

class SessionFollowTest : public QObject
{
	Q_OBJECT

private slots:
	//! Previous/Next WRAP, First/Last clamp, Any is uniform, Other excludes the
	//! current scene, Jump names a row and nothing else does.
	void followTargetSceneRules()
	{
		using Type = FollowAction::Type;
		QCOMPARE(followTargetScene(Type::Previous, 0, kSceneCount, 0, 0.0), kSceneCount - 1);
		QCOMPARE(followTargetScene(Type::Previous, 2, kSceneCount, 0, 0.0), 1);
		QCOMPARE(followTargetScene(Type::Next, 0, kSceneCount, 0, 0.0), 1);
		QCOMPARE(followTargetScene(Type::Next, kSceneCount - 1, kSceneCount, 0, 0.0), 0);
		QCOMPARE(followTargetScene(Type::First, 3, kSceneCount, 0, 0.0), 0);
		QCOMPARE(followTargetScene(Type::Last, 0, kSceneCount, 0, 0.0), kSceneCount - 1);
		QCOMPARE(followTargetScene(Type::Any, 1, kSceneCount, 0, 0.0), 0);
		QCOMPARE(followTargetScene(Type::Any, 1, kSceneCount, 0, 0.5), 2);
		QCOMPARE(followTargetScene(Type::Any, 1, kSceneCount, 0, 0.999), kSceneCount - 1);
		// Other: uniform over the list MINUS the current scene, and it never
		// returns the current one whatever the draw.
		for (int step = 0; step < 20; ++step)
		{
			const int target = followTargetScene(Type::Other, 1, kSceneCount, 0, step / 20.0);
			QVERIFY(target != 1);
			QVERIFY(target >= 0 && target < kSceneCount);
		}
		// ...and it has no answer at all on a one-scene grid.
		QCOMPARE(followTargetScene(Type::Other, 0, 1, 0, 0.0), -1);
		QCOMPARE(followTargetScene(Type::Jump, 0, kSceneCount, 3, 0.0), 3);
		// An out-of-grid Jump is REFUSED, never clamped: playing a different
		// scene than the chain names would be a wrong note, not a recovery.
		QCOMPARE(followTargetScene(Type::Jump, 0, kSceneCount, kSceneCount, 0.0), -1);
		QCOMPARE(followTargetScene(Type::Jump, 0, kSceneCount, -1, 0.0), -1);
		// NoAction and Stop have no scene at all.
		QCOMPARE(followTargetScene(Type::NoAction, 0, kSceneCount, 0, 0.0), -1);
		QCOMPARE(followTargetScene(Type::Stop, 0, kSceneCount, 0, 0.0), -1);
		// A grid with no scenes has no addresses.
		QCOMPARE(followTargetScene(Type::Next, 0, 0, 0, 0.0), -1);
	}

	//! Linked follows the clip's own length and falls back to ONE BAR - never to
	//! 0, which would fire on every audio period. Unlinked uses the first
	//! entry's timeBars. A chain that cannot fire answers 0.
	void followActionTicksRules()
	{
		const FollowPlan oneBar = single(action(FollowAction::Type::Next, 1.0, false, 1.0, 0),
			kSceneCount);
		QCOMPARE(followActionTicks(oneBar, kTicksPerBar), kTicksPerBar);

		const FollowPlan twoBars = single(action(FollowAction::Type::Next, 1.0, false, 2.0, 0),
			kSceneCount);
		QCOMPARE(followActionTicks(twoBars, kTicksPerBar), 2 * kTicksPerBar);

		// Linked with a clip length the model knows: the clip's own length wins.
		const FollowPlan linked = single(action(FollowAction::Type::Next, 1.0, true, 4.0, 0),
			kSceneCount, 64);
		QCOMPARE(followActionTicks(linked, kTicksPerBar), 64);

		// Linked with NO length: one bar, not zero.
		const FollowPlan linkedNoLength = single(action(FollowAction::Type::Next, 1.0, true, 1.0, 0),
			kSceneCount);
		QCOMPARE(followActionTicks(linkedNoLength, kTicksPerBar), kTicksPerBar);

		// An Unlinked time that rounds to nothing is still "cannot fire", and
		// the caller reads 0 as "nothing to do" rather than firing every period.
		const FollowPlan tiny = single(action(FollowAction::Type::Next, 1.0, false, 0.0, 0),
			kSceneCount);
		QCOMPARE(followActionTicks(tiny, kTicksPerBar), 0);

		// Disabled or empty: 0, whatever the timing says.
		FollowPlan disabled = oneBar;
		disabled.enabled = false;
		QCOMPARE(followActionTicks(disabled, kTicksPerBar), 0);
		FollowPlan empty = oneBar;
		empty.count = 0;
		QCOMPARE(followActionTicks(empty, kTicksPerBar), 0);
	}

	//! Chance A/B weighting: the weights are the entries' own, normalised over
	//! the chain; a chain whose weights sum to nothing picks the first entry,
	//! because a chain that can never fire is indistinguishable from a broken
	//! one; a disabled chain picks nothing.
	void followChanceWeighting()
	{
		FollowPlan plan;
		plan.enabled = true;
		plan.count = 2;
		plan.entries[0] = action(FollowAction::Type::Next, 0.25, false, 1.0, 0);
		plan.entries[1] = action(FollowAction::Type::Previous, 0.75, false, 1.0, 0);
		plan.sceneCount = kSceneCount;

		QCOMPARE(pickFollowIndex(plan, 0.0), 0);
		QCOMPARE(pickFollowIndex(plan, 0.2499), 0);
		QCOMPARE(pickFollowIndex(plan, 0.25), 1);
		QCOMPARE(pickFollowIndex(plan, 0.9999), 1);

		// The DRAW decides which entry, and the entry decides what happens: the
		// same plan at two draws selects two different actions.
		QCOMPARE(decideFollowFire(plan, evalAt(0, kTicksPerBar, 0.1)).chosenIndex, 0);
		QCOMPARE(decideFollowFire(plan, evalAt(0, kTicksPerBar, 0.9)).chosenIndex, 1);
		QCOMPARE(static_cast<int>(decideFollowFire(plan, evalAt(0, kTicksPerBar, 0.1)).outcome),
			static_cast<int>(FollowOutcome::SwitchScene));
		QCOMPARE(decideFollowFire(plan, evalAt(0, kTicksPerBar, 0.1)).targetScene, 1);
		QCOMPARE(decideFollowFire(plan, evalAt(0, kTicksPerBar, 0.9)).targetScene, kSceneCount - 1);

		// Equal weights split the draw in half.
		FollowPlan even = plan;
		even.entries[0].chance = 1.0;
		even.entries[1].chance = 1.0;
		QCOMPARE(pickFollowIndex(even, 0.49), 0);
		QCOMPARE(pickFollowIndex(even, 0.51), 1);

		// Every weight zero: the FIRST entry, not "nothing fired".
		FollowPlan zero = plan;
		zero.entries[0].chance = 0.0;
		zero.entries[1].chance = 0.0;
		QCOMPARE(pickFollowIndex(zero, 0.5), 0);

		// Disabled / empty: -1, which is "nothing was selected".
		FollowPlan off = plan;
		off.enabled = false;
		QCOMPARE(pickFollowIndex(off, 0.5), -1);
		FollowPlan none = plan;
		none.count = 0;
		QCOMPARE(pickFollowIndex(none, 0.5), -1);

		// An out-of-range draw is clamped, not wrapped: a caller that hands in
		// 1.0 must not pick a chain entry by accident.
		QCOMPARE(pickFollowIndex(plan, 1.0), 1);
		QCOMPARE(pickFollowIndex(plan, -1.0), 0);
	}

	//! The decision fires only at or after its action time, and each outcome is
	//! what it says it is.
	void followDecisionOutcomes()
	{
		const FollowPlan next = single(action(FollowAction::Type::Next, 1.0, false, 1.0, 0),
			kSceneCount);
		// Before the action time: nothing fires, not even a "None" outcome with
		// a chosen entry - the chain is scheduled, not decided.
		const FollowFire tooEarly = decideFollowFire(next, evalAt(0, kTicksPerBar - 1));
		QCOMPARE(static_cast<int>(tooEarly.outcome), static_cast<int>(FollowOutcome::None));
		QCOMPARE(tooEarly.chosenIndex, -1);
		// Exactly ON the action time: it fires (the boundary is inclusive).
		const FollowFire onTime = decideFollowFire(next, evalAt(0, kTicksPerBar));
		QCOMPARE(static_cast<int>(onTime.outcome), static_cast<int>(FollowOutcome::SwitchScene));
		QCOMPARE(onTime.targetScene, 1);
		QCOMPARE(onTime.chosenIndex, 0);

		QCOMPARE(static_cast<int>(decideFollowFire(single(
			action(FollowAction::Type::Stop, 1.0, false, 1.0, 0), kSceneCount),
			evalAt(0, kTicksPerBar)).outcome), static_cast<int>(FollowOutcome::Stop));
		QCOMPARE(static_cast<int>(decideFollowFire(single(
			action(FollowAction::Type::PlayAgain, 1.0, false, 1.0, 0), kSceneCount),
			evalAt(0, kTicksPerBar)).outcome), static_cast<int>(FollowOutcome::Restart));
		// NoAction fired, selected an entry, and did nothing - which is a
		// measurement ("this action time passed"), not a retry.
		const FollowFire noAction = decideFollowFire(single(
			action(FollowAction::Type::NoAction, 1.0, false, 1.0, 0), kSceneCount),
			evalAt(0, kTicksPerBar));
		QCOMPARE(static_cast<int>(noAction.outcome), static_cast<int>(FollowOutcome::None));
		QCOMPARE(noAction.chosenIndex, 0);
		// A Jump outside the grid is REFUSED, and the entry that produced it is
		// still named: the fire is a measurement, not a silent retry.
		const FollowFire refused = decideFollowFire(single(
			action(FollowAction::Type::Jump, 1.0, false, 1.0, kSceneCount + 3), kSceneCount),
			evalAt(0, kTicksPerBar));
		QCOMPARE(static_cast<int>(refused.outcome), static_cast<int>(FollowOutcome::Refused));
		QCOMPARE(refused.targetScene, -1);
		QCOMPARE(refused.chosenIndex, 0);
		// A disabled chain does not even select.
		FollowPlan off = next;
		off.enabled = false;
		QCOMPARE(decideFollowFire(off, evalAt(0, kTicksPerBar)).chosenIndex, -1);
	}

	//! The published fire is ONE packed word: outcome, chain entry, addressed
	//! scene and tick come out of the same 64 bits, so a reader can never pair a
	//! new outcome with the previous tick.
	void followFirePackingRoundTrips()
	{
		const FollowOutcome outcomes[] = {FollowOutcome::None, FollowOutcome::Stop,
			FollowOutcome::Restart, FollowOutcome::SwitchScene, FollowOutcome::Refused};
		for (const FollowOutcome outcome : outcomes)
		{
			for (const int index : {-1, 0, 3, 7})
			{
				for (const int scene : {-1, 0, 1, 255})
				{
					const std::uint64_t packed = packFollowFire(outcome, index, scene, 480);
					QCOMPARE(static_cast<int>(followFireOutcome(packed)),
						static_cast<int>(outcome));
					QCOMPARE(followFireIndex(packed), index);
					QCOMPARE(followFireTargetScene(packed), scene);
					QCOMPARE(static_cast<int>(followFireTick(packed)), 480);
				}
			}
		}
		// A zero word reads as "nothing has fired yet".
		QCOMPARE(static_cast<int>(followFireOutcome(0)), static_cast<int>(FollowOutcome::None));
		QCOMPARE(followFireIndex(0), -1);
		QCOMPARE(followFireTargetScene(0), -1);
		QCOMPARE(static_cast<int>(followFireTick(0)), 0);
		QCOMPARE(QString::fromLatin1(followOutcomeName(FollowOutcome::Stop)),
			QStringLiteral("stop"));
		QCOMPARE(QString::fromLatin1(followOutcomeName(FollowOutcome::Restart)),
			QStringLiteral("restart"));
		QCOMPARE(QString::fromLatin1(followOutcomeName(FollowOutcome::SwitchScene)),
			QStringLiteral("switch_scene"));
		QCOMPARE(QString::fromLatin1(followOutcomeName(FollowOutcome::Refused)),
			QStringLiteral("refused"));
		QCOMPARE(QString::fromLatin1(followOutcomeName(FollowOutcome::None)),
			QStringLiteral("none"));
	}

	//! The audio thread's random source: deterministic from its seed, never 0,
	//! and its unit draw stays inside [0, 1).
	void followRandomIsDeterministicAndBounded()
	{
		std::uint32_t first = 0x2545f491u;
		std::uint32_t second = 0x2545f491u;
		for (int i = 0; i < 64; ++i)
		{
			QCOMPARE(followNextRandom(first), followNextRandom(second));
		}
		// A zero state is repaired rather than left to degenerate.
		std::uint32_t zero = 0;
		QVERIFY(followNextRandom(zero) != 0);
		std::uint32_t state = 1u;
		for (int i = 0; i < 1000; ++i)
		{
			const double unit = followRandomUnit(state);
			QVERIFY(unit >= 0.0 && unit < 1.0);
		}
	}

	//! Disarming a cell is a plan install with `enabled` false, not a second
	//! command type: the cell stops being armed, the count and the mask follow,
	//! and re-arming the SAME cell replaces it in place rather than consuming a
	//! second entry of the fixed table.
	void armingAndDisarmingACellIsOneCommand()
	{
		SessionScheduler scheduler;
		FollowPlan plan = single(action(FollowAction::Type::Next, 1.0, false, 1.0, 0),
			kSceneCount);
		QVERIFY(scheduler.requestFollowPlan(3, 2, plan));
		plan.enabled = false;
		QVERIFY(scheduler.requestFollowPlan(3, 2, plan));
		SessionClockContext ctx = clock(0);
		scheduler.processAudio(ctx, kFramesPerPeriod);
		QCOMPARE(scheduler.armedFollowCells(), 0);
		QCOMPARE(scheduler.armedFollowCellsMask(), std::uint64_t(0));
		plan.enabled = true;
		QVERIFY(scheduler.requestFollowPlan(3, 2, plan));
		scheduler.processAudio(ctx, kFramesPerPeriod);
		QCOMPARE(scheduler.armedFollowCells(), 1);
		// bit (3 * 8 + 2) = 26
		QCOMPARE(scheduler.armedFollowCellsMask(), std::uint64_t(1) << 26);
		// The model-side queue path does not allocate either: the audio thread
		// would inherit the allocator's lock through it.
		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		for (int i = 0; i < 1000; ++i)
		{
			scheduler.requestFollowPlan(3, 2, plan);
		}
		const std::uint64_t queued = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		QVERIFY2(queued == 0, qPrintable(QStringLiteral(
			"queueing a Follow Action plan allocated %1 times").arg(queued)));
	}

	//! The audio path with a plan installed and FIRING allocates nothing - the
	//! realtime rule (AGENTS.md rule 4), on the path this feature added.
	void audioThreadPathDoesNotAllocateWhileFollowing()
	{
		SessionScheduler scheduler;
		QVERIFY(scheduler.requestFollowPlan(0, 0,
			single(action(FollowAction::Type::Next, 1.0, false, 1.0, 0), kSceneCount)));
		QVERIFY(scheduler.requestLaunch(0, 0, LaunchMode::Trigger, LaunchQuantisation::None));

		SessionClockContext ctx = clock(0);
		scheduler.processAudio(ctx, kFramesPerPeriod);

		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		for (int period = 0; period < 20000; ++period)
		{
			ctx.positionTicks = static_cast<tick_t>(period) * kTickStep;
			scheduler.processAudio(ctx, kFramesPerPeriod);
		}
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		QVERIFY2(allocations == 0,
			qPrintable(QStringLiteral("audio-thread path allocated %1 times while following")
				.arg(allocations)));
		QVERIFY(scheduler.followFires() > 0u);
	}
};

QTEST_GUILESS_MAIN(SessionFollowTest)
#include "SessionFollowTest.moc"
