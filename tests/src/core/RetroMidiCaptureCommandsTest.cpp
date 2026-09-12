/*
 * RetroMidiCaptureCommandsTest.cpp - the retrospective MIDI capture surface:
 *                                    the note matcher, the commands, the
 *                                    persisted arm switch (owner item 14)
 *
 * Copyright (c) 2026 LMMS developers
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

#include <cstdint>
#include <vector>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Midi.h"
#include "MidiClient.h"
#include "MidiClip.h"
#include "MidiEvent.h"
#include "Note.h"
#include "RetroMidiCapture.h"
#include "RetroMidiClipWriter.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

//! One note as the tests read it back off the clip.
struct WrittenNote
{
	int key;
	tick_t position;
	tick_t length;
	int velocity;
};

std::vector<WrittenNote> notesOf(const MidiClip* clip)
{
	std::vector<WrittenNote> notes;
	for (const Note* note : clip->notes())
	{
		notes.push_back(WrittenNote{note->key(), note->pos().getTicks(),
			note->length().getTicks(), static_cast<int>(note->getVolume())});
	}
	return notes;
}

//! A ring element with nothing left to chance (the writer's input, built the
//! way RetroMidiCapture::capture() builds one).
RetroMidiEvent element(std::uint8_t type, std::uint32_t tick, std::uint8_t param1,
	std::uint8_t param2, std::uint8_t channel = 0)
{
	RetroMidiEvent event{};
	event.tick = tick;
	event.source = 0;
	event.type = type;
	event.channel = channel;
	event.param1 = param1;
	event.param2 = param2;
	return event;
}

MidiEvent noteOn(int channel, int key, int velocity)
{
	return MidiEvent(MidiNoteOn, channel, key, velocity);
}

MidiEvent noteOff(int channel, int key)
{
	return MidiEvent(MidiNoteOff, channel, key, 0);
}

} // namespace


class RetroMidiCaptureCommandsTest : public QObject
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

	// ------------------------------------------------------------------
	// the note matcher, against a real MidiClip
	// ------------------------------------------------------------------

	//! Creates an instrument track with one empty MIDI clip; both ids come back
	//! through the out-parameters because QVERIFY's `return` needs a void scope.
	void makeInstrumentClip(QString* trackId, QString* clipId)
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult track = registry->invoke(QStringLiteral("track.add"),
			QJsonObject{{QStringLiteral("type"), QStringLiteral("instrument")}});
		QVERIFY2(track.ok, qPrintable(track.errorMessage));
		*trackId = track.result.value(QStringLiteral("track")).toString();
		const ControlResult clip = registry->invoke(QStringLiteral("clip.add"),
			QJsonObject{{QStringLiteral("track"), *trackId},
				{QStringLiteral("position"), 0},
				{QStringLiteral("length"), 192}});
		QVERIFY2(clip.ok, qPrintable(clip.errorMessage));
		*clipId = clip.result.value(QStringLiteral("clip")).toString();
	}

	MidiClip* resolvedClip(const QString& clipId)
	{
		ControlResult error;
		control::ClipRef ref;
		MidiClip* clip = control::resolveMidiClip(clipId, &ref, &error);
		return clip;
	}

	//! AN EMPTY WINDOW WRITES NOTHING and reports nothing consumed - the
	//! "Capture MIDI" case with no capture armed.
	void emptyWindowWritesNothing()
	{
		QString trackId;
		QString clipId;
		makeInstrumentClip(&trackId, &clipId);
		MidiClip* clip = resolvedClip(clipId);
		QVERIFY(clip != nullptr);

		const RetroMidiClipWrite written = writeWindowToClip(clip, nullptr, 0, 0);
		QCOMPARE(written.notes, 0);
		QCOMPARE(written.consumed, 0);
		QVERIFY(clip->notes().empty());

		// A null clip is a refusal, not a crash.
		const RetroMidiEvent one = element(MidiNoteOn, 10, 60, 100);
		const RetroMidiClipWrite refused = writeWindowToClip(nullptr, &one, 1, 10);
		QCOMPARE(refused.notes, 0);
	}

	//! ONE PAIR becomes exactly one note, with the caller's ticks taken as given.
	void singleNoteOnOffWritesOneNote()
	{
		QString trackId;
		QString clipId;
		makeInstrumentClip(&trackId, &clipId);
		MidiClip* clip = resolvedClip(clipId);
		QVERIFY(clip != nullptr);

		const RetroMidiEvent window[] = {
			element(MidiNoteOn, 100, 60, 100),
			element(MidiNoteOff, 148, 60, 64),
		};
		const RetroMidiClipWrite written = writeWindowToClip(clip, window, 2, 100);

		QCOMPARE(written.notes, 1);
		QCOMPARE(written.unmatchedOns, 0);
		QCOMPARE(written.unmatchedOffs, 0);
		QCOMPARE(written.consumed, 2);
		QCOMPARE(written.windowStart, 100);
		// The window ends at the newest NOTE event, which is this release (a
		// control change after it would not move the end).
		QCOMPARE(written.windowEnd, 148);
		const std::vector<WrittenNote> notes = notesOf(clip);
		QCOMPARE(static_cast<int>(notes.size()), 1);
		QCOMPARE(notes[0].key, 60);
		QCOMPARE(notes[0].position, 0);   // 100 - windowStart 100
		QCOMPARE(notes[0].length, 48);
		QCOMPARE(notes[0].velocity, 100);
	}

	//! OVERLAPPING notes: two keys sounding at once are two notes, and a
	//! release closes the OLDEST open note of its own (channel, key) - FIFO -
	//! so a repeated key does not swallow the first note's length.
	void overlappingWindowWritesEachNoteItsOwnLength()
	{
		QString trackId;
		QString clipId;
		makeInstrumentClip(&trackId, &clipId);
		MidiClip* clip = resolvedClip(clipId);
		QVERIFY(clip != nullptr);

		// Two different keys overlap; then the same key twice, its first release
		// arriving after the second strike.
		const RetroMidiEvent window[] = {
			element(MidiNoteOn, 0, 60, 100),    // A: 0..30
			element(MidiNoteOn, 10, 64, 90),    // B: 10..20
			element(MidiNoteOff, 20, 64, 0),    // closes B
			element(MidiNoteOn, 25, 60, 80),    // C: 25..40, same key as A
			element(MidiNoteOff, 30, 60, 0),    // closes A (oldest 60), not C
			element(MidiNoteOff, 40, 60, 0),    // closes C
		};
		const RetroMidiClipWrite written = writeWindowToClip(clip, window, 6, 0);

		QCOMPARE(written.notes, 3);
		QCOMPARE(written.unmatchedOns, 0);
		QCOMPARE(written.unmatchedOffs, 0);
		QCOMPARE(written.windowEnd, 40);
		const std::vector<WrittenNote> notes = notesOf(clip);
		QCOMPARE(static_cast<int>(notes.size()), 3);
		// The clip's list is position-sorted (MidiClip::addNote).
		QCOMPARE(notes[0].key, 60);
		QCOMPARE(notes[0].position, 0);
		QCOMPARE(notes[0].length, 30);   // closed by the 30 release, FIFO
		QCOMPARE(notes[1].key, 64);
		QCOMPARE(notes[1].position, 10);
		QCOMPARE(notes[1].length, 10);
		QCOMPARE(notes[2].key, 60);
		QCOMPARE(notes[2].position, 25);
		QCOMPARE(notes[2].length, 15);
	}

	//! An unmatched note-on is closed at the window's end (one tick minimum)
	//! AND counted, so the caller can see the capture was cut off.
	void unmatchedNoteOnIsClosedAtTheWindowEnd()
	{
		QString trackId;
		QString clipId;
		makeInstrumentClip(&trackId, &clipId);
		MidiClip* clip = resolvedClip(clipId);
		QVERIFY(clip != nullptr);

		const RetroMidiEvent window[] = {
			element(MidiNoteOn, 0, 60, 100),    // never released
			element(MidiControlChange, 12, 7, 64),
			element(MidiNoteOn, 40, 62, 100),   // never released; sets the window end
		};
		const RetroMidiClipWrite written = writeWindowToClip(clip, window, 3, 0);

		QCOMPARE(written.notes, 2);
		QCOMPARE(written.unmatchedOns, 2);
		QCOMPARE(written.skipped, 1);
		QCOMPARE(written.windowEnd, 40);
		const std::vector<WrittenNote> notes = notesOf(clip);
		QCOMPARE(static_cast<int>(notes.size()), 2);
		QCOMPARE(notes[0].length, 40);
		QCOMPARE(notes[1].length, 1);  // closed at the end, clamped to one tick
	}

	//! A release with no note-on before it (the capture started mid-note) is
	//! counted, never invented into a note.
	void releaseWithoutNoteOnIsCounted()
	{
		QString trackId;
		QString clipId;
		makeInstrumentClip(&trackId, &clipId);
		MidiClip* clip = resolvedClip(clipId);
		QVERIFY(clip != nullptr);

		const RetroMidiEvent window[] = {element(MidiNoteOff, 0, 60, 0)};
		const RetroMidiClipWrite written = writeWindowToClip(clip, window, 1, 0);
		QCOMPARE(written.notes, 0);
		QCOMPARE(written.unmatchedOffs, 1);
		QVERIFY(clip->notes().empty());
	}

	//! A note-on with velocity 0 is a note-off (the running-status form).
	void zeroVelocityNoteOnIsARelease()
	{
		QString trackId;
		QString clipId;
		makeInstrumentClip(&trackId, &clipId);
		MidiClip* clip = resolvedClip(clipId);
		QVERIFY(clip != nullptr);

		const RetroMidiEvent window[] = {
			element(MidiNoteOn, 0, 60, 100),
			element(MidiNoteOn, 24, 60, 0),
		};
		const RetroMidiClipWrite written = writeWindowToClip(clip, window, 2, 0);
		QCOMPARE(written.notes, 1);
		QCOMPARE(written.unmatchedOns, 0);
		const std::vector<WrittenNote> notes = notesOf(clip);
		QCOMPARE(static_cast<int>(notes.size()), 1);
		QCOMPARE(notes[0].length, 24);
	}

	//! Non-note events are counted and written nowhere.
	void nonNoteEventsAreSkipped()
	{
		QString trackId;
		QString clipId;
		makeInstrumentClip(&trackId, &clipId);
		MidiClip* clip = resolvedClip(clipId);
		QVERIFY(clip != nullptr);

		const RetroMidiEvent window[] = {
			element(MidiControlChange, 0, 7, 100),
			element(MidiPitchBend, 1, 0, 64),
			element(MidiNoteOn, 2, 60, 100),
			element(MidiNoteOff, 10, 60, 0),
			element(MidiSysEx, 11, 0, 0),
		};
		const RetroMidiClipWrite written = writeWindowToClip(clip, window, 5, 0);
		QCOMPARE(written.notes, 1);
		QCOMPARE(written.skipped, 3);
		QCOMPARE(written.consumed, 5);
		QCOMPARE(static_cast<int>(clip->notes().size()), 1);
	}

	// ------------------------------------------------------------------
	// the commands
	// ------------------------------------------------------------------

	//! The three commands are registered with the contract's parts, and the two
	//! modes are headless-safe (empty `requires`), which is what the A15 reverse
	//! sweep requires of them.
	void theThreeCommandsAreRegistered()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QStringList required = {
			QStringLiteral("midi.retro_capture_arm"),
			QStringLiteral("midi.retro_capture_status"),
			QStringLiteral("midi.retro_capture_to_clip"),
		};
		for (const QString& id : required)
		{
			QVERIFY2(registry->hasCommand(id), qPrintable(id));
			const ControlCommand* cmd = registry->command(id);
			QVERIFY(cmd != nullptr);
			QCOMPARE(cmd->group, QStringLiteral("midi"));
			QVERIFY(!cmd->verb.isEmpty());
			QVERIFY(!cmd->description.isEmpty());
			QVERIFY(!cmd->argsSchema.isEmpty());
			QVERIFY(!cmd->resultSchema.isEmpty());
			QVERIFY2(cmd->requiresDecl.isEmpty(),
				qPrintable(id + " declares a requirement; it is headless-safe"));
		}
		QVERIFY(registry->command(QStringLiteral("midi.retro_capture_to_clip"))->mutating);
		QVERIFY(!registry->command(QStringLiteral("midi.retro_capture_arm"))->mutating);
		QVERIFY(!registry->command(QStringLiteral("midi.retro_capture_status"))->mutating);
	}

	//! Arm, re-arm, disarm: `changed` says whether the mode moved, and the
	//! status command reads the same flag.
	void armDisarmAndStatusThroughTheCommands()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		MidiClient* client = Engine::audioEngine() != nullptr
			? Engine::audioEngine()->midiClient() : nullptr;
		QVERIFY(client != nullptr);

		const ControlResult disarmed = registry->invoke(QStringLiteral("midi.retro_capture_arm"),
			QJsonObject{{QStringLiteral("armed"), false}});
		QVERIFY2(disarmed.ok, qPrintable(disarmed.errorMessage));
		QVERIFY(!disarmed.result.value(QStringLiteral("armed")).toBool());
		QVERIFY(!client->retroCapture().isArmed());

		// No argument means arm - the deterministic reading of the verb.
		const ControlResult armed = registry->invoke(QStringLiteral("midi.retro_capture_arm"));
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		QVERIFY(armed.result.value(QStringLiteral("armed")).toBool());
		QVERIFY(armed.result.value(QStringLiteral("changed")).toBool());
		QVERIFY(client->retroCapture().isArmed());
		QCOMPARE(armed.result.value(QStringLiteral("capacity_events")).toInt(),
			static_cast<int>(client->retroCapture().ring().capacity()));

		// Arming an armed capture changes nothing.
		const ControlResult again = registry->invoke(QStringLiteral("midi.retro_capture_arm"));
		QVERIFY2(again.ok, qPrintable(again.errorMessage));
		QVERIFY(again.result.value(QStringLiteral("armed")).toBool());
		QVERIFY(!again.result.value(QStringLiteral("changed")).toBool());

		const ControlResult status = registry->invoke(QStringLiteral("midi.retro_capture_status"));
		QVERIFY2(status.ok, qPrintable(status.errorMessage));
		QVERIFY(status.result.value(QStringLiteral("armed")).toBool());
		QCOMPARE(status.result.value(QStringLiteral("capacity_events")).toInt(),
			static_cast<int>(client->retroCapture().ring().capacity()));
		QCOMPARE(status.result.value(QStringLiteral("events_buffered")).toInt(),
			static_cast<int>(client->retroCapture().ring().bufferedCount()));
		QVERIFY(!status.result.value(QStringLiteral("client")).toString().isEmpty());

		const ControlResult off = registry->invoke(QStringLiteral("midi.retro_capture_arm"),
			QJsonObject{{QStringLiteral("armed"), false}});
		QVERIFY2(off.ok, qPrintable(off.errorMessage));
		QVERIFY(!off.result.value(QStringLiteral("armed")).toBool());
		QVERIFY(off.result.value(QStringLiteral("changed")).toBool());
		QVERIFY(!client->retroCapture().isArmed());
	}

	//! An unarmed capture has nothing to write: a typed refusal, no clip.
	void toClipRefusesWhenNothingWasCaptured()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const int clipsBefore = registry->invoke(QStringLiteral("arrangement.get_state"))
			.result.value(QStringLiteral("clip_count")).toInt();

		const ControlResult captured = registry->invoke(QStringLiteral("midi.retro_capture_to_clip"));
		QVERIFY2(!captured.ok, "an empty capture window was written to a clip");
		QCOMPARE(captured.errorKind, ControlErrorKind::NotFound);
		QVERIFY(captured.errorMessage.contains(QStringLiteral("empty")));

		const int clipsAfter = registry->invoke(QStringLiteral("arrangement.get_state"))
			.result.value(QStringLiteral("clip_count")).toInt();
		QCOMPARE(clipsAfter, clipsBefore);
	}

	//! The persisted switch: the config key is the restored state, and the arm
	//! command rewrites it only when the mode moved.
	void persistedArmSwitchRoundTrips()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		MidiClient* client = Engine::audioEngine() != nullptr
			? Engine::audioEngine()->midiClient() : nullptr;
		QVERIFY(client != nullptr);
		ConfigManager* config = ConfigManager::inst();

		// Leave the store as we found it (the TelemetryTest convention).
		const QString previous = config->value(QStringLiteral("midi"),
			QStringLiteral("retrocapture"), QString());

		// The persisted key is applied by the startup hook, not by the capture
		// object's constructor (which runs before main()).
		config->setValue(QStringLiteral("midi"), QStringLiteral("retrocapture"), QStringLiteral("1"));
		applyPersistedRetroCaptureArm();
		QVERIFY(client->retroCapture().isArmed());

		config->setValue(QStringLiteral("midi"), QStringLiteral("retrocapture"), QStringLiteral("0"));
		applyPersistedRetroCaptureArm();
		QVERIFY(!client->retroCapture().isArmed());

		// The command writes the key when - and only when - the mode moves.
		const ControlResult armed = registry->invoke(QStringLiteral("midi.retro_capture_arm"));
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		QVERIFY(armed.result.value(QStringLiteral("persisted")).toBool());
		QCOMPARE(config->value(QStringLiteral("midi"), QStringLiteral("retrocapture"), QString()),
			QStringLiteral("1"));

		const ControlResult disarmed = registry->invoke(QStringLiteral("midi.retro_capture_arm"),
			QJsonObject{{QStringLiteral("armed"), false}});
		QVERIFY2(disarmed.ok, qPrintable(disarmed.errorMessage));
		QCOMPARE(config->value(QStringLiteral("midi"), QStringLiteral("retrocapture"), QString()),
			QStringLiteral("0"));
		QVERIFY(!disarmed.result.value(QStringLiteral("persisted")).toBool());

		// Restore what the runner's config held before this test.
		if (previous.isEmpty())
		{
			config->deleteValue(QStringLiteral("midi"), QStringLiteral("retrocapture"));
		}
		else
		{
			config->setValue(QStringLiteral("midi"), QStringLiteral("retrocapture"), previous);
		}
		config->saveConfigFile();
	}

	//! The whole seam end to end: record through RetroMidiCapture::capture()
	//! (the MIDI-thread entry point), then materialise the window into ONE clip
	//! on a named track, with one journalled checkpoint under it.
	void captureThenToClipWritesTheWindowIntoOneClip()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		MidiClient* client = Engine::audioEngine() != nullptr
			? Engine::audioEngine()->midiClient() : nullptr;
		QVERIFY(client != nullptr);
		RetroMidiCapture& capture = client->retroCapture();

		const int bufferedBefore = static_cast<int>(capture.ring().bufferedCount());
		QVERIFY(registry->invoke(QStringLiteral("midi.retro_capture_arm"),
			QJsonObject{{QStringLiteral("armed"), true}}).ok);

		// Two notes, plus a CC the matcher must skip.
		capture.capture(noteOn(0, 60, 100), 480);
		capture.capture(noteOff(0, 60), 528);
		capture.capture(noteOn(0, 64, 90), 600);
		capture.capture(noteOff(0, 64), 624);
		capture.capture(MidiEvent(MidiControlChange, 0, 7, 100), 700);

		const ControlResult status = registry->invoke(QStringLiteral("midi.retro_capture_status"));
		QVERIFY2(status.ok, qPrintable(status.errorMessage));
		QCOMPARE(status.result.value(QStringLiteral("events_buffered")).toInt(),
			bufferedBefore + 5);
		QVERIFY(status.result.value(QStringLiteral("ticks_span")).toInt() >= 220);
		QVERIFY(status.result.value(QStringLiteral("seconds_span")).toDouble() > 0.0);

		QString trackId;
		QString unusedClipId;
		makeInstrumentClip(&trackId, &unusedClipId);
		const int clipsBefore = registry->invoke(QStringLiteral("arrangement.get_state"))
			.result.value(QStringLiteral("clip_count")).toInt();

		const ControlResult captured = registry->invoke(
			QStringLiteral("midi.retro_capture_to_clip"),
			QJsonObject{{QStringLiteral("track"), trackId}});
		QVERIFY2(captured.ok, qPrintable(captured.errorMessage));
		const QString clipId = captured.result.value(QStringLiteral("clip")).toString();
		QVERIFY(clipId.startsWith(QStringLiteral("clip-")));
		QCOMPARE(captured.result.value(QStringLiteral("track")).toString(), trackId);
		QCOMPARE(captured.result.value(QStringLiteral("events_written")).toInt(), 2);
		QCOMPARE(captured.result.value(QStringLiteral("unmatched_ons")).toInt(), 0);
		QCOMPARE(captured.result.value(QStringLiteral("events")).toInt(), bufferedBefore + 5);
		// The clip starts at the window's first tick and is a whole number of
		// bars: the window is 480..700 (220 ticks), which is 2 bars of 192.
		QCOMPARE(captured.result.value(QStringLiteral("position")).toInt(), 480);
		QCOMPARE(captured.result.value(QStringLiteral("length")).toInt(),
			2 * Engine::getSong()->ticksPerBar());
		QCOMPARE(registry->invoke(QStringLiteral("arrangement.get_state"))
			.result.value(QStringLiteral("clip_count")).toInt(), clipsBefore + 1);

		MidiClip* clip = resolvedClip(clipId);
		QVERIFY(clip != nullptr);
		const std::vector<WrittenNote> notes = notesOf(clip);
		QVERIFY(static_cast<int>(notes.size()) >= 2);
		bool sawFirst = false;
		bool sawSecond = false;
		for (const WrittenNote& note : notes)
		{
			if (note.key == 60 && note.position == 0 && note.length == 48) { sawFirst = true; }
			if (note.key == 64 && note.position == 120 && note.length == 24) { sawSecond = true; }
		}
		QVERIFY2(sawFirst, "the 60 note is missing from the captured clip");
		QVERIFY2(sawSecond, "the 64 note is missing from the captured clip");

		// SPEC A16: the transaction records clip.delete as the inverse, and one
		// control.undo removes the whole capture.
		const ControlResult undo = registry->invoke(QStringLiteral("control.undo"));
		QVERIFY2(undo.ok, qPrintable(undo.errorMessage));
		QCOMPARE(registry->invoke(QStringLiteral("arrangement.get_state"))
			.result.value(QStringLiteral("clip_count")).toInt(), clipsBefore);
		// The clip is gone from the arrangement - the undo restored the track.
		QVERIFY(resolvedClip(clipId) == nullptr);

		registry->invoke(QStringLiteral("midi.retro_capture_arm"),
			QJsonObject{{QStringLiteral("armed"), false}});
	}
};

QTEST_GUILESS_MAIN(RetroMidiCaptureCommandsTest)

#include "RetroMidiCaptureCommandsTest.moc"
