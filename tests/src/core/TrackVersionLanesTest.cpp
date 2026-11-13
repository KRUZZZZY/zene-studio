/*
 * TrackVersionLanesTest.cpp - track versions used as comp lanes (Q050-275)
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
/*! Q050-275: a track version used as a comp source is copied into a take lane at the moment of use
 *  (ED-028 family); the copy survives a version switch, the version_to_lane call is one undo step,
 *  bad input is refused, and a comp keeps its lane when the version is removed. */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include <cstddef>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Song.h"
#include "TrackVersions.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The clip of \p track on take lane \p lane, or nullptr.
Clip* clipOnLane(const Track* track, int lane)
{
	for (Clip* clip : track->getClips()) { if (clip->laneIndex() == lane) { return clip; } }
	return nullptr;
}

//! The index of the take lane of \p track named \p name, or -1.
int laneNamed(const Track* track, const QString& name)
{
	for (const auto& lane : track->takeLanes().lanes()) { if (lane.name == name) { return lane.index; } }
	return -1;
}

//! The name of the take lane of \p track at \p index, or an empty string.
QString laneNameAt(const Track* track, int index)
{
	for (const auto& lane : track->takeLanes().lanes()) { if (lane.index == index) { return lane.name; } }
	return QString();
}

//! The segments of the track's comp, as comp.get_state reports them.
QJsonArray compSegments(const QString& trackId)
{
	return run(QStringLiteral("comp.get_state"), {{QStringLiteral("track"), trackId}}).result
		.value(QStringLiteral("composite")).toObject().value(QStringLiteral("segments")).toArray();
}

//! Stores two versions on \p track. The track already holds "A-clip" (not yet stored).
//! 'A' stores that clip. 'B' is saved next, so it becomes active and holds a copy of A. The clips are then
//! replaced by "B-clip" and saved under the active 'B', which leaves 'A' holding "A-clip".
void twoVersions(Track* track, const QString& id)
{
	QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}}).ok);
	QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("B")}}).ok);
	track->deleteClips();
	track->createClip(TimePos(384))->setName(QStringLiteral("B-clip"));
	QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("B")}}).ok);
}
} // namespace

