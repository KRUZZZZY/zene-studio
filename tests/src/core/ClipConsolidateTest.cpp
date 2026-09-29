/*
 * ClipConsolidateTest.cpp - R3.2: clip.consolidate, a region's clips become one clip
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

/*! Every claim clip.consolidate's description makes, held against the audio the new clip
 *  actually carries (its SampleBuffer, loaded back from the rendered file):
 *    * two clips with a gap between them become one clip of their extent whose audio is
 *      each clip's level where it was and silence in the gap; one undo brings both back;
 *    * a muted TRACK still consolidates its audio (the handle ignores the track mute);
 *    * clip gain is baked in (the clips' own audio), the composite's lane gate is honoured
 *      and the composite then names the one clip, and undo restores the composite;
 *    * a clip crossing the region's edge, an instrument track and an empty region are
 *      refused and change nothing; a default path is never reused.
 */

#include <QtTest>

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <memory>
#include <vector>

#include "AudioEngine.h"
#include "ClipEdits.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TakeLane.h"

using namespace lmms;
using namespace revtest;

namespace
{

SampleClip* addTake(Track* track, tick_t position, float level, int lane = 0)
{
	auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(position)));
	Q_ASSERT(clip != nullptr);
	const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	std::vector<SampleFrame> data(static_cast<std::size_t>(rate / 2), SampleFrame(level, level));
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	clip->movePosition(TimePos(position));
	clip->setLaneIndex(lane);
	return clip;
}

std::vector<SampleClip*> clipsOf(Track* track)
{
	std::vector<SampleClip*> out;
	for (Clip* clip : track->getClips()) { out.push_back(dynamic_cast<SampleClip*>(clip)); }
	return out;
}

//! Mean |left| of \a clip's audio over the ticks [from, to), relative to the clip's start.
double levelAt(SampleClip* clip, tick_t from, tick_t to)
{
	const auto buffer = clip->sample().buffer();
	const double fpt = Engine::framesPerTick(buffer->sampleRate());
	const auto a = static_cast<std::size_t>(from * fpt);
	const auto b = std::min(buffer->size(), static_cast<std::size_t>(to * fpt));
	double sum = 0.0;
	for (std::size_t i = a; i < b; ++i) { sum += std::fabs(buffer->data()[i][0]); }
	return b > a ? sum / static_cast<double>(b - a) : -1.0;
}

} // namespace

