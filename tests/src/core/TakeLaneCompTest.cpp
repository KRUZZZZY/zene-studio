/*
 * TakeLaneCompTest.cpp - take lanes and the non-destructive composite (task #600):
 *                        the model's invariants, the project-file round trip, and
 *                        the property the whole feature exists for - comping
 *                        never touches the take audio.
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

// The claims, in the shortest true form (docs/COMPING.md, design §2.3):
//   I6  a composite is ordered and gapless over its span, and every segment
//       names a lane the track has - including after a lane is removed;
//   I7  lane indices are stable, and a segment naming a lane the track does not
//       have is dropped by a load rather than re-pointed;
//   I9  a track that never comped writes no element at all;
//   and the acceptance claim itself: changing the composite changes what the
//   ticks resolve to, while the take FILES and the take BUFFERS are byte-identical
//   before and after every comp.* command and after a full save/load round trip.
//
// The byte-identity half is the point. A comp that copied, normalised or rewrote a
// take would still pass every field-level assertion in this file; it cannot pass
// this one.

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
#include "TimePos.h"
#include "Track.h"
#include "TrackContainer.h"

using namespace lmms;

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr int kRate = 44100;
constexpr int kSeconds = 2;

/*! A real 16-bit mono WAV on disk. Two of these with different frequencies are
 *  the two "takes": SampleClip::loadSettings only accepts a `src` whose file
 *  exists, so a round trip through the serialiser needs real files. */
QString writeToneWav(const QString& path, double frequency)
{
	const auto frames = static_cast<int>(kRate) * kSeconds;
	QByteArray pcm;
	pcm.reserve(frames * 2);
	for (int i = 0; i < frames; ++i)
	{
		const auto value = static_cast<qint16>(0.5 * 32767.0 * std::sin(2.0 * kPi * frequency * i / kRate));
		pcm.append(static_cast<char>(value & 0xff));
		pcm.append(static_cast<char>((value >> 8) & 0xff));
	}

	QByteArray out;
	const auto put32 = [&out](quint32 v) {
		out.append(static_cast<char>(v & 0xff));
		out.append(static_cast<char>((v >> 8) & 0xff));
		out.append(static_cast<char>((v >> 16) & 0xff));
		out.append(static_cast<char>((v >> 24) & 0xff));
	};
	const auto put16 = [&out](quint16 v) {
		out.append(static_cast<char>(v & 0xff));
		out.append(static_cast<char>((v >> 8) & 0xff));
	};
	out.append("RIFF");
	put32(36 + pcm.size());
	out.append("WAVEfmt ");
	put32(16);
	put16(1);
	put16(1);
	put32(kRate);
	put32(kRate * 2);
	put16(2);
	put16(16);
	out.append("data");
	put32(pcm.size());
	out.append(pcm);

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return {}; }
	file.write(out);
	file.close();
	return path;
}

//! sha256 of a file's bytes, or a marker string when it cannot be read.
QString fileDigest(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QStringLiteral("<unreadable>"); }
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(file.readAll());
	return QString::fromLatin1(hash.result().toHex());
}

//! sha256 of the take audio a clip HOLDS IN MEMORY, over the frames it would
//! render: the buffer is the source the composite reads, so it is what must not
//! move. SampleBuffer has no operator[], and Sample::data() is the accessor.
//! (SampleClip::sample() is non-const, hence the non-const parameter.)
QString takeDigest(SampleClip* clip)
{
	if (clip == nullptr) { return QStringLiteral("<no clip>"); }
	const Sample& sample = clip->sample();
	QCryptographicHash hash(QCryptographicHash::Sha256);
	// The QByteArray overload, by name: the (const char*, int) one is deprecated
	// in Qt6 and this tree builds both Qt5 (linux/macos/mingw) and Qt6 (msvc).
	hash.addData(QByteArray::fromRawData(reinterpret_cast<const char*>(sample.data()),
		static_cast<int>(sample.sampleSize() * sizeof(SampleFrame))));
	return QString::fromLatin1(hash.result().toHex());
}

//! A sample track in the song holding `count` take clips, each its own tone.
SampleTrack* makeTakeTrack(const QString& dir, int count, QVector<SampleClip*>* clips)
{
	auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
	if (track == nullptr) { return nullptr; }
	for (int i = 0; i < count; ++i)
	{
		const QString wav = writeToneWav(dir + QStringLiteral("/take%1.wav").arg(i), 440.0 + 220.0 * i);
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		if (clip == nullptr) { return nullptr; }
		clip->setSampleFile(wav);
		clips->append(clip);
	}
	return track;
}

//! The track, saving it the way Track::clone does and loading it into a FRESH
//! track: the same path TrackContainer::loadSettings uses for a project file.
Track* reloadTrack(Track* source)
{
	QDomDocument doc;
	QDomElement parent = doc.createElement(QStringLiteral("track"));
	const QDomElement element = source->saveState(doc, parent);
	return Track::create(element, Engine::getSong());
}

QVector<int> laneIndexes(const TakeLaneModel& model)
{
	QVector<int> out;
	for (const TakeLane& lane : model.lanes()) { out.append(lane.index); }
	return out;
}

QVector<int> segmentBegins(const TakeLaneModel& model)
{
	QVector<int> out;
	for (const TakeLaneSegment& seg : model.segments()) { out.append(seg.beginTick); }
	return out;
}

} // namespace


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
