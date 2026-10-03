/*
 * RecordToolsTest.cpp - comp edges and comp groups, metronome options, punch here, remaining record time
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
/*! 0.5.0 L03 (W3): a comp boundary moves and the growing take keeps its timing (ED-169); one comp
 *  choice lands on several tracks as one undo step, or on none (ED-172); the metronome's options are
 *  kept and undone, and record-only silences it outside a pass but not for a count-in (WF-081); punch
 *  in and out at the play position (WF-079); the remaining record time is read (WF-085). */

#include <QtTest>

#include <QJsonArray>

#include "Clip.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "MetronomeOptions.h"
#include "RecorderTaps.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TakeLane.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! A sample track with two take lanes, composited lane 0 then lane 1 at tick 192.
SampleTrack* twoTakeTrack()
{
	auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
	const QString id = control::trackIdOf(track);
	for (int i = 0; i < 2; ++i) { run(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), id}}); }
	TakeLaneModel& model = track->takeLanes();
	model.selectSegment(0, 192, 0, 0);
	model.selectSegment(192, 384, 1, 0);
	return track;
}

} // namespace

class RecordToolsTest : public QObject
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

	void aCompEdgeMoves()
	{
		SampleTrack* track = twoTakeTrack();
		const QString id = control::trackIdOf(track);
		QVERIFY(run(QStringLiteral("comp.move_edge"), {{QStringLiteral("track"), id}, {QStringLiteral("tick"), 200}, {QStringLiteral("to"), 240}}).ok);
		const std::vector<TakeLaneSegment>& segments = track->takeLanes().segments();
		QCOMPARE(segments.size(), std::size_t{2});
		QCOMPARE(segments[0].endTick, 240);
		QCOMPARE(segments[0].sourceOffset, 0);  // lane 0 carries on in time
		// Moving it back left: lane 1 would have to start before its own take does - refused.
		QCOMPARE(run(QStringLiteral("comp.move_edge"), {{QStringLiteral("track"), id}, {QStringLiteral("tick"), 240}, {QStringLiteral("to"), 100}}).errorKind,
			ControlErrorKind::Refused);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(track->takeLanes().segments()[0].endTick, 192);
	}

	void oneCompChoiceOnSeveralTracks()
	{
		SampleTrack* kick = twoTakeTrack();
		SampleTrack* snare = twoTakeTrack();
		const QJsonArray tracks{control::trackIdOf(kick), control::trackIdOf(snare)};
		QVERIFY(run(QStringLiteral("comp.select_group"), {{QStringLiteral("tracks"), tracks}, {QStringLiteral("begin"), 0},
			{QStringLiteral("end"), 384}, {QStringLiteral("lane"), 1}}).ok);
		QCOMPARE(kick->takeLanes().segments().size(), std::size_t{1});
		QCOMPARE(snare->takeLanes().segments().front().laneIndex, 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // both, one step
		QCOMPARE(kick->takeLanes().segments().size(), std::size_t{2});
		QCOMPARE(snare->takeLanes().segments().size(), std::size_t{2});
		QCOMPARE(run(QStringLiteral("comp.select_group"), {{QStringLiteral("tracks"), tracks}, {QStringLiteral("begin"), 0},
			{QStringLiteral("end"), 384}, {QStringLiteral("lane"), 5}}).errorKind, ControlErrorKind::Refused);
	}

	void aLoopedTakeBecomesPasses()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		Clip* take = track->createClip(TimePos(192));
		take->changeLength(TimePos(192 * 3));  // three passes of a one-bar loop
		Engine::getSong()->getTimeline(Song::PlayMode::Song).setLoopPoints(TimePos(192), TimePos(384));
		const ControlResult split = run(QStringLiteral("comp.split_passes"), {{QStringLiteral("clip"), control::clipIdOf(take)}});
		QVERIFY2(split.ok, qPrintable(split.errorMessage));
		QCOMPARE(split.result.value(QStringLiteral("passes")).toInt(), 3);
		QCOMPARE(track->getClips().size(), std::size_t{3});
		for (const Clip* clip : track->getClips()) { QCOMPARE(clip->startPosition().getTicks(), tick_t{192}); }
		QCOMPARE(track->takeLanes().laneCount(), 3);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(track->getClips().size(), std::size_t{1});
	}

	void theMetronomesOptions()
	{
		QVERIFY(run(QStringLiteral("transport.metronome_options"), {{QStringLiteral("level"), 0.5}, {QStringLiteral("sound"), QStringLiteral("rim")},
			{QStringLiteral("record_only"), true}}).ok);
		QCOMPARE(metronomeoptions::options().level, 0.5f);
		QVERIFY(!metronomeoptions::sounds(false, false));  // not recording: silent
		QVERIFY(metronomeoptions::sounds(true, false));    // a count-in clicks anyway
		QVERIFY(metronomeoptions::sounds(false, true));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(metronomeoptions::options().level, 1.f);
		QVERIFY(metronomeoptions::sounds(false, false));
		QCOMPARE(run(QStringLiteral("transport.metronome_options"), {{QStringLiteral("level"), 3.0}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	void punchAtThePlayPosition()
	{
		Song* song = Engine::getSong();
		song->setPlayPos(384);  // the play head of the current mode, the one punch_here reads (review W3-batchA A1)
		QCOMPARE(run(QStringLiteral("transport.punch_here"), {{QStringLiteral("edge"), QStringLiteral("out")}}).errorKind, ControlErrorKind::Refused);
		QVERIFY(run(QStringLiteral("transport.punch_here"), {{QStringLiteral("edge"), QStringLiteral("in")}}).ok);
		song->setPlayPos(768);
		QVERIFY(run(QStringLiteral("transport.punch_here"), {{QStringLiteral("edge"), QStringLiteral("out")}}).ok);
		const QJsonObject punch = run(QStringLiteral("transport.punch_get_state")).result;
		QCOMPARE(punch.value(QStringLiteral("punch_begin")).toInt(), 384);
		QCOMPARE(punch.value(QStringLiteral("punch_end")).toInt(), 768);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(run(QStringLiteral("transport.punch_get_state")).result.value(QStringLiteral("punch_begin")).toInt(), 384);
	}

	void aRouteTakesTheMasterOrAChannel()
	{
		QVERIFY(run(QStringLiteral("record.route_source"), {{QStringLiteral("route"), 0}, {QStringLiteral("source"), QStringLiteral("master")},
			{QStringLiteral("side"), QStringLiteral("right")}}).ok);
		QVERIFY(recordertaps::tapped(0));
		QCOMPARE(run(QStringLiteral("record.route_source"), {{QStringLiteral("route"), 0}}).result.value(QStringLiteral("side")).toString(), QStringLiteral("right"));
		QVERIFY(run(QStringLiteral("record.route_source"), {{QStringLiteral("route"), 0}, {QStringLiteral("source"), QStringLiteral("channel")},
			{QStringLiteral("channel"), control::channelIdOf(Engine::mixer()->mixerChannel(0))}, {QStringLiteral("tap"), QStringLiteral("pre_fader")}}).ok);
		QCOMPARE(recordertaps::get(0).source, recordertaps::Source::Channel);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(recordertaps::get(0).source, recordertaps::Source::Master);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!recordertaps::tapped(0));  // back on the input
		QCOMPARE(run(QStringLiteral("record.route_source"), {{QStringLiteral("route"), 999}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	void theRemainingRecordTime()
	{
		const ControlResult left = run(QStringLiteral("record.remaining"));
		QVERIFY2(left.ok, qPrintable(left.errorMessage));
		QVERIFY(left.result.value(QStringLiteral("seconds")).toDouble() >= 0.0);
		QCOMPARE(left.result.value(QStringLiteral("armed_routes")).toInt(), 0);
	}
};

QTEST_MAIN(RecordToolsTest)
#include "RecordToolsTest.moc"