class TrackVersionLanesTest : public QObject
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

	// A version used as a comp source is copied into a take lane at the moment of use (later edits to
	// the version do not follow). The copy is appended to every stored version, so a switch keeps the takes.
	void versionBecomesACompLane()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		const QString id = control::trackIdOf(track);
		const int aClipId = track->createClip(TimePos(0))->id();
		track->getClips().front()->setName(QStringLiteral("A-clip"));
		twoVersions(track, id);

		const ControlResult made = run(QStringLiteral("track.version_to_lane"),
			{{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		const int lane = made.result.value(QStringLiteral("lane")).toInt(-1);
		QVERIFY(lane > 0);
		QCOMPARE(laneNamed(track, QStringLiteral("A")), lane);
		QCOMPARE(track->getClips().size(), std::size_t{2});
		Clip* copy = clipOnLane(track, lane);
		QVERIFY(copy != nullptr);
		QCOMPARE(copy->name(), QStringLiteral("A-clip"));
		QVERIFY(copy->startPosition() == TimePos(0));
		QVERIFY2(copy->id() != aClipId, "the lane copy reused the stored clip's id");
		QCOMPARE(clipOnLane(track, 0)->name(), QStringLiteral("B-clip"));
		QVERIFY2(track->takeLanes().hasLane(0) && laneNameAt(track, 0) != QStringLiteral("A"), "the base lane is missing or named A");
		QVERIFY(control::clipIdOf(copy) != control::clipIdOf(clipOnLane(track, 0)));

		QVERIFY(run(QStringLiteral("comp.select"), {{QStringLiteral("track"), id}, {QStringLiteral("lane"), lane},
			{QStringLiteral("begin"), 0}, {QStringLiteral("end"), 192}}).ok);
		const QJsonArray segments = compSegments(id);
		QCOMPARE(segments.size(), 1);
		QCOMPARE(segments.first().toObject().value(QStringLiteral("lane")).toInt(), lane);
		QCOMPARE(segments.first().toObject().value(QStringLiteral("begin")).toInt(), 0);
		QCOMPARE(segments.first().toObject().value(QStringLiteral("end")).toInt(), 192);
	}

	void laneCopySurvivesAVersionSwitch()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		const QString id = control::trackIdOf(track);
		track->createClip(TimePos(0))->setName(QStringLiteral("A-clip"));
		twoVersions(track, id);
		const ControlResult made = run(QStringLiteral("track.version_to_lane"),
			{{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		const int lane = made.result.value(QStringLiteral("lane")).toInt(-1);
		QVERIFY(run(QStringLiteral("comp.select"), {{QStringLiteral("track"), id}, {QStringLiteral("lane"), lane},
			{QStringLiteral("begin"), 0}, {QStringLiteral("end"), 192}}).ok);

		QVERIFY2(run(QStringLiteral("track.version_switch"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}}).ok,
			"switching to A failed");
		QVERIFY2(clipOnLane(track, lane) != nullptr, "the lane copy is gone after switching to A");
		QCOMPARE(compSegments(id).first().toObject().value(QStringLiteral("lane")).toInt(), lane);

		QVERIFY2(run(QStringLiteral("track.version_switch"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("B")}}).ok,
			"switching back to B failed");
		QVERIFY2(clipOnLane(track, lane) != nullptr, "the lane copy is gone after switching back to B");
		QCOMPARE(compSegments(id).first().toObject().value(QStringLiteral("lane")).toInt(), lane);
	}

	void versionToLaneIsOneUndoStep()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		const QString id = control::trackIdOf(track);
		track->createClip(TimePos(0))->setName(QStringLiteral("A-clip"));
		twoVersions(track, id);
		const TrackVersions::Set before = TrackVersions::instance().of(track->id());
		const std::size_t clipsBefore = track->getClips().size();

		const ControlResult made = run(QStringLiteral("track.version_to_lane"),
			{{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		const int lane = made.result.value(QStringLiteral("lane")).toInt(-1);

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!track->takeLanes().hasLane(lane));
		QCOMPARE(track->takeLanes().laneCount(), 0);
		QCOMPARE(track->getClips().size(), clipsBefore);
		QVERIFY(TrackVersions::instance().of(track->id()) == before);

		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QVERIFY(track->takeLanes().hasLane(lane));
		QCOMPARE(track->getClips().size(), clipsBefore + 1);
		QVERIFY(!(TrackVersions::instance().of(track->id()) == before));
	}

	void versionToLaneRefusesBadInput()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		const QString id = control::trackIdOf(track);
		QCOMPARE(run(QStringLiteral("track.version_to_lane"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}}).errorKind,
			ControlErrorKind::NotFound);

		track->createClip(TimePos(0))->setName(QStringLiteral("A-clip"));
		QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}}).ok);
		QCOMPARE(run(QStringLiteral("track.version_to_lane"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral(" ")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("track.version_to_lane"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("missing")}}).errorKind,
			ControlErrorKind::NotFound);

		// A stored version that is not XML: Refused, nothing changed, and no undo step recorded.
		TrackVersions::Set bad = TrackVersions::instance().of(track->id());
		bad.clips.insert(QStringLiteral("A"), QStringLiteral("not xml"));
		TrackVersions::instance().set(track->id(), bad);
		const std::size_t clipsBefore = track->getClips().size();
		QCOMPARE(run(QStringLiteral("track.version_to_lane"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}}).errorKind,
			ControlErrorKind::Refused);
		QCOMPARE(track->getClips().size(), clipsBefore);
		QCOMPARE(track->takeLanes().laneCount(), 0);
		QVERIFY(TrackVersions::instance().of(track->id()) == bad);
		// The next undo reverts the save of A (its closure puts the empty set back), not the refused call.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(TrackVersions::instance().of(track->id()).clips.isEmpty());
	}

	void compLaneSurvivesRemovingItsVersion()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		const QString id = control::trackIdOf(track);
		track->createClip(TimePos(0))->setName(QStringLiteral("A-clip"));
		twoVersions(track, id);
		const ControlResult made = run(QStringLiteral("track.version_to_lane"),
			{{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		const int lane = made.result.value(QStringLiteral("lane")).toInt(-1);
		QVERIFY(run(QStringLiteral("comp.select"), {{QStringLiteral("track"), id}, {QStringLiteral("lane"), lane},
			{QStringLiteral("begin"), 0}, {QStringLiteral("end"), 192}}).ok);

		QVERIFY(run(QStringLiteral("track.version_remove"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}}).ok);
		QVERIFY(track->takeLanes().hasLane(lane));
		QVERIFY2(clipOnLane(track, lane) != nullptr, "removing the version took the lane copy with it");
		const QJsonArray segments = compSegments(id);
		QCOMPARE(segments.size(), 1);
		QCOMPARE(segments.first().toObject().value(QStringLiteral("lane")).toInt(), lane);
	}
};

QTEST_MAIN(TrackVersionLanesTest)
#include "TrackVersionLanesTest.moc"
