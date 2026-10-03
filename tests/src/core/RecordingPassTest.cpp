/*
 * RecordingPassTest.cpp - count-in, pre-roll, the record mode and cycling takes
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
/*! 0.5.0 L03: transport.record_options sets count-in, pre-roll and the record mode as one undo step
 *  (WF-080 / WF-082); a pass with a count-in starts that many bars early and the metronome's count-in
 *  window covers exactly those bars, gone once the pass ends; comp.cycle moves a comp segment to the
 *  next take lane and back, wrapping, and one undo puts it back (ED-168); a launcher binding is stored
 *  and undone, and learning binds the next note played (WF-069); session.capture_to_slot puts what was
 *  played into a new pattern a cell plays, and one undo takes it away (WF-055). */

#include <QtTest>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "LaunchBindings.h"
#include "MidiClient.h"
#include "MidiEvent.h"
#include "PatternStore.h"
#include "RecordOptions.h"
#include "RetroMidiCapture.h"
#include "SessionModel.h"
#include "Song.h"
#include "TakeLane.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace

class RecordingPassTest : public QObject
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
		recordoptions::setOptions({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theOptionsAreSetAndUndone()
	{
		const ControlResult set = run(QStringLiteral("transport.record_options"), {{QStringLiteral("count_in_bars"), 1},
			{QStringLiteral("pre_roll_bars"), 1}, {QStringLiteral("mode"), QStringLiteral("replace")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(recordoptions::options().countInBars, 1);
		QVERIFY(recordoptions::options().mode == recordoptions::Mode::Replace);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(recordoptions::options().countInBars, 0);
		QCOMPARE(run(QStringLiteral("transport.record_options"), {{QStringLiteral("count_in_bars"), 9}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	void aCountInStartsEarlyAndClicksOnlyThroughIt()
	{
		Song* song = Engine::getSong();
		const tick_t bar = song->ticksPerBar();
		recordoptions::setOptions({1, 1, recordoptions::Mode::Overdub});
		song->setPlayPos(4 * bar, Song::PlayMode::Song);
		recordoptions::beginPass(song);
		QCOMPARE(song->getPlayPos(Song::PlayMode::Song).getTicks(), 2 * bar);  // two bars early
		QVERIFY(recordoptions::countingIn(2 * bar));
		QVERIFY(recordoptions::countingIn(3 * bar - 1));
		QVERIFY(!recordoptions::countingIn(3 * bar));  // the pre-roll bar is not clicked
		recordoptions::endPass(song);
		QVERIFY(!recordoptions::countingIn(2 * bar));
		recordoptions::setOptions({});
	}

	void cyclingMovesASegmentToTheNextTake()
	{
		const QString track = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).result
			.value(QStringLiteral("track")).toString();
		QVERIFY(run(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), track}}).ok);
		QVERIFY(run(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), track}}).ok);
		const ControlResult selected = run(QStringLiteral("comp.select"), {{QStringLiteral("track"), track}, {QStringLiteral("begin"), 0},
			{QStringLiteral("end"), 192}, {QStringLiteral("lane"), 0}});
		QVERIFY2(selected.ok, qPrintable(selected.errorMessage));
		const ControlResult cycled = run(QStringLiteral("comp.cycle"), {{QStringLiteral("track"), track}, {QStringLiteral("tick"), 10}});
		QVERIFY2(cycled.ok, qPrintable(cycled.errorMessage));
		QCOMPARE(cycled.result.value(QStringLiteral("lane")).toInt(), 1);
		QCOMPARE(run(QStringLiteral("comp.cycle"), {{QStringLiteral("track"), track}, {QStringLiteral("tick"), 10}}).result
			.value(QStringLiteral("lane")).toInt(), 0);  // wraps round
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(run(QStringLiteral("comp.cycle"), {{QStringLiteral("track"), track}, {QStringLiteral("tick"), 10},
			{QStringLiteral("direction"), QStringLiteral("previous")}}).result.value(QStringLiteral("lane")).toInt(), 0);
		QCOMPARE(run(QStringLiteral("comp.cycle"), {{QStringLiteral("track"), track}, {QStringLiteral("tick"), 5000}}).errorKind,
			ControlErrorKind::NotFound);  // no segment there
	}

	//! WF-055: what was played lands in a new pattern a launcher cell plays; one undo takes it away.
	void playingIsRecordedIntoALauncherCell()
	{
		MidiClient* client = Engine::audioEngine()->midiClient();
		QVERIFY(client != nullptr);
		if (Engine::patternStore()->tracks().empty()) { Track::create(Track::Type::Instrument, Engine::patternStore()); }
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 2}, {QStringLiteral("scenes"), 2}}).ok);
		QVERIFY(run(QStringLiteral("midi.retro_capture_arm"), {{QStringLiteral("armed"), true}}).ok);
		RetroMidiCapture& capture = client->retroCapture();
		capture.capture(MidiEvent(MidiNoteOn, 0, 60, 100), 0);
		capture.capture(MidiEvent(MidiNoteOff, 0, 60, 0), 48);
		capture.capture(MidiEvent(MidiNoteOn, 0, 62, 100), 96);
		capture.capture(MidiEvent(MidiNoteOff, 0, 62, 0), 144);
		const int patternsBefore = Engine::patternStore()->numOfPatterns();
		const ControlResult captured = run(QStringLiteral("session.capture_to_slot"), {{QStringLiteral("track"), 1},
			{QStringLiteral("scene"), 1}});
		QVERIFY2(captured.ok, qPrintable(captured.errorMessage));
		QCOMPARE(captured.result.value(QStringLiteral("notes")).toInt(), 2);
		QCOMPARE(Engine::patternStore()->numOfPatterns(), patternsBefore + 1);
		QCOMPARE(Engine::getSong()->sessionModel().slot(1, 1).patternId(), captured.result.value(QStringLiteral("pattern")).toInt());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(Engine::getSong()->sessionModel().slot(1, 1).isEmpty());
		QCOMPARE(Engine::patternStore()->numOfPatterns(), patternsBefore);
		QCOMPARE(run(QStringLiteral("session.capture_to_slot"), {{QStringLiteral("track"), 9}, {QStringLiteral("scene"), 0}}).errorKind,
			ControlErrorKind::NotFound);  // outside the grid
		run(QStringLiteral("midi.retro_capture_arm"), {{QStringLiteral("armed"), false}});
	}

	//! WF-069: a note bound to a scene is stored and undone; learn binds the next note played.
	void theLauncherIsBoundToNotes()
	{
		LaunchBindings::setBindings({});
		const ControlResult bound = run(QStringLiteral("session.bind"), {{QStringLiteral("key"), 60}, {QStringLiteral("action"), QStringLiteral("scene")},
			{QStringLiteral("scene"), 2}});
		QVERIFY2(bound.ok, qPrintable(bound.errorMessage));
		QCOMPARE(LaunchBindings::bindings().size(), 1);
		QCOMPARE(run(QStringLiteral("session.bind"), {{QStringLiteral("key"), 61}, {QStringLiteral("action"), QStringLiteral("slot")},
			{QStringLiteral("scene"), 0}}).errorKind, ControlErrorKind::InvalidArgs);  // a slot needs its track
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(LaunchBindings::bindings().isEmpty());
		QCOMPARE(run(QStringLiteral("session.bind_learn"), {{QStringLiteral("action"), QStringLiteral("stop_all")}}).errorKind,
			ControlErrorKind::Refused);  // nothing connected
		LaunchBindings* bindings = LaunchBindings::connect(QString(), QString());
		QVERIFY(run(QStringLiteral("session.bind_learn"), {{QStringLiteral("action"), QStringLiteral("stop_all")}}).ok);
		bindings->applyNote(LaunchNote{3, 48, true});  // the next note played
		QCOMPARE(LaunchBindings::bindings().size(), 1);
		QCOMPARE(LaunchBindings::bindings().first().toObject().value(QStringLiteral("key")).toInt(), 48);
		bindings->applyNote(LaunchNote{3, 48, true});  // now it stops everything (and records nothing)
		QVERIFY(run(QStringLiteral("session.unbind"), {{QStringLiteral("key"), 48}, {QStringLiteral("channel"), 3}}).ok);
		QVERIFY(LaunchBindings::bindings().isEmpty());
		LaunchBindings::disconnect();
	}
};

QTEST_MAIN(RecordingPassTest)
#include "RecordingPassTest.moc"
