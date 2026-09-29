/*
 * SessionSceneFollowTest.cpp - R5.2: Follow Action chains on scenes
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

/*! The plan's acceptance list for R5.2, each held directly:
 *    * a scene chain fires WHOLE rows - three columns move to the next row on one line;
 *    * the scene overrides the clip - a cell chain that would stop its column first is
 *      skipped while the row's chain runs, and fires when the row was not launched;
 *    * Stop ends the row and the chain; a row nobody plays any more ends its chain unfired;
 *    * the audio path allocates nothing with a scene chain firing;
 *    * save/load round trip, and old projects byte-identical: a section without a scene
 *      chain is still v="1", one with a chain is v="2" and reads back equal;
 *    * the socket half: session.set_scene / set_slot persist a chain (set_slot could not
 *      before - the defect this slot also closes), scene_follow_set arms it.
 */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>
#include <QJsonObject>

#include "AllocationProbe.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"
#include "SessionFollow.h"
#include "SessionModel.h"
#include "SessionScheduler.h"

using namespace lmms;

namespace
{

constexpr tick_t kTicksPerBar = 192;
constexpr tick_t kTickStep = 4;
constexpr f_cnt_t kFramesPerPeriod = 16;
constexpr int kScenes = 3;

SessionClockContext clockAt(tick_t ticks)
{
	SessionClockContext ctx;
	ctx.positionTicks = ticks;
	ctx.ticksPerBar = kTicksPerBar;
	ctx.framesPerTick = 4.0f;
	ctx.transportRunning = true;
	return ctx;
}

FollowPlan chainOf(FollowAction::Type type, double bars)
{
	FollowPlan plan;
	plan.enabled = true;
	plan.count = 1;
	plan.entries[0].type = type;
	plan.entries[0].linked = false;
	plan.entries[0].timeBars = bars;
	plan.sceneCount = kScenes;
	return plan;
}

//! Runs the transport from @a from to @a to ticks, one period at a time.
void runTo(SessionScheduler& scheduler, tick_t from, tick_t to)
{
	for (tick_t t = from; t <= to; t += kTickStep) { scheduler.processAudio(clockAt(t), kFramesPerPeriod); }
}

void launchRow(SessionScheduler& scheduler, int scene, int columns)
{
	for (int track = 0; track < columns; ++track)
	{
		QVERIFY(scheduler.requestLaunch(track, scene, LaunchMode::Trigger, LaunchQuantisation::None));
	}
	QVERIFY(scheduler.requestSceneLaunch(scene, LaunchQuantisation::None));
}

} // namespace

class SessionSceneFollowTest : public QObject
{
	Q_OBJECT

private slots:
	void aSceneChainMovesTheWholeRowOnOneLine()
	{
		SessionScheduler scheduler;
		QVERIFY(scheduler.requestSceneFollowPlan(0, chainOf(FollowAction::Type::Next, 1.0)));
		launchRow(scheduler, 0, 3);
		runTo(scheduler, 0, kTicksPerBar + 8);
		QCOMPARE(scheduler.armedFollowScenes(), 1);
		QCOMPARE(scheduler.sceneFollowFires(), std::uint64_t{1});
		for (int track = 0; track < 3; ++track)
		{
			QCOMPARE(scheduler.slotPhase(track, 1), SlotPhase::Playing);
			QCOMPARE(scheduler.slotPhase(track, 0), SlotPhase::Idle);
		}
		// The three moves are three starts on ONE grid line: the action time.
		QCOMPARE(startLineTick(scheduler.lastStartLine()), kTicksPerBar);
		QCOMPARE(startLineStarts(scheduler.lastStartLine()), 3u);
		QCOMPARE(scheduler.activeScene(), 1);
	}

