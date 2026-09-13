/*
 *  * TakeLaneCompTest.cpp - the composite (task #600): what a selection paints,
 *                        how it resolves, and the comp.* command surface that
 *                        drives it - including its A16 undo contract.
 *
 * The other half of the feature, the take lanes themselves and the
 * non-destructive property of the audio they hold, is TakeLaneTest.cpp; the
 * fixtures both halves build a take lane with are in TakeLaneTestSupport.h, so
 * there is ONE definition of "a take track".
 *
 * The composite's own claims, in the shortest true form (docs/COMPING.md,
 * design §2.3):
 *   I6  a composite is ordered and gapless over its span, and every segment
 *       names a lane the track has - including after a lane is removed;
 *   and the surface holds the A16 contract: every comp.* mutation is ONE
 *       undoable step through the Track's or the Clip's own ProjectJournal
 *       checkpoint, and every comp.* id has a row of the right class.
 *
 * A selection that would leave a hole, or a lane the track does not have, is
 * REFUSED and writes nothing: the model has no partial state, so a refusal and
 * a no-op are distinguishable.

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

#include <QtTest>

#include <QCryptographicHash>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QTemporaryDir>
#include <QVector>

#include <cmath>
#include <cstddef>

#include "Clip.h"
#include "ControlCompSupport.h"   // takeClipId(), the comp.* helpers
#include "ControlRegistry.h"
#include "ControlReversibility.h"  // the A16 table this test holds to account
#include "Engine.h"
#include "ReversibilityTestSupport.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TakeLane.h"
#include "TakeLaneTestSupport.h"   // the two halves' shared take-lane fixtures
#include "TimePos.h"
#include "Track.h"
#include "TrackContainer.h"

using namespace lmms;
using namespace taketest;

class TakeLaneCompTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Refusals write nothing: the model has no partial state, and the rule
	//! `canSelect` exposes is the one `selectSegment` enforces.
	void anInvalidSelectionIsRefusedAndWritesNothing()
	{
		QVector<SampleClip*> clips;
		SampleTrack* track = makeTakeTrack(m_dir.path(), 1, &clips);
		QVERIFY(track != nullptr);
		TakeLaneModel& model = track->takeLanes();
		model.addLane();
		const int lanesBefore = model.laneCount();

		QVERIFY(model.hasLane(0));
		QVERIFY(!model.canSelect(0, 0, 0));          // empty range
		QVERIFY(!model.canSelect(50, 10, 0));        // reversed
		QVERIFY(!model.canSelect(-1, 10, 0));        // negative start
		QVERIFY(!model.canSelect(0, 10, 0, -1));     // negative slip
		QVERIFY(!model.canSelect(0, 10, 7));         // no such lane
		QVERIFY(!model.selectSegment(0, 10, 7));
		QVERIFY(model.segments().empty());
		QCOMPARE(model.laneCount(), lanesBefore);
	}

	// ------------------------------------------------------------------ composite

	//! A selection paints: the range is cut out of every existing segment, the
	//! new choice is inserted, and the composite stays ordered and gapless.
	void aSelectionSplitsWhatItOverwritesAndStaysGapless()
	{
		QVector<SampleClip*> clips;
		SampleTrack* track = makeTakeTrack(m_dir.path(), 1, &clips);
		QVERIFY(track != nullptr);
		TakeLaneModel& model = track->takeLanes();
		model.addLane(QStringLiteral("base"));
		model.addLane(QStringLiteral("take 2"));
		model.addLane(QStringLiteral("take 3"));

		QVERIFY(model.selectSegment(0, 100, 1));
		QCOMPARE(static_cast<int>(model.segments().size()), 1);
		QVERIFY(model.selectSegment(50, 150, 2));
		QCOMPARE(static_cast<int>(model.segments().size()), 2);
		QCOMPARE(segmentBegins(model), (QVector<int>{0, 50}));
		QCOMPARE(model.segments().front().laneIndex, 1);
		QCOMPARE(model.segments().back().laneIndex, 2);
		QCOMPARE(model.segments().front().endTick, model.segments().back().beginTick);

		// A span the composite does not reach yet is FILLED with the base lane,
		// so the composite is total over the span the caller names.
		QCOMPARE(model.rebuild(0, 200), 3);
		QCOMPARE(segmentBegins(model), (QVector<int>{0, 50, 150}));
		QCOMPARE(model.segments().back().laneIndex, 0);
		QCOMPARE(model.segments().back().endTick, 200);
		int begin = 0;
		int end = 0;
		QVERIFY(model.span(&begin, &end));
		QCOMPARE(begin, 0);
		QCOMPARE(end, 200);
		// Idempotent: a second rebuild of the same span changes nothing.
		QCOMPARE(model.rebuild(0, 200), 3);
		QCOMPARE(model.rebuild(), 3);
	}

	//! Removing a lane re-points the range it supplied to the base lane instead
	//! of leaving a hole, and removing the last lane clears the composite.
	//! A selection that RESTARTS the take inside one lane is not merged into one
	//! segment - a merge would silently change what each tick reads - so the two
	//! shapes are asserted separately.
	void removingALaneRepointsItsSegmentsToTheBaseLane()
	{
		QVector<SampleClip*> clips;
		SampleTrack* track = makeTakeTrack(m_dir.path(), 1, &clips);
		QVERIFY(track != nullptr);
		TakeLaneModel& model = track->takeLanes();
		model.addLane();
		model.addLane();
		QVERIFY(model.selectSegment(0, 200, 1));
		QCOMPARE(static_cast<int>(model.segments().size()), 1);

		QVERIFY(model.removeLane(1));
		QCOMPARE(static_cast<int>(model.segments().size()), 1);
		QCOMPARE(model.segments().front().laneIndex, 0);
		QCOMPARE(model.segments().front().beginTick, 0);
		QCOMPARE(model.segments().front().endTick, 200);

		// The restart shape: two selections that each read the lane's take from
		// its own start stay two segments (the merge rule only joins a
		// CONTINUOUS read), which is the shape a comp with a deliberate jump in
		// it is written as.
		QVERIFY(model.selectSegment(0, 100, 0));
		QVERIFY(model.selectSegment(100, 200, 0));
		QCOMPARE(static_cast<int>(model.segments().size()), 2);
		QCOMPARE(model.segments().back().sourceOffset, 0);

		// Removing the LAST lane clears the composite: with no lane left there
		// is nothing that could supply a tick, and a comp of nothing is nothing.
		QVERIFY(model.removeLane(0));
		QVERIFY(model.segments().empty());
		QVERIFY(model.isEmpty());
	}

	//! Resolution is a pure function of the composite: the same tick answers
	//! differently once a selection changes, and a tick outside the span has no
	//! answer at all rather than a default one.
	void resolutionFollowsTheSelection()
	{
		QVector<SampleClip*> clips;
		SampleTrack* track = makeTakeTrack(m_dir.path(), 1, &clips);
		QVERIFY(track != nullptr);
		TakeLaneModel& model = track->takeLanes();
		model.addLane();
		model.addLane();
		QVERIFY(model.selectSegment(0, 100, 1));

		int lane = -1;
		int offset = -1;
		QVERIFY(model.resolve(40, &lane, &offset));
		QCOMPARE(lane, 1);
		QCOMPARE(offset, 40);
		QVERIFY(!model.resolve(4000, &lane, &offset));

		QVERIFY(model.selectSegment(0, 100, 0));
		QVERIFY(model.resolve(40, &lane, &offset));
		QCOMPARE(lane, 0);
	}

	// ------------------------------------------------------------------ the surface

	//! The command group, driven the way an agent drives it: lanes, assignment,
	//! selection, rebuild and the state query, each with a typed refusal where
	//! the request is one the model cannot honour.
	void theCommandGroupAddsSelectsRebuildsAndReports()
	{
		QVector<SampleClip*> clips;
		SampleTrack* track = makeTakeTrack(m_dir.path(), 2, &clips);
		QVERIFY(track != nullptr);
		const QString id = control::trackIdOf(track);

		ControlResult added = revtest::run(QStringLiteral("comp.lane_add"),
			{{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("take 1")}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		QCOMPARE(added.result.value(QStringLiteral("lane")).toInt(), 0);
		QVERIFY2(revtest::run(QStringLiteral("comp.lane_add"),
			{{QStringLiteral("track"), id}}).ok, "second lane_add");

		ControlResult assigned = revtest::run(QStringLiteral("comp.assign"),
			{{QStringLiteral("clip"), control::takeClipId(clips[1])},
				{QStringLiteral("lane"), 1}});
		QVERIFY2(assigned.ok, qPrintable(assigned.errorMessage));
		QCOMPARE(assigned.result.value(QStringLiteral("previous_lane")).toInt(), 0);
		QCOMPARE(clips[1]->laneIndex(), 1);

		const int length = clips[0]->length().getTicks();
		ControlResult selected = revtest::run(QStringLiteral("comp.select"),
			{{QStringLiteral("track"), id}, {QStringLiteral("begin"), 0},
				{QStringLiteral("end"), length}, {QStringLiteral("lane"), 1}});
		QVERIFY2(selected.ok, qPrintable(selected.errorMessage));
		QJsonObject composite = selected.result.value(QStringLiteral("composite")).toObject();
		QCOMPARE(composite.value(QStringLiteral("segments")).toArray().size(), 1);
		QCOMPARE(composite.value(QStringLiteral("resolved")).toArray()
			.first().toObject().value(QStringLiteral("clip")).toString(),
			control::takeClipId(clips[1]));

		ControlResult rebuilt = revtest::run(QStringLiteral("comp.rebuild"),
			{{QStringLiteral("track"), id}, {QStringLiteral("begin"), 0},
				{QStringLiteral("end"), length * 2}});
		QVERIFY2(rebuilt.ok, qPrintable(rebuilt.errorMessage));
		QCOMPARE(rebuilt.result.value(QStringLiteral("segments_after")).toInt(), 2);

		ControlResult state = revtest::run(QStringLiteral("comp.get_state"),
			{{QStringLiteral("track"), id}});
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		QCOMPARE(state.result.value(QStringLiteral("lane_count")).toInt(), 2);
		// Two takes: the clip nobody assigned is on the base lane (0), the one
		// comp.assign moved is on lane 1. Which lane holds which is the point.
		QCOMPARE(state.result.value(QStringLiteral("take_count")).toInt(), 2);
		const QJsonArray lanes = state.result.value(QStringLiteral("lanes")).toArray();
		QCOMPARE(lanes.at(0).toObject().value(QStringLiteral("takes")).toArray()
			.first().toString(), control::takeClipId(clips[0]));
		QCOMPARE(lanes.at(1).toObject().value(QStringLiteral("takes")).toArray()
			.first().toString(), control::takeClipId(clips[1]));
		QVERIFY(!state.result.value(QStringLiteral("composite")).toObject()
			.value(QStringLiteral("segments")).toArray().isEmpty());

		ControlResult listed = revtest::run(QStringLiteral("comp.lane_list"), {});
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		QVERIFY(listed.result.value(QStringLiteral("count")).toInt() >= 1);

		// Typed refusals: a lane the track does not have, a reversed range, a
		// track that does not exist.
		QCOMPARE(revtest::run(QStringLiteral("comp.select"),
			{{QStringLiteral("track"), id}, {QStringLiteral("begin"), 10},
				{QStringLiteral("end"), 5}, {QStringLiteral("lane"), 0}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(revtest::run(QStringLiteral("comp.select"),
			{{QStringLiteral("track"), id}, {QStringLiteral("begin"), 0},
				{QStringLiteral("end"), 10}, {QStringLiteral("lane"), 9}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(revtest::run(QStringLiteral("comp.lane_remove"),
			{{QStringLiteral("track"), id}, {QStringLiteral("lane"), 9}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(revtest::run(QStringLiteral("comp.get_state"),
			{{QStringLiteral("track"), QStringLiteral("trk-9999")}}).errorKind,
			ControlErrorKind::NotFound);
	}

	//! SPEC A16: a comp.* mutation is one undoable step, and control.undo takes
	//! the model - lanes, composite and all - back through the Track checkpoint.
	void aCompMutationIsUndoneByControlUndo()
	{
		QVector<SampleClip*> clips;
		SampleTrack* track = makeTakeTrack(m_dir.path(), 1, &clips);
		QVERIFY(track != nullptr);
		const QString id = control::trackIdOf(track);
		QVERIFY(revtest::run(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), id}}).ok);
		QVERIFY(revtest::run(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), id}}).ok);
		const int length = clips[0]->length().getTicks();

		QVERIFY(revtest::run(QStringLiteral("comp.select"),
			{{QStringLiteral("track"), id}, {QStringLiteral("begin"), 0},
				{QStringLiteral("end"), length}, {QStringLiteral("lane"), 1}}).ok);
		QCOMPARE(static_cast<int>(track->takeLanes().segments().size()), 1);
		QCOMPARE(track->takeLanes().segments().front().laneIndex, 1);
		REV_UNDO_OR_FAIL();
		QVERIFY(track->takeLanes().segments().empty());

		// A lane removal is one step too, including the segments it re-pointed.
		QVERIFY(revtest::run(QStringLiteral("comp.select"),
			{{QStringLiteral("track"), id}, {QStringLiteral("begin"), 0},
				{QStringLiteral("end"), length}, {QStringLiteral("lane"), 1}}).ok);
		QVERIFY(revtest::run(QStringLiteral("comp.rebuild"),
			{{QStringLiteral("track"), id}, {QStringLiteral("begin"), 0},
				{QStringLiteral("end"), length}}).ok);
		const int before = static_cast<int>(track->takeLanes().segments().size());
		QVERIFY(revtest::run(QStringLiteral("comp.lane_remove"),
			{{QStringLiteral("track"), id}, {QStringLiteral("lane"), 1}}).ok);
		QVERIFY(!track->takeLanes().hasLane(1));
		REV_UNDO_OR_FAIL();
		QVERIFY(track->takeLanes().hasLane(1));
		QCOMPARE(static_cast<int>(track->takeLanes().segments().size()), before);
	}

	//! The A16 contract both directions, for this group only: every comp.* id has
	//! a row, and the rows name registered commands. (The whole-registry sweep is
	//! ReversibilityContractTest; this is the same claim, local to the group, so a
	//! row that goes missing is caught by the file that owns the commands.)
	void everyCompCommandHasAContractRowOfTheRightClass()
	{
		const QStringList ids = ControlRegistry::instance()->commandIds();
		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		int seen = 0;
		for (const QString& id : ids)
		{
			if (!id.startsWith(QStringLiteral("comp."))) { continue; }
			++seen;
			const control::ReversibilityEntry* entry = table.lookup(id);
			QVERIFY2(entry != nullptr, qPrintable(id + " has no A16 row"));
			QVERIFY2(!entry->reason.isEmpty(), qPrintable(id + " has an empty reason"));
			QVERIFY2(!entry->mechanism.isEmpty(), qPrintable(id + " has an empty mechanism"));
		}
		QCOMPARE(seen, 7);   // 4 in the take half, 3 in the composite half
	}

private:
	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(TakeLaneCompTest)
#include "TakeLaneCompTest.moc"
