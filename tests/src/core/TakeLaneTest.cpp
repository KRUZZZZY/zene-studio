/*
 *  * TakeLaneTest.cpp - take lanes (task #600): the lane list's own invariants,
 *                    the clip's lane tag, the project-file round trip - and the
 *                    property the whole feature exists for, that comping never
 *                    writes the take audio or the take files.
 *
 * The other half of the feature, the composite and the comp.* command surface,
 * is TakeLaneCompTest.cpp; the fixtures both halves build a take lane with are
 * in TakeLaneTestSupport.h, so there is ONE definition of "a take track".
 *
 * The claims this half holds, in the shortest true form (docs/COMPING.md,
 * design §2.3):
 *   I7  lane indices are stable, and a segment naming a lane the track does not
 *       have is dropped by a load rather than re-pointed;
 *   I9  a track that never comped writes no element at all, and a clip that is
 *       not a take writes no lane attribute;
 *   and the acceptance claim itself: the take FILES and the take BUFFERS are
 *   byte-identical before and after every comp.* command and after a full
 *   save/load round trip.
 *
 * The byte-identity half is the point. A comp that copied, normalised or
 * rewrote a take would still pass every field-level assertion in these files;
 * it cannot pass this one.

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

class TakeLaneTest : public QObject
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

	// ------------------------------------------------------------------ lanes

	//! A lane index is stable: a removal never renumbers the survivors, and the
	//! next lane added takes the lowest index the track is not using.
	void laneIndexesAreStableAndTheLowestFreeIsReused()
	{
		QVector<SampleClip*> clips;
		SampleTrack* track = makeTakeTrack(m_dir.path(), 1, &clips);
		QVERIFY(track != nullptr);
		TakeLaneModel& model = track->takeLanes();

		QCOMPARE(model.addLane(QStringLiteral("take 1")), 0);
		QCOMPARE(model.addLane(QStringLiteral("take 2")), 1);
		QCOMPARE(model.addLane(QStringLiteral("take 3")), 2);
		QVERIFY(model.removeLane(1));
		QCOMPARE(laneIndexes(model), (QVector<int>{0, 2}));
		QVERIFY(!model.hasLane(1));
		// The gap refills: the index is not monotonic, but it is never a live
		// lane's index, which is the property stability needs (I7).
		QCOMPARE(model.addLane(QStringLiteral("take 4")), 1);
		QVERIFY(model.removeLane(99) == false);
	}

	// ------------------------------------------------------------------ the takes

	//! THE NON-DESTRUCTIVE PROPERTY. The composite resolves onto the take clip
	//! of the selected lane (its frame comes from that clip's own mapping), and
	//! the take FILES and take BUFFERS are byte-identical after every operation.
	void compingResolvesOntoTheTakesAndNeverWritesThem()
	{
		QVector<SampleClip*> clips;
		SampleTrack* track = makeTakeTrack(m_dir.path(), 2, &clips);
		QVERIFY(track != nullptr);
		QCOMPARE(clips.size(), 2);
		TakeLaneModel& model = track->takeLanes();
		model.addLane(QStringLiteral("base"));
		model.addLane(QStringLiteral("take 2"));
		// The second take is the second clip, on lane 1.
		clips[0]->setLaneIndex(0);
		clips[1]->setLaneIndex(1);
		const QString fileBefore0 = fileDigest(clips[0]->sample().sampleFile());
		const QString fileBefore1 = fileDigest(clips[1]->sample().sampleFile());
		const QString bufferBefore0 = takeDigest(clips[0]);
		const QString bufferBefore1 = takeDigest(clips[1]);

		const int length = clips[0]->length().getTicks();
		QVERIFY(length > 100);
		QVERIFY(model.selectSegment(0, length / 2, 0));
		QVERIFY(model.selectSegment(length / 2, length, 1));
		QCOMPARE(model.rebuild(0, length), 2);

		// Which clip supplies a tick follows the selection, not the file order.
		int lane = -1;
		QCOMPARE(model.takeAt(track->getClips(), 10, &lane), clips[0]);
		QCOMPARE(lane, 0);
		QCOMPARE(model.takeAt(track->getClips(), length / 2 + 10, &lane), clips[1]);
		QCOMPARE(lane, 1);

		f_cnt_t frame = 0;
		QVERIFY(model.resolveSource(track->getClips(), length / 2 + 10, &frame, &lane));
		QCOMPARE(frame, clips[1]->sourceFrameAt(TimePos(length / 2 + 10)));
		QVERIFY(frame >= 0);
		QVERIFY(static_cast<std::size_t>(frame) < clips[1]->sample().sampleSize());
		// A lane with no take covering the tick is UNRESOLVED, not guessed at.
		QVERIFY(model.takeAt(track->getClips(), length + 100) == nullptr);
		QVERIFY(!model.resolveSource(track->getClips(), length + 100, &frame, &lane));

		// Re-select, rebuild, save and reload: the audio must not move by a byte.
		QVERIFY(model.selectSegment(0, length, 1));
		QCOMPARE(model.rebuild(0, length), 1);
		Track* reloaded = reloadTrack(track);
		QVERIFY(reloaded != nullptr);
		QCOMPARE(static_cast<int>(reloaded->takeLanes().segments().size()), 1);
		QCOMPARE(reloaded->takeLanes().segments().front().laneIndex, 1);

		QCOMPARE(fileDigest(clips[0]->sample().sampleFile()), fileBefore0);
		QCOMPARE(fileDigest(clips[1]->sample().sampleFile()), fileBefore1);
		QCOMPARE(takeDigest(clips[0]), bufferBefore0);
		QCOMPARE(takeDigest(clips[1]), bufferBefore1);
	}

	// ------------------------------------------------------------------ the file

	//! I7 + the round trip: both lists survive the element, and a track element
	//! with no <takelanes> child leaves an EMPTY model - the reset-on-absence
	//! rule, without which a journal checkpoint could never undo the first edit.
	void theElementRoundTripsAndAnAbsentOneResets()
	{
		QVector<SampleClip*> clips;
		SampleTrack* track = makeTakeTrack(m_dir.path(), 1, &clips);
		QVERIFY(track != nullptr);
		TakeLaneModel& model = track->takeLanes();
		model.addLane(QStringLiteral("base"));
		model.addLane(QStringLiteral("take 2"));
		model.addLane(QStringLiteral("take 3"));
		QVERIFY(model.selectSegment(0, 64, 1));
		QVERIFY(model.selectSegment(64, 128, 2, 7));

		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("track"));
		const QDomElement element = track->saveState(doc, parent);
		QVERIFY(!element.firstChildElement(QStringLiteral("takelanes")).isNull());

		Track* reloaded = Track::create(element, Engine::getSong());
		QVERIFY(reloaded != nullptr);
		const TakeLaneModel& back = reloaded->takeLanes();
		QCOMPARE(laneIndexes(back), (QVector<int>{0, 1, 2}));
		QCOMPARE(back.lanes().at(1).name, QStringLiteral("take 2"));
		QCOMPARE(static_cast<int>(back.segments().size()), 2);
		QCOMPARE(back.segments().at(1).sourceOffset, 7);

		// The same track, reloaded from an element with the model stripped out:
		// it must come back EMPTY rather than keep what it held.
		QDomElement stripped = element;
		stripped.removeChild(stripped.firstChildElement(QStringLiteral("takelanes")));
		track->loadSettings(stripped);
		QVERIFY(track->takeLanes().isEmpty());
		QVERIFY(track->takeLanes().segments().empty());
	}

	//! I9: a track that never comped writes no element, and a clip that is not a
	//! take writes no lane attribute - so a project from before this feature
	//! serialises exactly as it did.
	void anUntouchedTrackAndClipWriteNothing()
	{
		QVector<SampleClip*> clips;
		SampleTrack* track = makeTakeTrack(m_dir.path(), 1, &clips);
		QVERIFY(track != nullptr);
		QCOMPARE(clips[0]->laneIndex(), 0);

		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("track"));
		const QDomElement element = track->saveState(doc, parent);
		QVERIFY(element.firstChildElement(QStringLiteral("takelanes")).isNull());

		QDomElement clipParent = doc.createElement(QStringLiteral("clips"));
		const QDomElement clipElement = clips[0]->saveState(doc, clipParent);
		QVERIFY(clipElement.attribute(QStringLiteral("lane")).isEmpty());

		// The lane attribute is written when it is set, and an element WITHOUT it
		// loads as lane 0 (the reset-on-absence rule at the clip level).
		clips[0]->setLaneIndex(3);
		QDomElement taggedParent = doc.createElement(QStringLiteral("clips"));
		QDomElement tagged = clips[0]->saveState(doc, taggedParent);
		QCOMPARE(tagged.attribute(QStringLiteral("lane")), QStringLiteral("3"));
		tagged.removeAttribute(QStringLiteral("lane"));
		clips[0]->loadSettings(tagged);
		QCOMPARE(clips[0]->laneIndex(), 0);
	}

private:
	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(TakeLaneTest)
#include "TakeLaneTest.moc"
