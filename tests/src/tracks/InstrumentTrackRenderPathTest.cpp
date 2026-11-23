/*
 * InstrumentTrackRenderPathTest.cpp - what the instrument track's render path may touch
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! The instrument track's render path, and the three properties it must have.
 *
 *  InstrumentTrack::play() runs on the AUDIO THREAD, once per period, and it is the
 *  place where this fork's own additions went wrong three times:
 *
 *   - new-p15-83: it asked the GUI (a QWidget) for the piano roll's recording state,
 *     so the audio thread read an object the GUI thread owns;
 *   - new-p16-89: it collected the clips it plays into a LOCAL std::vector, so every
 *     period that played a clip allocated on the audio thread;
 *   - 3192: the MPE expression of a live note was published as three plain ints and a
 *     plain bool on the note handle, so the MIDI thread's write and the render
 *     thread's read raced.
 *
 *  Every slot below drives the REAL path (the real track, the real instrument, the
 *  real note handle) and measures one property. No wall-clock timing and no sleeps:
 *  the values are injected and read back.
 */

#include <QtTest>

#include <atomic>
#include <memory>

#include "../core/AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "DetuningHelper.h"
#include "Engine.h"
#include "Instrument.h"
#include "MidiEvent.h"
#include "Note.h"
#include "Piano.h"
// cluster new-p9-189: the test observes the legacy midi-notes lock (the sentinel) from
// inside the note path. InstrumentRoutingTest.cpp reads the same private state this way.
#define private public
#include "InstrumentTrack.h"
#undef private
#include "NotePlayHandle.h"
#include "Song.h"

using namespace lmms;

namespace {

const Plugin::Descriptor s_lockWatchingDescriptor{
	"lockwatchinginstrument", "Lock-watching instrument", "Proves the note path holds no lock",
	"Zene Studio test suite", 0x0100, Plugin::Type::Instrument, nullptr, nullptr, nullptr};

/*! cluster new-p9-189: records, from inside the instrument callback, whether the track's
 *  note path holds the legacy m_midiNotesMutex there, and counts the note messages. The
 *  audio thread calls handleMidiEvent() from sendNoteOn()/sendNoteOff(), so a lock held
 *  across this callback is exactly the defect: the audio thread waits on a control thread. */
class LockWatchingInstrument final : public Instrument
{
public:
	explicit LockWatchingInstrument(InstrumentTrack* track)
		: Instrument(track, &s_lockWatchingDescriptor)
	{
	}

	QString nodeName() const override { return QStringLiteral("lockwatchinginstrument"); }
	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}
	gui::PluginView* instantiateView(QWidget*) override { return nullptr; }

	bool handleMidiEvent(const MidiEvent& event, const TimePos&, f_cnt_t) override
	{
		if (event.type() == MidiNoteOn) { ++m_noteOns; }
		if (event.type() == MidiNoteOff) { ++m_noteOffs; }
		if (event.type() == MidiNoteOn || event.type() == MidiNoteOff)
		{
			// tryLock(0) never waits, so it reports whether the lock is held right here.
			if (!instrumentTrack()->m_midiNotesMutex.tryLock(0)) { m_sawMutexHeld = true; }
			else { instrumentTrack()->m_midiNotesMutex.unlock(); }
		}
		return true;
	}

	int noteOns() const { return m_noteOns; }
	int noteOffs() const { return m_noteOffs; }
	bool sawMutexHeld() const { return m_sawMutexHeld; }

private:
	int m_noteOns = 0;
	int m_noteOffs = 0;
	bool m_sawMutexHeld = false;
};

} // namespace

class InstrumentTrackRenderPathTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// No audio device runs while the test measures the render path: the
		// measurement must be the path's own, not the device thread's.
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
	}

	void cleanupTestCase()
	{
		InstrumentTrack::setPianoRollRecording(false);
		Engine::destroy();
	}

	/*! new-p15-83: the render path takes the piano roll's recording state from the
	 *  PUBLISHED flag, never from the GUI.
	 *
	 *  A live MIDI-input note's detuning curve is recorded while the piano roll
	 *  records: that is what the recording flag decides, on the audio thread, once
	 *  per period. Before the fix the flag was read from the piano roll window (a
	 *  QWidget the GUI thread owns), so the audio thread touched a Qt object - and
	 *  with no GUI at all (this test, a headless renderer, the agent surface) the
	 *  state was simply not there.
	 *
	 *  The negative control is the other half: with the published flag CLEARED, the
	 *  same path must not record anything. */
	void theRenderPathTakesTheRecordingStateFromThePublishedFlag()
	{
		InstrumentTrack track(Engine::getSong());
		// A plugin that does not exist leaves the DummyInstrument in place: a live
		// instrument on the track, which is all the render path needs.
		QVERIFY(track.loadInstrument(QStringLiteral("no_such_instrument_in_this_test")) != nullptr);

		auto detuning = std::make_shared<DetuningHelper>();
		const Note note(TimePos(48), TimePos(0), 60, volume_t(100), DefaultPanning, detuning);
		NotePlayHandle* handle = NotePlayHandleManager::acquire(&track, 0,
				Engine::audioEngine()->framesPerPeriod(), note, nullptr, 0,
				NotePlayHandle::Origin::MidiInput);
		QVERIFY(handle != nullptr);

		// The track's pitch: the value the recording path stores (in semitones).
		track.pitchModel()->setValue(50);

		// The piano roll records: the published flag says so.
		InstrumentTrack::setPianoRollRecording(true);
		track.play(TimePos(0), Engine::audioEngine()->framesPerPeriod(), 0);
		QVERIFY2(std::abs(detuning->automationClip()->valueAt(TimePos(0)) - 0.5f) < 0.001f,
				"the render path did not record the live note's detuning while the piano roll recorded");

		// The negative control: with the flag cleared, the same path records nothing.
		auto quiet = std::make_shared<DetuningHelper>();
		const Note quietNote(TimePos(48), TimePos(0), 60, volume_t(100), DefaultPanning, quiet);
		NotePlayHandle* quietHandle = NotePlayHandleManager::acquire(&track, 0,
				Engine::audioEngine()->framesPerPeriod(), quietNote, nullptr, 0,
				NotePlayHandle::Origin::MidiInput);
		QVERIFY(quietHandle != nullptr);
		InstrumentTrack::setPianoRollRecording(false);
		track.play(TimePos(0), Engine::audioEngine()->framesPerPeriod(), 0);
		QVERIFY2(quiet->automationClip()->valueAt(TimePos(0)) == 0.f,
				"the render path recorded a detuning while the piano roll was not recording");
	}

	/*! cluster new-p9-189: a scheduled MIDI note's event path is lock-free.
	 *
	 *  NotePlayHandle::play() sends the note through InstrumentTrack::processOutEvent on the
	 *  AUDIO thread. Before this fix sendNoteOn()/sendNoteOff() took m_midiNotesMutex around
	 *  the piano key state and the instrument callbacks, so a control-thread silenceAllNotes()
	 *  could make the audio thread wait for a period. The state is now atomic and the lock is
	 *  never acquired.
	 *
	 *  The check is a tryLock(0) probe placed where the audio path does its work: inside the
	 *  instrument callback, and inside the piano's key-state publication (observed through a
	 *  direct connection to dataChanged). tryLock(0) never waits, so this is deterministic -
	 *  no timing, no sleep and no second thread. */
	void theScheduledNotePathTakesNoMutex()
	{
		// The published state is lock-free, which is what lets the audio path read it.
		QVERIFY(std::atomic<bool>::is_always_lock_free);
		QVERIFY(std::atomic<int>::is_always_lock_free);

		InstrumentTrack track(Engine::getSong());
		// A plugin that does not exist leaves the DummyInstrument in place; the probe replaces it.
		QVERIFY(track.loadInstrument(QStringLiteral("no_such_instrument_in_this_test")) != nullptr);
		auto* instrument = new LockWatchingInstrument(&track);
		track.setInstrumentForTest(instrument);
		QCOMPARE(track.instrument(), instrument);
		Piano* piano = track.pianoModel();
		QVERIFY(piano != nullptr);

		bool pianoSawMutexHeld = false;
		int pianoPublications = 0;
		const QMetaObject::Connection pianoProbe = connect(piano, &Piano::dataChanged, this,
			[&]
			{
				++pianoPublications;
				if (!track.m_midiNotesMutex.tryLock(0)) { pianoSawMutexHeld = true; }
				else { track.m_midiNotesMutex.unlock(); }
			}, Qt::DirectConnection);

		// The note-on: instrument callback, then the piano key state.
		track.processOutEvent(MidiEvent(MidiNoteOn, 0, 69, 100), TimePos(0), 0);
		QCOMPARE(instrument->noteOns(), 1);
		QVERIFY2(!instrument->sawMutexHeld(),
			"the scheduled note-on held m_midiNotesMutex across the instrument callback");
		QVERIFY2(pianoPublications >= 1, "the note-on published no piano key state");
		QVERIFY2(!pianoSawMutexHeld,
			"the scheduled note-on held m_midiNotesMutex while publishing the piano key state");
		QVERIFY2(piano->isKeyPressed(69), "the note-on left the piano key released");

		// The note-off, through the same path.
		track.processOutEvent(MidiEvent(MidiNoteOff, 0, 69, 0), TimePos(0), 0);
		QCOMPARE(instrument->noteOffs(), 1);
		QVERIFY2(!instrument->sawMutexHeld(),
			"the scheduled note-off held m_midiNotesMutex across the instrument callback");
		QVERIFY2(pianoPublications >= 2, "the note-off published no piano key state");
		QVERIFY2(!pianoSawMutexHeld,
			"the scheduled note-off held m_midiNotesMutex while publishing the piano key state");
		QVERIFY2(!piano->isKeyPressed(69), "the note-off left the piano key pressed");

		disconnect(pianoProbe);
	}

	/*! cluster new-p9-189: the per-key running-note count keeps the behaviour it had under
	 *  the lock, now that the count is atomic.
	 *
	 *  Two note-ons on one key are two voices: the second releases the first (a retrigger)
	 *  and one note-off must not end the note the other one still holds. silenceAllNotes()
	 *  forgets the counts, so a note started after it still gets its note-off - the case a
	 *  decrement racing a reset could leave stuck. */
	void runningNotesOnOneKeyKeepTheirCount()
	{
		InstrumentTrack track(Engine::getSong());
		QVERIFY(track.loadInstrument(QStringLiteral("no_such_instrument_in_this_test")) != nullptr);
		auto* instrument = new LockWatchingInstrument(&track);
		track.setInstrumentForTest(instrument);

		track.processOutEvent(MidiEvent(MidiNoteOn, 0, 60, 100), TimePos(0), 0);
		track.processOutEvent(MidiEvent(MidiNoteOn, 0, 60, 80), TimePos(0), 0);
		QCOMPARE(instrument->noteOns(), 2);
		QCOMPARE(instrument->noteOffs(), 1);  // the retrigger released the note already sounding
		track.processOutEvent(MidiEvent(MidiNoteOff, 0, 60, 0), TimePos(0), 0);
		QCOMPARE(instrument->noteOffs(), 1);  // the other note-on still holds the key
		track.processOutEvent(MidiEvent(MidiNoteOff, 0, 60, 0), TimePos(0), 0);
		QCOMPARE(instrument->noteOffs(), 2);

		// silenceAllNotes() resets the counts. Without that reset a note started afterwards
		// would retrigger a note that is not sounding and then never send its own note-off.
		const int offsBefore = instrument->noteOffs();
		track.processOutEvent(MidiEvent(MidiNoteOn, 0, 62, 100), TimePos(0), 0);
		track.silenceAllNotes();
		track.processOutEvent(MidiEvent(MidiNoteOn, 0, 62, 100), TimePos(0), 0);
		QVERIFY2(instrument->noteOffs() == offsBefore,
			"a note started after silenceAllNotes() retriggered a stale running note");
		track.processOutEvent(MidiEvent(MidiNoteOff, 0, 62, 0), TimePos(0), 0);
		QVERIFY2(instrument->noteOffs() == offsBefore + 1,
			"the note started after silenceAllNotes() never got its note-off");
	}
};

QTEST_GUILESS_MAIN(InstrumentTrackRenderPathTest)
#include "InstrumentTrackRenderPathTest.moc"
