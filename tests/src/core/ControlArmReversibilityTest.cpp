/*
 * ControlArmReversibilityTest.cpp - the single-track arm is irreversible too
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

/*! The batch arm's sibling: track.set_arm arms ONE route, and the capture it starts writes a
 *  take file that outlives the capture. Its recorded inverse (this command with armed: false)
 *  stopped the capture and retired the journal but left the take on disk, so control.undo
 *  claimed to reverse an arming it could not restore. The row is irreversible now, the arming
 *  records no inverse, and control.undo refuses it typed while the route and the take stay.
 *  The batch's own regression is UndoHonestyTest::armAllRefusesUndoThatWouldLeaveItsTakesBehind.
 *  Cluster 4736 carries the other half of the single-track arm's contract here: the route the
 *  command addresses follows the TRACK by its stable id, not the row it happens to stand on. */

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MultiTrackRecorder.h"
#include "Song.h"
#include "TrackRecorder.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The row @a id occupies in the song container, or -1. Cluster 4736: a record route is bound to
//! the track, so a test has to be able to say where that track is standing this moment.
int indexOfTrackId(const QString& id)
{
	const auto& list = Engine::getSong()->tracks();
	for (int i = 0; i < static_cast<int>(list.size()); ++i)
	{
		if (control::trackIdOf(list[i]) == id) { return i; }
	}
	return -1;
}

} // namespace

