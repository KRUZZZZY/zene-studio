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
 *  played into a new pattern a cell plays, and one undo takes it away (WF-055).
 *  Theme 7: Replace erases the notes from before the pass that a recorded note overlaps, at any pitch, and
 *  nothing else - not a note of the pass itself, not the clip's controller lanes - and the record options'
 *  reply states that scope; transport.time_format switches bars|beats, ticks and time as one undo step, and
 *  positions and lengths read and type back exactly in each.
 *  Q050-094: the record mode is one choice for MIDI and audio - overdub, replace or keep_history - read and set
 *  through transport.record_options with its scope for each medium, as one undo step; the stored config numbers
 *  0 (overdub) and 1 (replace) keep their meaning; the audio overdub mix is sample-exact. */

#include <QtTest>

#include <vector>

#include <QJsonArray>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "LaunchBindings.h"
#include "MidiClient.h"
#include "MidiClip.h"
#include "MidiEvent.h"
#include "MusicalTime.h"
#include "Note.h"
#include "PatternStore.h"
#include "RecordOptions.h"
#include "RetroMidiCapture.h"
#include "SampleBuffer.h"
#include "SampleFrame.h"
#include "ReversibilityTestSupport.h"
#include "SessionModel.h"
#include "SessionScheduler.h"
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
		QCOMPARE(recordoptions::beginPass(song), 4 * bar);  // where Stop returns to
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

	//! Theme 7 (owner decision): Replace erases overlapped NOTES only, at any pitch; Overdub erases nothing.
	void replaceErasesOverlappedNotesOnly()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		Note* chord = clip->addNote(Note(TimePos(48), TimePos(0), 60), false);    // [0, 48): overlapped
		Note* pad = clip->addNote(Note(TimePos(48), TimePos(24), 72), false);     // [24, 72): overlapped, another pitch
		Note* later = clip->addNote(Note(TimePos(48), TimePos(96), 48), false);   // [96, 144): clear of it
		clip->setControllerPoints(ControllerKind::ControlChange, 1, {{0, 10}, {40, 90}, {120, 30}});
		const std::vector<int> beforePass{chord->id(), pad->id(), later->id()};
		const int laterId = later->id();
		Note* ofThePass = clip->addNote(Note(TimePos(24), TimePos(36), 64), false);  // played in this pass
		const int passId = ofThePass->id();
		const Note recorded(TimePos(30), TimePos(30), 65);  // [30, 60)

		recordoptions::setOptions({0, 0, recordoptions::Mode::Overdub});
		QCOMPARE(recordoptions::replaceOverlapped(clip, recorded, beforePass), 0);
		QCOMPARE(static_cast<int>(clip->notes().size()), 4);

		recordoptions::setOptions({0, 0, recordoptions::Mode::Replace});
		QCOMPARE(recordoptions::replaceOverlapped(clip, recorded, beforePass), 2);
		QCOMPARE(static_cast<int>(clip->notes().size()), 2);
		QList<int> kept;
		for (const Note* note : clip->notes()) { kept << note->id(); }
		QVERIFY(kept.contains(laterId) && kept.contains(passId));
		const ControllerLane* lane = clip->controllers().lane(ControllerKind::ControlChange, 1);
		QVERIFY(lane != nullptr);
		QCOMPARE(static_cast<int>(lane->points.size()), 3);  // the controller lane is untouched

		const QJsonObject reply = run(QStringLiteral("transport.record_options")).result;
		QCOMPARE(reply.value(QStringLiteral("replace_erases")).toString(), QStringLiteral("overlapped_notes_any_pitch"));
		QVERIFY(reply.value(QStringLiteral("replace_keeps")).toArray().contains(QStringLiteral("automation")));
		QVERIFY(reply.value(QStringLiteral("mode_scope")).toString().contains(QStringLiteral("Controllers and automation are kept")));
		recordoptions::setOptions({});
		delete track;
	}

	//! Q050-094: the same three choices for MIDI and audio, each with its scope, one undo step per change.
	void theRecordModeIsOneChoiceForMidiAndAudio()
	{
		recordoptions::setOptions({});
		const QJsonObject read = run(QStringLiteral("transport.record_options")).result;
		QCOMPARE(read.value(QStringLiteral("mode")).toString(), QStringLiteral("overdub"));
		QCOMPARE(read.value(QStringLiteral("modes")).toVariant().toStringList(),
			(QStringList{QStringLiteral("overdub"), QStringLiteral("replace"), QStringLiteral("keep_history")}));
		// The command's schema offers exactly those choices.
		const ControlCommand* command = ControlRegistry::instance()->command(QStringLiteral("transport.record_options"));
		QVERIFY(command != nullptr);
		const QJsonArray offered = command->argsSchema.value(QStringLiteral("properties")).toObject()
			.value(QStringLiteral("mode")).toObject().value(QStringLiteral("enum")).toArray();
		QCOMPARE(offered.size(), 3);

		for (const QString& name : recordoptions::modeNames())
		{
			const ControlResult set = run(QStringLiteral("transport.record_options"), {{QStringLiteral("mode"), name}});
			QVERIFY2(set.ok, qPrintable(name + QStringLiteral(": ") + set.errorMessage));
			const QJsonObject reply = set.result;
			QCOMPARE(reply.value(QStringLiteral("mode")).toString(), name);
			const auto mode = static_cast<recordoptions::Mode>(recordoptions::modeNames().indexOf(name));
			QCOMPARE(reply.value(QStringLiteral("mode_label")).toString(), recordoptions::modeLabel(mode));
			QCOMPARE(reply.value(QStringLiteral("midi_scope")).toString(), recordoptions::midiScope(mode));
			QCOMPARE(reply.value(QStringLiteral("audio_scope")).toString(), recordoptions::audioScope(mode));
			QVERIFY(reply.value(QStringLiteral("mode_scope")).toString().contains(recordoptions::audioScope(mode)));
		}
		QVERIFY(recordoptions::options().mode == recordoptions::Mode::KeepHistory);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one step: back to Replace, the previous change
		QVERIFY(recordoptions::options().mode == recordoptions::Mode::Replace);
		QCOMPARE(run(QStringLiteral("transport.record_options"), {{QStringLiteral("mode"), QStringLiteral("merge")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		recordoptions::setOptions({});
	}

	//! Q050-094: the numbers in an existing config keep their meaning; one no build knows reads as the default.
	void theStoredModeNumbersAreStable()
	{
		const auto stored = [](int value) {
			ConfigManager::inst()->setValue(QStringLiteral("recording"), QStringLiteral("mode"), QString::number(value));
			return recordoptions::options().mode;
		};
		QVERIFY(stored(0) == recordoptions::Mode::Overdub);
		QVERIFY(stored(1) == recordoptions::Mode::Replace);
		QVERIFY(stored(2) == recordoptions::Mode::KeepHistory);
		QVERIFY(stored(9) == recordoptions::Mode::Overdub);
		recordoptions::setOptions({0, 0, recordoptions::Mode::KeepHistory});
		QCOMPARE(ConfigManager::inst()->value(QStringLiteral("recording"), QStringLiteral("mode")), QStringLiteral("2"));
		recordoptions::setOptions({});
	}

	//! Q050-094: the Overdub mix adds the pass to the audio already there, frame for frame.
	void theOverdubMixAddsThePassToTheEarlierAudio()
	{
		const int rate = 48000;
		std::vector<SampleFrame> earlier(100, SampleFrame(0.25f, -0.25f));
		std::vector<SampleFrame> pass(150, SampleFrame(0.125f, 0.5f));
		const SampleBuffer a(earlier.data(), earlier.size(), rate);
		const SampleBuffer b(pass.data(), pass.size(), rate);
		const auto mixed = recordoptions::overdubMix(a, b);
		QCOMPARE(mixed->size(), std::size_t{150});  // as long as the longer
		QCOMPARE(mixed->sampleRate(), rate);
		QCOMPARE(mixed->data()[0].left(), 0.375f);
		QCOMPARE(mixed->data()[99].right(), 0.25f);
		QCOMPARE(mixed->data()[100].left(), 0.125f);  // past the earlier audio: the pass alone
		QCOMPARE(mixed->data()[149].right(), 0.5f);
		QCOMPARE(a.size(), std::size_t{100});  // the inputs are not changed
		QCOMPARE(a.data()[0].left(), 0.25f);

		// A pass at another rate is resampled to the earlier audio's rate (24 kHz doubles in length).
		const SampleBuffer slow(pass.data(), 50, rate / 2);
		const auto resampled = recordoptions::overdubMix(a, slow);
		QCOMPARE(resampled->size(), std::size_t{100});
		QCOMPARE(resampled->data()[10].left(), 0.375f);
		QCOMPARE(recordoptions::overdubMix(a, SampleBuffer())->size(), std::size_t{100});  // nothing to add
	}

	//! Theme 7: the time format is one undo step, and every format reads and types back the same tick.
	void theTimeFormatReadsAndTypesMusicalTime()
	{
		using namespace musicaltime;
		QCOMPARE(run(QStringLiteral("transport.time_format")).result.value(QStringLiteral("format")).toString(), QStringLiteral("bars_beats"));
		QVERIFY(run(QStringLiteral("transport.time_format"), {{QStringLiteral("format"), QStringLiteral("ticks")}}).ok);
		QVERIFY(currentFormat() == Format::Ticks);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(currentFormat() == Format::BarsBeats);
		QCOMPARE(run(QStringLiteral("transport.time_format"), {{QStringLiteral("format"), QStringLiteral("smpte")}}).errorKind,
			ControlErrorKind::InvalidArgs);

		// 4/4, 192 ticks a bar: a beat is 48, a sixteenth 12.
		QCOMPARE(positionText(0), QStringLiteral("1.1.1"));
		QCOMPARE(positionText(192 + 48 + 12 + 2), QStringLiteral("2.2.2.2"));
		QCOMPARE(counterText(192 + 48 + 12 + 2), QStringLiteral("2.2.2"));
		QCOMPARE(lengthText(192), QStringLiteral("1.0.0"));
		QCOMPARE(lengthText(-48), QStringLiteral("-0.1.0"));
		tick_t tick = 0;
		QVERIFY(positionFromText(QStringLiteral("2.2.2.2"), &tick) && tick == 192 + 48 + 12 + 2);
		QVERIFY(positionFromText(QStringLiteral("2.1"), &tick) && tick == 192);
		QVERIFY(positionFromText(QStringLiteral("768t"), &tick) && tick == 768);
		QVERIFY(!positionFromText(QStringLiteral("1.5"), &tick));  // no fifth beat in 4/4
		QVERIFY(lengthFromText(QStringLiteral("2"), &tick) && tick == 384);  // a plain number: bars, as shown
		QVERIFY(lengthFromText(QStringLiteral("1.5 bars"), &tick) && tick == 288);
		QVERIFY(lengthFromText(QStringLiteral("-0.1.0"), &tick) && tick == -48);
		QVERIFY(lengthFromText(QStringLiteral("96"), &tick, 0, Format::Ticks) && tick == 96);  // ticks shown: ticks
		QCOMPARE(positionText(96, 0, Format::Ticks), QStringLiteral("96 ticks"));

		IntModel& tempo = Engine::getSong()->tempoModel();
		const int wasTempo = tempo.value();
		tempo.setValue(120);  // 96 ticks are a second
		QCOMPARE(positionText(96, 0, Format::Time), QStringLiteral("0:01.000"));
		for (const tick_t at : {0, 1, 95, 97, 1000, 12345})
		{
			QVERIFY(positionFromText(positionText(at, 0, Format::Time), &tick, 0, Format::Time));
			QCOMPARE(tick, at);  // read to the millisecond, typed back to the same tick
		}
		QVERIFY(lengthFromText(QStringLiteral("0:00.5"), &tick, 96, Format::Time) && tick == 48);
		tempo.setValue(static_cast<float>(wasTempo));
	}

	//! Cluster 5176: the ".tick" remainder counts INSIDE the sixteenth above it (12 ticks in 4/4),
	//! so it cannot reach into the next one. It used to be bounded by the whole beat, and a position
	//! it accepted did not read back as the text that produced it ("1.1.1.47" reads as "1.1.4.11").
	void aTickRemainderStaysInsideItsSixteenth()
	{
		using namespace musicaltime;
		tick_t tick = 0;
		QVERIFY(positionFromText(QStringLiteral("1.1.1.11"), &tick) && tick == 11);
		QVERIFY(!positionFromText(QStringLiteral("1.1.1.12"), &tick));
		QVERIFY(!positionFromText(QStringLiteral("1.1.1.47"), &tick));
		QVERIFY(!positionFromText(QStringLiteral("1.1.4.12"), &tick));
		QVERIFY(!lengthFromText(QStringLiteral("0.0.0.12"), &tick));
		// Every position the formatter does emit still reads back as itself.
		for (const tick_t at : {0, 11, 12, 192 + 48 + 12 + 2, 192 + 48 + 12 + 11})
		{
			QVERIFY(positionFromText(positionText(at), &tick));
			QCOMPARE(tick, at);
		}
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
			ControlErrorKind::InvalidArgs);  // outside the grid: gridContains' refusal, as every session command's
		run(QStringLiteral("midi.retro_capture_arm"), {{QStringLiteral("armed"), false}});
	}

	//! The capture's undo finds its pattern track by id: track.remove and its undo rebuild the track as a NEW object,
	//! which a guarded pointer no longer names, so the undo used to leave the rebuilt track in place.
	void aCapturedPatternIsFoundAgainAfterItsRemovalIsUndone()
	{
		if (Engine::patternStore()->tracks().empty()) { Track::create(Track::Type::Instrument, Engine::patternStore()); }
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 2}, {QStringLiteral("scenes"), 2}}).ok);
		QVERIFY(run(QStringLiteral("midi.retro_capture_arm"), {{QStringLiteral("armed"), true}}).ok);
		RetroMidiCapture& capture = Engine::audioEngine()->midiClient()->retroCapture();
		capture.capture(MidiEvent(MidiNoteOn, 0, 64, 100), 0);
		capture.capture(MidiEvent(MidiNoteOff, 0, 64, 0), 48);
		const ControlResult captured = run(QStringLiteral("session.capture_to_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 1}});
		QVERIFY2(captured.ok, qPrintable(captured.errorMessage));
		const QString pattern = control::trackIdOf(Engine::getSong()->tracks().back());  // the pattern track it made
		QVERIFY(revtest::makerUndoFindsTrackAfterRemovalUndone(pattern));
		QVERIFY(Engine::getSong()->sessionModel().slot(0, 1).isEmpty());
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

	//! A note consumed by learning must not release a Gate cell that was already playing.
	void learningANoteDoesNotReleaseAPlayingGateCell()
	{
		Engine::audioEngine()->audioDev()->stopProcessing();
		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY(Track::create(Track::Type::Instrument, song) != nullptr);
		QVERIFY(Track::create(Track::Type::Pattern, song) != nullptr);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1}, {QStringLiteral("scenes"), 1}}).ok);
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("type"), QStringLiteral("midi")}, {QStringLiteral("pattern"), 0},
			{QStringLiteral("mode"), QStringLiteral("gate")}, {QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);

		LaunchBindings* bindings = LaunchBindings::connect(QString(), QString());
		SessionScheduler& scheduler = song->sessionScheduler();
		QVERIFY(run(QStringLiteral("session.launch_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0}}).ok);
		SessionClockContext clock;
		clock.ticksPerBar = song->ticksPerBar();
		clock.framesPerTick = 4.0f;
		clock.transportRunning = true;
		scheduler.processAudio(clock, 16);
		QCOMPARE(scheduler.slotPhase(0, 0), SlotPhase::Playing);

		QVERIFY(run(QStringLiteral("session.bind_learn"), {{QStringLiteral("action"), QStringLiteral("slot")},
			{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0}}).ok);
		bindings->applyNote(LaunchNote{0, 60, true});
		QVERIFY(bindings->learning().isEmpty());
		bindings->applyNote(LaunchNote{0, 60, false});
		scheduler.processAudio(clock, 16);
		const SlotPhase afterLearnedRelease = scheduler.slotPhase(0, 0);
		LaunchBindings::disconnect();
		LaunchBindings::setBindings({});
		QCOMPARE(afterLearnedRelease, SlotPhase::Playing);
	}

	//! A hand-edited binding whose channel is invalid (-2, a word, a fraction, or absent) is skipped, not an any-channel
	//! binding that launches on every channel; a channel 0..15 and -1 still match as they did.
	void aBindingWithAnInvalidChannelIsSkipped()
	{
		Engine::audioEngine()->audioDev()->stopProcessing();
		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY(Track::create(Track::Type::Instrument, song) != nullptr);
		QVERIFY(Track::create(Track::Type::Pattern, song) != nullptr);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1}, {QStringLiteral("scenes"), 6}}).ok);
		for (int scene = 0; scene < 6; ++scene)
		{
			QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), scene},
				{QStringLiteral("type"), QStringLiteral("midi")}, {QStringLiteral("pattern"), 0},
				{QStringLiteral("mode"), QStringLiteral("gate")}, {QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
		}
		const auto binding = [](int key, const QJsonValue& channel, int scene) {
			return QJsonObject{{QStringLiteral("key"), key}, {QStringLiteral("channel"), channel}, {QStringLiteral("action"), QStringLiteral("slot")},
				{QStringLiteral("track"), 0}, {QStringLiteral("scene"), scene}};
		};
		const QJsonObject missingChannel{{QStringLiteral("key"), 65}, {QStringLiteral("action"), QStringLiteral("slot")},
			{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 5}};
		LaunchBindings::setBindings(QJsonArray{binding(60, -2, 0), binding(61, QStringLiteral("bad"), 1), binding(62, 3, 2),
			binding(63, -1.5, 3), binding(64, -1, 4), missingChannel});
		LaunchBindings* bindings = LaunchBindings::connect(QString(), QString());
		SessionScheduler& scheduler = song->sessionScheduler();
		SessionClockContext clock;
		clock.ticksPerBar = song->ticksPerBar();
		clock.framesPerTick = 4.0f;
		clock.transportRunning = true;
		// All six cells share track 0, where a launch stops the cell it replaces: each note is checked as it lands.
		bindings->applyNote(LaunchNote{3, 60, true});
		scheduler.processAudio(clock, 16);
		QCOMPARE(scheduler.slotPhase(0, 0), SlotPhase::Idle);  // key 60, channel -2: skipped
		bindings->applyNote(LaunchNote{3, 61, true});
		scheduler.processAudio(clock, 16);
		QCOMPARE(scheduler.slotPhase(0, 1), SlotPhase::Idle);  // key 61, channel "bad": skipped
		bindings->applyNote(LaunchNote{3, 62, true});
		scheduler.processAudio(clock, 16);
		QCOMPARE(scheduler.slotPhase(0, 2), SlotPhase::Playing);  // key 62, channel 3: an exact match
		bindings->applyNote(LaunchNote{3, 63, true});
		scheduler.processAudio(clock, 16);
		QCOMPARE(scheduler.slotPhase(0, 3), SlotPhase::Idle);  // key 63, channel -1.5: skipped, so 62 keeps playing
		QCOMPARE(scheduler.slotPhase(0, 2), SlotPhase::Playing);
		bindings->applyNote(LaunchNote{5, 64, true});
		scheduler.processAudio(clock, 16);
		QCOMPARE(scheduler.slotPhase(0, 4), SlotPhase::Playing);  // key 64, channel -1: any channel
		bindings->applyNote(LaunchNote{7, 65, true});
		scheduler.processAudio(clock, 16);
		QCOMPARE(scheduler.slotPhase(0, 5), SlotPhase::Idle);  // key 65, no channel field: malformed, not any channel
		QCOMPARE(scheduler.slotPhase(0, 4), SlotPhase::Playing);
		LaunchBindings::disconnect();
		LaunchBindings::setBindings({});
	}
};

QTEST_MAIN(RecordingPassTest)
#include "RecordingPassTest.moc"
