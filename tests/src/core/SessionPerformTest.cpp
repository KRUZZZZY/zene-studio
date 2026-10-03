/*
 * SessionPerformTest.cpp - capture a scene, scene playlists, setlists, marker actions, release and jump commands
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
/*! 0.5.0 L02 (W3): the playing cells become a new scene row, one undo step (WF-068); a playlist writes
 *  each scene's chain to jump to the next, the last stopping or looping (WF-067); the setlist is a
 *  preference with its own undo (WF-071); a marker's action runs when playback crosses it, and a jump
 *  back only moves the mark (WF-073); release and jump queue on the scheduler (WF-063, WF-070). */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "MarkerActions.h"
#include "ProjectMarkers.h"
#include "SessionModel.h"
#include "SessionScheduler.h"
#include "Song.h"
#include "Track.h"
#include "TrackFolder.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

SessionClockContext clockAt(tick_t ticks)
{
	SessionClockContext ctx;
	ctx.positionTicks = ticks;
	ctx.ticksPerBar = 192;
	ctx.framesPerTick = 4.0f;
	ctx.transportRunning = true;
	return ctx;
}

QJsonObject audioCell(int track, int scene)
{
	return {{QStringLiteral("track"), track}, {QStringLiteral("scene"), scene}, {QStringLiteral("type"), QStringLiteral("audio")},
		{QStringLiteral("source"), QStringLiteral("loop%1.wav").arg(track)}};
}

} // namespace

class SessionPerformTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 2}, {QStringLiteral("scenes"), 3}}).ok);
		for (const QJsonObject& cell : {audioCell(0, 0), audioCell(1, 1)}) { QVERIFY(run(QStringLiteral("session.set_slot"), cell).ok); }
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void thePlayingCellsBecomeAScene()
	{
		QCOMPARE(run(QStringLiteral("session.capture_scene")).errorKind, ControlErrorKind::NotFound);  // nothing plays
		SessionScheduler& scheduler = Engine::getSong()->sessionScheduler();
		QVERIFY(scheduler.requestLaunch(0, 0, LaunchMode::Trigger, LaunchQuantisation::None));
		QVERIFY(scheduler.requestLaunch(1, 1, LaunchMode::Trigger, LaunchQuantisation::None));
		scheduler.processAudio(clockAt(0), 16);
		scheduler.processAudio(clockAt(4), 16);
		const ControlResult captured = run(QStringLiteral("session.capture_scene"), {{QStringLiteral("name"), QStringLiteral("Chorus")}});
		QVERIFY2(captured.ok, qPrintable(captured.errorMessage));
		const SessionModel& model = Engine::getSong()->sessionModel();
		QCOMPARE(model.sceneCount(), 4);
		QCOMPARE(model.scene(3).name(), QStringLiteral("Chorus"));
		QVERIFY(!model.slot(0, 3).isEmpty() && !model.slot(1, 3).isEmpty());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::getSong()->sessionModel().sceneCount(), 3);
		scheduler.reset();
	}

	void aPlaylistChainsTheScenes()
	{
		QVERIFY(run(QStringLiteral("session.set_playlist"), {{QStringLiteral("scenes"), QJsonArray{2, 0, 1}}, {QStringLiteral("bars"), 4}}).ok);
		const SessionModel& model = Engine::getSong()->sessionModel();
		QCOMPARE(model.scene(2).followActions().front().type, FollowAction::Type::Jump);
		QCOMPARE(model.scene(2).followActions().front().jumpTo, 0);
		QCOMPARE(model.scene(0).followActions().front().jumpTo, 1);
		QCOMPARE(model.scene(1).followActions().front().type, FollowAction::Type::Stop);  // the last, no loop
		QCOMPARE(model.scene(2).followActions().front().timeBars, 4.0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(Engine::getSong()->sessionModel().scene(2).followActions().empty());
		QCOMPARE(run(QStringLiteral("session.set_playlist"), {{QStringLiteral("scenes"), QJsonArray{9}}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	void theSetlistIsAPreference()
	{
		QVERIFY(run(QStringLiteral("session.setlist"), {{QStringLiteral("items"), QJsonArray{QStringLiteral("/tmp/a.mmpz"), QStringLiteral("/tmp/b.mmpz")}}}).ok);
		QCOMPARE(run(QStringLiteral("session.setlist")).result.value(QStringLiteral("items")).toArray().size(), 2);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("session.setlist")).result.value(QStringLiteral("items")).toArray().isEmpty());
		QCOMPARE(run(QStringLiteral("session.setlist_go")).errorKind, ControlErrorKind::NotFound);
	}

	void aMarkerRunsItsActionWhenCrossed()
	{
		const ControlResult added = run(QStringLiteral("marker.add"), {{QStringLiteral("tick"), 192}, {QStringLiteral("name"), QStringLiteral("Drop")}});
		QVERIFY(added.ok);
		const QString marker = added.result.value(QStringLiteral("marker")).toString();
		QCOMPARE(run(QStringLiteral("marker.set"), {{QStringLiteral("marker"), marker}, {QStringLiteral("action"), QStringLiteral("marker.jump")}}).errorKind,
			ControlErrorKind::InvalidArgs);  // a marker command cannot be an action
		QVERIFY(run(QStringLiteral("marker.set"), {{QStringLiteral("marker"), marker}, {QStringLiteral("action"), QStringLiteral("session.set_quantisation")},
			{QStringLiteral("args"), QJsonObject{{QStringLiteral("quantisation"), QStringLiteral("two_bars")}}}}).ok);
		QCOMPARE(markeractions::advance(100, true), 0);  // the watcher's first look only sets its mark
		QCOMPARE(markeractions::advance(190, true), 0);
		QCOMPARE(markeractions::advance(200, true), 1);  // crossed 192
		QCOMPARE(markeractions::advance(50, true), 0);   // a jump back
		QCOMPARE(markeractions::advance(300, true), 1);  // crossed again after the loop
		QCOMPARE(markeractions::advance(400, false), 0);
		QCOMPARE(ProjectMarkers::instance().markers().front().action, QStringLiteral("session.set_quantisation"));
	}

	void releaseAndJumpQueue()
	{
		QVERIFY(run(QStringLiteral("session.release_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0}}).ok);
		QVERIFY(run(QStringLiteral("session.jump_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("position_ticks"), 96}}).ok);
		QCOMPARE(run(QStringLiteral("session.jump_slot"), {{QStringLiteral("track"), 7}, {QStringLiteral("scene"), 0},
			{QStringLiteral("position_ticks"), 96}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	//! WF-064: a folder's cells launch as one - the cell of the track inside it, and nothing of the folder's own column.
	void aGroupLaunchesTogether()
	{
		Song* song = Engine::getSong();
		const int column = static_cast<int>(song->tracks().size());
		auto* folder = static_cast<TrackFolder*>(Track::create(Track::Type::Folder, song));
		Track* inside = Track::create(Track::Type::Sample, song);
		QVERIFY(folder != nullptr && inside != nullptr);
		inside->setParentFolder(folder);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), column + 2}, {QStringLiteral("scenes"), 3}}).ok);
		QVERIFY(run(QStringLiteral("session.set_slot"), audioCell(column + 1, 2)).ok);
		const ControlResult launched = run(QStringLiteral("session.launch_group"), {{QStringLiteral("track"), column}, {QStringLiteral("scene"), 2}});
		QVERIFY2(launched.ok, qPrintable(launched.errorMessage));
		const QJsonArray columns = launched.result.value(QStringLiteral("launched")).toArray();
		QCOMPARE(columns.size(), 1);
		QCOMPARE(columns.first().toInt(), column + 1);
		QCOMPARE(launched.result.value(QStringLiteral("scene")).toInt(), 2);
		QCOMPARE(run(QStringLiteral("session.launch_group"), {{QStringLiteral("track"), column + 1}, {QStringLiteral("scene"), 2}}).errorKind,
			ControlErrorKind::InvalidArgs);  // not a folder
		Engine::getSong()->sessionScheduler().reset();
	}

	//! WF-071: setlist_go opens the entry as project.open does and remembers where the set stands. Last: it replaces the song.
	void theSetlistOpensItsProjects()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("first.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(path));
		QVERIFY(run(QStringLiteral("session.setlist"), {{QStringLiteral("items"), QJsonArray{path}}}).ok);
		const ControlResult went = run(QStringLiteral("session.setlist_go"));
		QVERIFY2(went.ok, qPrintable(went.errorMessage));
		QCOMPARE(went.result.value(QStringLiteral("index")).toInt(), 0);
		QCOMPARE(Engine::getSong()->projectFileName(), path);
		QCOMPARE(run(QStringLiteral("session.setlist")).result.value(QStringLiteral("index")).toInt(), 0);
		QCOMPARE(run(QStringLiteral("session.setlist_go"), {{QStringLiteral("to"), QStringLiteral("4")}}).errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(run(QStringLiteral("session.setlist"), {{QStringLiteral("items"), QJsonArray{}}}).ok);  // leave the preference empty
	}
};

QTEST_MAIN(SessionPerformTest)
#include "SessionPerformTest.moc"