	void theSceneOverridesTheClip()
	{
		// Control first: launched on its own, the cell's chain stops it at half a bar.
		{
			SessionScheduler scheduler;
			QVERIFY(scheduler.requestFollowPlan(0, 0, chainOf(FollowAction::Type::Stop, 0.5)));
			QVERIFY(scheduler.requestSceneFollowPlan(0, chainOf(FollowAction::Type::Next, 1.0)));
			QVERIFY(scheduler.requestLaunch(0, 0, LaunchMode::Trigger, LaunchQuantisation::None));
			runTo(scheduler, 0, kTicksPerBar / 2 + 8);
			QCOMPARE(scheduler.slotPhase(0, 0), SlotPhase::Idle);
		}
		// Launched as a row: the row's chain governs and the cell's Stop never fires.
		SessionScheduler scheduler;
		QVERIFY(scheduler.requestFollowPlan(0, 0, chainOf(FollowAction::Type::Stop, 0.5)));
		QVERIFY(scheduler.requestSceneFollowPlan(0, chainOf(FollowAction::Type::Next, 1.0)));
		launchRow(scheduler, 0, 1);
		runTo(scheduler, 0, kTicksPerBar / 2 + 8);
		QCOMPARE(scheduler.slotPhase(0, 0), SlotPhase::Playing);
		runTo(scheduler, kTicksPerBar / 2 + 12, kTicksPerBar + 8);
		QCOMPARE(scheduler.slotPhase(0, 1), SlotPhase::Playing);
		QCOMPARE(scheduler.followFires(), std::uint64_t{1});
	}

	void stopEndsTheRowAndAnAbandonedRowEndsItsChainUnfired()
	{
		SessionScheduler stopping;
		QVERIFY(stopping.requestSceneFollowPlan(0, chainOf(FollowAction::Type::Stop, 1.0)));
		launchRow(stopping, 0, 2);
		runTo(stopping, 0, kTicksPerBar + 8);
		QCOMPARE(stopping.slotPhase(0, 0), SlotPhase::Idle);
		QCOMPARE(stopping.slotPhase(1, 0), SlotPhase::Idle);
		QCOMPARE(stopping.activeScene(), -1);

		SessionScheduler abandoned;
		QVERIFY(abandoned.requestSceneFollowPlan(0, chainOf(FollowAction::Type::Next, 1.0)));
		launchRow(abandoned, 0, 2);
		runTo(abandoned, 0, 40);
		for (int track = 0; track < 2; ++track)
		{
			QVERIFY(abandoned.requestStop(track, 0, LaunchMode::Trigger, LaunchQuantisation::None));
		}
		runTo(abandoned, 44, 2 * kTicksPerBar);
		QCOMPARE(abandoned.sceneFollowFires(), std::uint64_t{0});
		QCOMPARE(abandoned.activeScene(), -1);
		QCOMPARE(abandoned.slotPhase(0, 1), SlotPhase::Idle);
	}

	void theAudioPathDoesNotAllocateWithASceneChainFiring()
	{
		SessionScheduler scheduler;
		QVERIFY(scheduler.requestSceneFollowPlan(0, chainOf(FollowAction::Type::Next, 1.0)));
		QVERIFY(scheduler.requestSceneFollowPlan(1, chainOf(FollowAction::Type::Next, 1.0)));
		QVERIFY(scheduler.requestSceneFollowPlan(2, chainOf(FollowAction::Type::First, 1.0)));
		launchRow(scheduler, 0, 4);
		scheduler.processAudio(clockAt(0), kFramesPerPeriod);
		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		for (int period = 1; period < 20000; ++period)
		{
			scheduler.processAudio(clockAt(static_cast<tick_t>(period) * kTickStep), kFramesPerPeriod);
		}
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		QCOMPARE(allocations, std::uint64_t{0});
		QVERIFY(scheduler.sceneFollowFires() > 100u);
		scheduler.reset();
		scheduler.processAudio(clockAt(0), kFramesPerPeriod);
		QCOMPARE(scheduler.armedFollowScenes(), 0);
		QCOMPARE(scheduler.activeScene(), -1);
	}

	void aSceneChainRoundTripsAndOldSectionsStayVersionOne()
	{
		SessionModel model;
		model.setTrackCount(2);
		model.setSceneCount(kScenes);
		model.scene(1).setName(QStringLiteral("verse"));
		QDomDocument plainDoc;
		QDomElement plainRoot = plainDoc.createElement(QStringLiteral("song"));
		plainDoc.appendChild(plainRoot);
		QCOMPARE(model.saveScenesSection(plainDoc, plainRoot).attribute(QStringLiteral("v")), QStringLiteral("1"));

		FollowAction next;
		next.type = FollowAction::Type::Jump;
		next.jumpTo = 2;
		next.chance = 0.25;
		model.scene(1).setFollowActions({next});
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("song"));
		doc.appendChild(root);
		const QDomElement section = model.saveScenesSection(doc, root);
		QCOMPARE(section.attribute(QStringLiteral("v")), QStringLiteral("2"));