class ClipConsolidateTest : public QObject
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

	void twoClipsAndAGapBecomeOneClipAndUndoBringsThemBack()
	{
		Track* track = newSampleTrack();
		SampleClip* first = addTake(track, 0, 0.5f);
		const tick_t length = first->length().getTicks();
		addTake(track, 2 * length, 0.25f);

		const ControlResult done = run(QStringLiteral("clip.consolidate"), args(track, {}));
		QVERIFY2(done.ok, qPrintable(done.errorMessage));
		QCOMPARE(done.result.value(QStringLiteral("consolidated")).toArray().size(), 2);
		QCOMPARE(done.result.value(QStringLiteral("end")).toInt(), 3 * length);
		QVERIFY(QFileInfo::exists(done.result.value(QStringLiteral("path")).toString()));
		const auto after = clipsOf(track);
		QCOMPARE(after.size(), std::size_t{1});
		QCOMPARE(after[0]->startPosition().getTicks(), 0);
		QCOMPARE(after[0]->length().getTicks(), 3 * length);
		const tick_t pad = length / 8;
		std::printf("CONSOLIDATE_EVIDENCE first %.4f gap %.4f second %.4f\n",
			levelAt(after[0], pad, length - pad), levelAt(after[0], length + pad, 2 * length - pad),
			levelAt(after[0], 2 * length + pad, 3 * length - pad));
		QVERIFY(std::fabs(levelAt(after[0], pad, length - pad) - 0.5) < 1e-4);
		QCOMPARE(levelAt(after[0], length + pad, 2 * length - pad), 0.0);
		QVERIFY(std::fabs(levelAt(after[0], 2 * length + pad, 3 * length - pad) - 0.25) < 1e-4);

		REV_UNDO_OR_FAIL();
		const auto undone = clipsOf(track);
		QCOMPARE(undone.size(), std::size_t{2});
		QCOMPARE(undone[1]->startPosition().getTicks(), 2 * length);
	}

	void aMutedTrackStillConsolidatesItsAudioAndClipGainIsBakedIn()
	{
		Track* track = newSampleTrack();
		SampleClip* clip = addTake(track, 0, 0.5f);
		ClipEdits edits = clip->clipEdits();
		edits.gain = 0.5f;
		clip->setClipEdits(edits);
		track->setMuted(true);
		const ControlResult done = run(QStringLiteral("clip.consolidate"), args(track, {}));
		QVERIFY2(done.ok, qPrintable(done.errorMessage));
		const auto after = clipsOf(track);
		const tick_t length = after[0]->length().getTicks();
		QVERIFY2(std::fabs(levelAt(after[0], length / 8, length - length / 8) - 0.25) < 1e-4,
			"the muted track's clip was not rendered at its clip gain");
	}

	void theCompositeIsHonouredThenNamesTheOneClip()
	{
		Track* track = newSampleTrack();
		SampleClip* first = addTake(track, 0, 0.5f, 0);
		addTake(track, 0, 0.25f, 1);
		const int length = first->length().getTicks();
		TakeLaneModel& comp = track->takeLanes();
		comp.addLane();
		comp.addLane();
		QVERIFY(comp.selectSegment(0, length / 2, 0));
		QVERIFY(comp.selectSegment(length / 2, length, 1));

		const ControlResult done = run(QStringLiteral("clip.consolidate"), args(track, {}));
		QVERIFY2(done.ok, qPrintable(done.errorMessage));
		QVERIFY(done.result.value(QStringLiteral("recomped")).toBool());
		const auto after = clipsOf(track);
		QCOMPARE(after.size(), std::size_t{1});
		const int pad = length / 8;
		QVERIFY(std::fabs(levelAt(after[0], pad, length / 2 - pad) - 0.5) < 1e-4);
		QVERIFY(std::fabs(levelAt(after[0], length / 2 + pad, length - pad) - 0.25) < 1e-4);
		QCOMPARE(comp.segments().size(), std::size_t{1});
		QCOMPARE(comp.segments()[0].laneIndex, comp.baseLane());

		REV_UNDO_OR_FAIL();
		QCOMPARE(track->takeLanes().segments().size(), std::size_t{2});
		QCOMPARE(clipsOf(track).size(), std::size_t{2});
	}

	void refusalsChangeNothing()
	{
		Track* track = newSampleTrack();
		SampleClip* clip = addTake(track, 0, 0.5f);
		const int length = clip->length().getTicks();
		const QString out = m_dir.filePath(QStringLiteral("refused.wav"));
		const ControlResult crossing = run(QStringLiteral("clip.consolidate"), args(track,
			{{QStringLiteral("start"), length / 2}, {QStringLiteral("end"), length * 2},
				{QStringLiteral("out"), out}}));
		QCOMPARE(crossing.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(crossing.errorMessage.contains(QStringLiteral("clip.split")));
		const ControlResult empty = run(QStringLiteral("clip.consolidate"), args(track,
			{{QStringLiteral("start"), length * 4}, {QStringLiteral("end"), length * 5},
				{QStringLiteral("out"), out}}));
		QCOMPARE(empty.errorKind, ControlErrorKind::Refused);
		QVERIFY(!QFileInfo::exists(out));
		QCOMPARE(clipsOf(track).size(), std::size_t{1});
		QCOMPARE(clipsOf(track)[0], clip);

		const ControlResult instrument = run(QStringLiteral("clip.consolidate"),
			{{QStringLiteral("track"), addTrack(QStringLiteral("instrument"))}});
		QCOMPARE(instrument.errorKind, ControlErrorKind::InvalidArgs);
	}

	void aDefaultPathIsNeverReused()
	{
		Track* track = newSampleTrack();
		addTake(track, 0, 0.5f);
		const QJsonObject whole{{QStringLiteral("track"), control::trackIdOf(track)}};
		const ControlResult once = run(QStringLiteral("clip.consolidate"), whole);
		QVERIFY2(once.ok, qPrintable(once.errorMessage));
		const ControlResult twice = run(QStringLiteral("clip.consolidate"), whole);
		QVERIFY2(twice.ok, qPrintable(twice.errorMessage));
		const QString a = once.result.value(QStringLiteral("path")).toString();
		const QString b = twice.result.value(QStringLiteral("path")).toString();
		QVERIFY2(a != b, qPrintable(a));
		QVERIFY(QFileInfo::exists(a) && QFileInfo::exists(b));
		QFile::remove(a);
		QFile::remove(b);
	}

private:
	Track* newSampleTrack()
	{
		ControlResult error;
		return control::resolveTrack(addTrack(QStringLiteral("sample")), &error);
	}

	QJsonObject args(Track* track, QJsonObject extra)
	{
		extra.insert(QStringLiteral("track"), control::trackIdOf(track));
		if (!extra.contains(QStringLiteral("out")))
		{
			extra.insert(QStringLiteral("out"), m_dir.filePath(QStringLiteral("c%1.wav").arg(++m_files)));
		}
		return extra;
	}

	QTemporaryDir m_dir;
	int m_files = 0;
};

QTEST_GUILESS_MAIN(ClipConsolidateTest)
#include "ClipConsolidateTest.moc"
