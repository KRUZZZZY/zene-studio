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

#include "AutomationTrack.h"
#include "Clip.h"
#include "ControlCompSupport.h"
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

//! The newest A16 record (SPEC A16 §3): what control.transactions reports for the last undo step.
QJsonObject lastRecord()
{
	const QJsonArray all = run(QStringLiteral("control.transactions")).result.value(QStringLiteral("transactions")).toArray();
	return all.isEmpty() ? QJsonObject() : all.last().toObject();
}

//! A take clip on @a lane over [@a start, @a start + @a length) ticks.
void addTake(SampleTrack* track, int lane, int start, int length)
{
	Clip* take = track->createClip(TimePos(start));
	take->changeLength(TimePos(length));
	take->setLaneIndex(lane);
}

//! A sample track with two take lanes, composited lane 0 then lane 1 at tick 192. The takes are real clips -
//! lane 0's over [0, 384), lane 1's over [192, 384) - since comp.move_edge asks whether the growing take
//! reaches (0.5.0 E2: a slip is relative to the take, so the takes themselves are what can refuse).
SampleTrack* twoTakeTrack()
{
	auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
	const QString id = control::trackIdOf(track);
	for (int i = 0; i < 2; ++i) { run(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), id}}); }
	addTake(track, 0, 0, 384);
	addTake(track, 1, 192, 192);
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
		// Moving it back left: lane 1 would have to start before its own take does (at 192) - refused.
		QCOMPARE(run(QStringLiteral("comp.move_edge"), {{QStringLiteral("track"), id}, {QStringLiteral("tick"), 240}, {QStringLiteral("to"), 100}}).errorKind,
			ControlErrorKind::Refused);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(track->takeLanes().segments()[0].endTick, 192);
	}

	//! A lane may hold SEVERAL takes, so the two ends of the range an edge move grows into can be
	//! supplied by DIFFERENT clips with a gap between them: asking only the two ends let the
	//! boundary move across the gap and left the composite naming a lane with no take there
	//! (comp.get_state would report it 'unresolved'). Every tick of the range must be covered.
	void anEdgeMoveRefusesAcrossAGapInTheGrowingLane()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		const QString id = control::trackIdOf(track);
		for (int i = 0; i < 2; ++i) { run(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), id}}); }
		addTake(track, 0, 0, 240);  // lane 0: two takes with a GAP over [240, 300)
		addTake(track, 0, 300, 180);
		addTake(track, 1, 192, 288);
		TakeLaneModel& model = track->takeLanes();
		model.selectSegment(0, 192, 0, 0);
		model.selectSegment(192, 480, 1, 0);
		// Growing lane 0 over [192, 350): both ends sit inside one of its takes, the gap does not.
		QCOMPARE(run(QStringLiteral("comp.move_edge"), {{QStringLiteral("track"), id}, {QStringLiteral("tick"), 192},
			{QStringLiteral("to"), 350}}).errorKind, ControlErrorKind::Refused);
		QCOMPARE(model.segments()[0].endTick, 192);  // nothing changed
		// A range inside the first take is still covered, so the move is still allowed.
		QVERIFY(run(QStringLiteral("comp.move_edge"), {{QStringLiteral("track"), id}, {QStringLiteral("tick"), 192},
			{QStringLiteral("to"), 240}}).ok);
		QCOMPARE(model.segments()[0].endTick, 240);
	}

	//! 0.5.0 E2: the growing take keeps its SLIP (a slip is the segment's, not a position that grows with the tick),
	//! so the grown range merges back into one segment; and the take refuses where its slipped read runs off its end.
	void aSlippedTakeKeepsItsSlipAcrossAnEdgeMove()
	{
		SampleTrack* track = twoTakeTrack();
		const QString id = control::trackIdOf(track);
		QVERIFY(run(QStringLiteral("comp.select"), {{QStringLiteral("track"), id}, {QStringLiteral("begin"), 0}, {QStringLiteral("end"), 192},
			{QStringLiteral("lane"), 0}, {QStringLiteral("srcpos"), 24}}).ok);
		QVERIFY(run(QStringLiteral("comp.move_edge"), {{QStringLiteral("track"), id}, {QStringLiteral("tick"), 192}, {QStringLiteral("to"), 240}}).ok);
		const std::vector<TakeLaneSegment>& segments = track->takeLanes().segments();
		QCOMPARE(segments.size(), std::size_t{2});
		QCOMPARE(segments[0].endTick, 240);
		QCOMPARE(segments[0].sourceOffset, 24);
		// Lane 0's take ends at 384: grown to 370 it would read up to 369 + 24 - past its end.
		QCOMPARE(run(QStringLiteral("comp.move_edge"), {{QStringLiteral("track"), id}, {QStringLiteral("tick"), 240}, {QStringLiteral("to"), 370}}).errorKind,
			ControlErrorKind::Refused);
		QCOMPARE(segments[0].endTick, 240);
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

	//! Only a clip comping can hold as a take (an audio or MIDI take, comp.assign's own rule) may be
	//! cut into passes: splitPasses resolved the clip through the general resolver, so an automation
	//! clip meeting the loop conditions was cut, moved and tagged with a take lane - lanes whose
	//! composite can never resolve to it.
	void splittingPassesRefusesAClipCompingCannotHold()
	{
		auto* track = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong()));
		Clip* clip = track->createClip(TimePos(192));
		clip->changeLength(TimePos(192 * 3));
		Engine::getSong()->getTimeline(Song::PlayMode::Song).setLoopPoints(TimePos(192), TimePos(384));
		const ControlResult split = run(QStringLiteral("comp.split_passes"), {{QStringLiteral("clip"), control::clipIdOf(clip)}});
		QCOMPARE(split.errorKind, ControlErrorKind::Refused);
		QCOMPARE(track->getClips().size(), std::size_t{1});
		QCOMPARE(track->getClips()[0]->startPosition().getTicks(), tick_t{192});
		QCOMPARE(track->takeLanes().laneCount(), 0);
	}

	//! The last pass can be shorter than the loop, but splitPasses always selected the last lane over
	//! the WHOLE loop, so the composite named a lane with no take behind it over the uncovered tail -
	//! TakeLaneModel::takeAt() reports 'unresolved' there and the engine plays nothing for. Only the
	//! last piece's own duration may be selected.
	void splittingPassesCompsOnlyWhatTheLastPassCovers()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		Clip* take = track->createClip(TimePos(192));
		take->changeLength(TimePos(192 * 3 + 96));  // three full passes and half of a fourth
		Engine::getSong()->getTimeline(Song::PlayMode::Song).setLoopPoints(TimePos(192), TimePos(384));
		const ControlResult split = run(QStringLiteral("comp.split_passes"), {{QStringLiteral("clip"), control::clipIdOf(take)}});
		QVERIFY2(split.ok, qPrintable(split.errorMessage));
		QCOMPARE(split.result.value(QStringLiteral("passes")).toInt(), 4);
		const std::vector<TakeLaneSegment>& segments = track->takeLanes().segments();
		QCOMPARE(segments.size(), std::size_t{1});
		// The last take is 96 ticks, so the composite ends at 192 + 96, not at the loop's end 384.
		QCOMPARE(segments[0].endTick, 288);
		for (const QJsonValue& entry : control::resolvedState(track))
		{
			QCOMPARE(entry.toObject().value(QStringLiteral("status")).toString(), QStringLiteral("bound"));
		}
	}

	//! A16 §3: a mutating command records what it wrote - the composite before the edit and the
	//! inverse control.undo replays. comp.move_edge journalled the track but described nothing, so
	//! the record carried no before-state and no inverse, unlike its sibling comp.select.
	void aMovedEdgeRecordsItsBeforeStateAndInverse()
	{
		SampleTrack* track = twoTakeTrack();
		const QString id = control::trackIdOf(track);
		QVERIFY(run(QStringLiteral("comp.move_edge"), {{QStringLiteral("track"), id}, {QStringLiteral("tick"), 200},
			{QStringLiteral("to"), 240}}).ok);
		const QJsonObject record = lastRecord();
		QCOMPARE(record.value(QStringLiteral("command")).toString(), QStringLiteral("comp.move_edge"));
		QVERIFY(record.value(QStringLiteral("reversible")).toBool());
		const QJsonObject before = record.value(QStringLiteral("before")).toObject().value(QStringLiteral("composite")).toObject();
		QCOMPARE(before.value(QStringLiteral("segments")).toArray().at(0).toObject().value(QStringLiteral("end")).toInt(), 192);
		QCOMPARE(record.value(QStringLiteral("inverse")).toObject().value(QStringLiteral("op")).toString(), QStringLiteral("control.undo"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(track->takeLanes().segments()[0].endTick, 192);
	}

	//! A16 §3, on the multi-track half: one grouped edit is one record, and its before-state names
	//! EVERY track it wrote - comp.select_group journalled each track but returned no payload, so the
	//! record said nothing about what had changed.
	void aGroupedChoiceRecordsEveryTrackItWrote()
	{
		SampleTrack* kick = twoTakeTrack();
		SampleTrack* snare = twoTakeTrack();
		const QJsonArray tracks{control::trackIdOf(kick), control::trackIdOf(snare)};
		QVERIFY(run(QStringLiteral("comp.select_group"), {{QStringLiteral("tracks"), tracks}, {QStringLiteral("begin"), 0},
			{QStringLiteral("end"), 384}, {QStringLiteral("lane"), 1}}).ok);
		const QJsonObject record = lastRecord();
		QCOMPARE(record.value(QStringLiteral("command")).toString(), QStringLiteral("comp.select_group"));
		QVERIFY(record.value(QStringLiteral("reversible")).toBool());
		const QJsonArray before = record.value(QStringLiteral("before")).toObject().value(QStringLiteral("tracks")).toArray();
		QCOMPARE(before.size(), 2);
		for (const QJsonValue& entry : before)
		{
			const QJsonObject composite = entry.toObject().value(QStringLiteral("composite")).toObject();
			QCOMPARE(composite.value(QStringLiteral("segments")).toArray().size(), 2);
			QCOMPARE(composite.value(QStringLiteral("segments")).toArray().at(0).toObject().value(QStringLiteral("lane")).toInt(), 0);
		}
		QCOMPARE(record.value(QStringLiteral("inverse")).toObject().value(QStringLiteral("op")).toString(), QStringLiteral("control.undo"));
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