		SessionModel loaded;
		QVERIFY(loaded.restoreScenesSection(section));
		QCOMPARE(loaded.scene(1).followActions().size(), std::size_t{1});
		QVERIFY(loaded.scene(1).followActions()[0] == next);
		QCOMPARE(loaded.scene(1).name(), QStringLiteral("verse"));

		// The legacy <session> form (the undo checkpoint's payload) carries it too.
		QDomDocument legacy;
		QDomElement legacyRoot = legacy.createElement(QStringLiteral("song"));
		legacy.appendChild(legacyRoot);
		SessionModel restored;
		QVERIFY(restored.restoreState(model.saveState(legacy, legacyRoot)));
		QCOMPARE(restored.scene(1).followActions().size(), std::size_t{1});
	}

	void theSocketPersistsAndArmsSceneAndCellChains()
	{
		using revtest::run;
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1},
			{QStringLiteral("scenes"), kScenes}}).ok);
		const QJsonArray stop{QJsonObject{{QStringLiteral("type"), QStringLiteral("stop")}}};

		// The defect: session.follow_set said "session.set_slot writes one", and it could not.
		const ControlResult slot = run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 0}, {QStringLiteral("type"), QStringLiteral("audio")},
			{QStringLiteral("source"), QStringLiteral("kick.wav")}, {QStringLiteral("follow_actions"), stop}});
		QVERIFY2(slot.ok, qPrintable(slot.errorMessage));
		const ControlResult armedCell = run(QStringLiteral("session.follow_set"),
			{{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0}});
		QVERIFY2(armedCell.ok, qPrintable(armedCell.errorMessage));
		QCOMPARE(armedCell.result.value(QStringLiteral("source")).toString(), QStringLiteral("slot"));

		QCOMPARE(run(QStringLiteral("session.scene_follow_set"), {{QStringLiteral("scene"), 1}}).errorKind,
			ControlErrorKind::InvalidArgs);
		const QJsonArray next{QJsonObject{{QStringLiteral("type"), QStringLiteral("next")},
			{QStringLiteral("linked"), false}, {QStringLiteral("time_bars"), 2}}};
		const ControlResult scene = run(QStringLiteral("session.set_scene"),
			{{QStringLiteral("scene"), 1}, {QStringLiteral("follow_actions"), next}});
		QVERIFY2(scene.ok, qPrintable(scene.errorMessage));
		QCOMPARE(scene.result.value(QStringLiteral("scene")).toObject()
			.value(QStringLiteral("follow_actions")).toArray().size(), 1);
		const ControlResult armed = run(QStringLiteral("session.scene_follow_set"), {{QStringLiteral("scene"), 1}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		QCOMPARE(armed.result.value(QStringLiteral("source")).toString(), QStringLiteral("scene"));
		QCOMPARE(armed.result.value(QStringLiteral("step_ticks")).toInt(), 2 * kTicksPerBar);

		// A bad entry is refused before anything is written, and one undo clears the chain.
		QCOMPARE(run(QStringLiteral("session.set_scene"), {{QStringLiteral("scene"), 1},
			{QStringLiteral("follow_actions"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("sideways")}}}}})
			.errorKind, ControlErrorKind::InvalidArgs);
		REV_UNDO_OR_FAIL();
		const QJsonArray scenes = run(QStringLiteral("session.get_state")).result.value(QStringLiteral("scenes")).toArray();
		QCOMPARE(scenes.at(1).toObject().value(QStringLiteral("follow_actions")).toArray().size(), 0);
		ControlRegistry::setReady(false);
		Engine::destroy();
	}
};

QTEST_GUILESS_MAIN(SessionSceneFollowTest)
#include "SessionSceneFollowTest.moc"