class ControlArmReversibilityTest : public QObject
{
	Q_OBJECT

private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Arming one track: control.undo refuses it, and the route and the take both survive.
	void armingOneTrackRefusesUndoThatWouldLeaveItsTakeBehind()
	{
		// The default take sits beside the working directory's recovery file: keep it private.
		QTemporaryDir takes;
		QVERIFY(takes.isValid());
		const auto restoreDir = qScopeGuard([previous = ConfigManager::inst()->workingDir()] { ConfigManager::inst()->setWorkingDir(previous); });
		ConfigManager::inst()->setWorkingDir(takes.path());

		const ControlResult added = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString track = added.result.value(QStringLiteral("track")).toString();
		QVERIFY(!track.isEmpty());

		const ControlResult armed = run(QStringLiteral("track.set_arm"),
			{{QStringLiteral("track"), track}, {QStringLiteral("armed"), true}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		const QString take = armed.result.value(QStringLiteral("file")).toString();
		QVERIFY2(!take.isEmpty(), "the arm did not report its take file");
		QVERIFY2(QFile::exists(take), qPrintable(take));
		ControlResult error;
		QVERIFY(control::trackRecordArmed(control::resolveTrack(track, &error)));

		const ControlResult undo = run(QStringLiteral("control.undo"));
		// The refused undo must leave both the armed route and the take file alone.
		const bool stillArmed = control::trackRecordArmed(control::resolveTrack(track, &error));
		const bool takeRemains = QFile::exists(take);
		// Stop the capture on either implementation, so a RED run leaves no route writing.
		QVERIFY(run(QStringLiteral("track.set_arm"),
			{{QStringLiteral("track"), track}, {QStringLiteral("armed"), false}}).ok);

		QVERIFY2(!undo.ok, "control.undo claimed it could remove the take from track.set_arm");
		QCOMPARE(undo.errorKind, ControlErrorKind::Irreversible);
		QVERIFY2(undo.errorMessage.contains(QStringLiteral("track.set_arm")), qPrintable(undo.errorMessage));
		QVERIFY2(stillArmed, "the refused undo disarmed the route");
		QVERIFY2(takeRemains, "the refused undo took the take away");
	}

	/*! Cluster 4736: the capture a track started belongs to the TRACK, not to its row. Removing
	 *  another track above it moves its row, and track.set_arm resolved the new row: the disarm
	 *  stopped a route that was never armed and left the real capture writing to disk. */
	void disarmingATrackAfterAnotherTracksRemovalStopsItsOwnCapture()
	{
		QTemporaryDir takes;
		QVERIFY(takes.isValid());
		const auto restoreDir = qScopeGuard([previous = ConfigManager::inst()->workingDir()] { ConfigManager::inst()->setWorkingDir(previous); });
		ConfigManager::inst()->setWorkingDir(takes.path());
		MultiTrackRecorder& recorder = Engine::audioEngine()->recorder();
		const auto stopCaptures = qScopeGuard([&recorder] { recorder.disarmAll(); });

		const ControlResult above = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}});
		QVERIFY2(above.ok, qPrintable(above.errorMessage));
		const ControlResult armedOne = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}});
		QVERIFY2(armedOne.ok, qPrintable(armedOne.errorMessage));
		const QString aboveId = above.result.value(QStringLiteral("track")).toString();
		const QString armedId = armedOne.result.value(QStringLiteral("track")).toString();
		QVERIFY(!aboveId.isEmpty() && !armedId.isEmpty());
		QVERIFY(indexOfTrackId(aboveId) + 1 == indexOfTrackId(armedId));

		const ControlResult armed = run(QStringLiteral("track.set_arm"),
			{{QStringLiteral("track"), armedId}, {QStringLiteral("armed"), true}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		const int route = armed.result.value(QStringLiteral("route")).toInt();
		QVERIFY(route >= 1 && route < recorder.trackCount());
		QVERIFY(recorder.track(route).isArmed());
		const QString journal = armed.result.value(QStringLiteral("journal")).toString();
		QVERIFY(!journal.isEmpty());
		QVERIFY(QFileInfo::exists(journal));

		// The row above goes; the capture does not. The armed track's row is now one lower.
		const ControlResult removed = run(QStringLiteral("track.remove"), {{QStringLiteral("track"), aboveId}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(indexOfTrackId(armedId), route - 1);
		QVERIFY2(recorder.track(route).isArmed(), "removing another track stopped the armed route");

		const ControlResult disarmed = run(QStringLiteral("track.set_arm"),
			{{QStringLiteral("track"), armedId}, {QStringLiteral("armed"), false}});
		QVERIFY2(disarmed.ok, qPrintable(disarmed.errorMessage));
		QVERIFY2(!recorder.track(route).isArmed(),
			"the disarm resolved the track's NEW row and left its capture writing");
		QVERIFY2(!QFileInfo::exists(journal), "the stopped capture kept its take journal");
		ControlResult error;
		QVERIFY2(!control::trackRecordArmed(control::resolveTrack(armedId, &error)),
			"the track still reports an armed route after its disarm");
	}

	/*! Cluster 4736, the other half: once a track MOVES onto the row of another track's armed
	 *  route, the row is not its capture. It may not stop it, arm onto it, or show it as its own,
	 *  and the batch verb resolves the same binding as the single one. */
	void aTrackThatMovedOntoAnArmedRouteCannotStopAnotherTracksCapture()
	{
		QTemporaryDir takes;
		QVERIFY(takes.isValid());
		const auto restoreDir = qScopeGuard([previous = ConfigManager::inst()->workingDir()] { ConfigManager::inst()->setWorkingDir(previous); });
		ConfigManager::inst()->setWorkingDir(takes.path());
		MultiTrackRecorder& recorder = Engine::audioEngine()->recorder();
		const auto stopCaptures = qScopeGuard([&recorder] { recorder.disarmAll(); });

		const ControlResult moverAdd = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}});
		QVERIFY2(moverAdd.ok, qPrintable(moverAdd.errorMessage));
		const ControlResult ownerAdd = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}});
		QVERIFY2(ownerAdd.ok, qPrintable(ownerAdd.errorMessage));
		const QString mover = moverAdd.result.value(QStringLiteral("track")).toString();
		const QString owner = ownerAdd.result.value(QStringLiteral("track")).toString();
		QVERIFY(!mover.isEmpty() && !owner.isEmpty());

		const ControlResult armed = run(QStringLiteral("track.set_arm"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("armed"), true}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		const int route = armed.result.value(QStringLiteral("route")).toInt();
		QVERIFY(route >= 1 && route < recorder.trackCount());
		const QString journal = armed.result.value(QStringLiteral("journal")).toString();
		QVERIFY(!journal.isEmpty());
		QVERIFY(QFileInfo::exists(journal));

		// The track above moves onto the armed row; the armed track now sits one row lower.
		const ControlResult moved = run(QStringLiteral("track.move"),
			{{QStringLiteral("track"), mover}, {QStringLiteral("index"), route}});
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QCOMPARE(indexOfTrackId(mover), route);
		QCOMPARE(indexOfTrackId(owner), route - 1);
		QVERIFY2(recorder.track(route).isArmed(), "moving a track stopped the armed route");

		// Each row shows the capture its own track started.
		ControlResult error;
		QVERIFY2(control::trackRecordArmed(control::resolveTrack(owner, &error)),
			"the armed track lost the arm state its row no longer holds");
		QVERIFY2(!control::trackRecordArmed(control::resolveTrack(mover, &error)),
			"the track that moved onto the row shows another track's capture as armed");

		// The row is not the mover's capture: it cannot stop it, and cannot arm onto it either.
		const ControlResult refused = run(QStringLiteral("track.set_arm"),
			{{QStringLiteral("track"), mover}, {QStringLiteral("armed"), false}});
		QVERIFY2(!refused.ok, "a track that moved onto the row stopped another track's capture");
		QVERIFY2(refused.errorMessage.contains(owner), qPrintable(refused.errorMessage));
		QVERIFY(recorder.track(route).isArmed());
		const ControlResult busy = run(QStringLiteral("track.set_arm"),
			{{QStringLiteral("track"), mover}, {QStringLiteral("armed"), true}});
		QVERIFY(!busy.ok);
		QVERIFY(recorder.track(route).isArmed());

		// track.set_arm_many resolves the same binding: it stops the owner's capture, not the row's.
		const ControlResult batch = run(QStringLiteral("track.set_arm_many"),
			{{QStringLiteral("tracks"), QJsonArray{owner}}, {QStringLiteral("armed"), false}});
		QVERIFY2(batch.ok, qPrintable(batch.errorMessage));
		QVERIFY2(!recorder.track(route).isArmed(), "the batch disarm left the owner's capture writing");
		QVERIFY2(!QFileInfo::exists(journal), "the stopped capture kept its take journal");
	}
};

QTEST_GUILESS_MAIN(ControlArmReversibilityTest)

#include "ControlArmReversibilityTest.moc"
