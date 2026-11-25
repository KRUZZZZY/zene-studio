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

#include <array>
#include <atomic>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include <QDomDocument>

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

/*! The arpeggio's modes, as its mode model fills its menu and as a project file stores
 *  the number: InstrumentFunctionArpeggio::ArpMode is private to the class. */
enum ArpModeIndex
{
	FreeArpMode = 0,
	SortArpMode = 1,
	SyncArpMode = 2
};

//! Turns @a track's arpeggio on in @a mode, the way an <arpeggiator> project element does.
void enableArpeggio(InstrumentTrack& track, int mode, int arpRange = 1)
{
	QDomDocument document;
	QDomElement trackSettings = document.createElement(QStringLiteral("instrumenttrack"));
	QDomElement arpeggio = document.createElement(QStringLiteral("arpeggiator"));
	arpeggio.setAttribute(QStringLiteral("arp-enabled"), 1);
	arpeggio.setAttribute(QStringLiteral("arp"), 0);  // the "octave" chord: one pitch per step
	arpeggio.setAttribute(QStringLiteral("arprange"), arpRange);
	arpeggio.setAttribute(QStringLiteral("arprepeats"), 1);
	arpeggio.setAttribute(QStringLiteral("arpmode"), mode);
	arpeggio.setAttribute(QStringLiteral("arpdir"),
		static_cast<int>(InstrumentFunctionArpeggio::ArpDirection::Up));
	arpeggio.setAttribute(QStringLiteral("arptime"), 25);  // the model's minimum
	trackSettings.appendChild(arpeggio);
	track.loadTrackSpecificSettings(trackSettings);
}

/*! The note handles a test has sounding on @a track. They are in the engine's play handle
 *  list - which is where the arpeggio looks for a track's notes, in this order - while the
 *  guard lives, and out of it, and out of the note pool, when it dies. */
class SoundingNotes
{
public:
	SoundingNotes(InstrumentTrack& track, std::initializer_list<int> keys, f_cnt_t frames)
	{
		for (const int key : keys)
		{
			const Note note(TimePos(48), TimePos(0), key);
			NotePlayHandle* handle = NotePlayHandleManager::acquire(&track, 0, frames, note);
			if (handle != nullptr) { m_handles.push_back(handle); }
		}
		for (NotePlayHandle* handle : m_handles) { Engine::audioEngine()->playHandles().append(handle); }
	}

	~SoundingNotes()
	{
		for (NotePlayHandle* handle : m_handles)
		{
			Engine::audioEngine()->playHandles().removeAll(handle);
			NotePlayHandleManager::release(handle);
		}
	}

	SoundingNotes(const SoundingNotes&) = delete;
	SoundingNotes& operator=(const SoundingNotes&) = delete;

	int count() const { return static_cast<int>(m_handles.size()); }
	//! The first handle in play order: the one sort mode lets through.
	NotePlayHandle* first() const { return m_handles.empty() ? nullptr : m_handles.front(); }
	//! The handle of the note at @a key, or nullptr.
	NotePlayHandle* withKey(int key) const
	{
		for (NotePlayHandle* handle : m_handles) { if (handle->key() == key) { return handle; } }
		return nullptr;
	}

private:
	std::vector<NotePlayHandle*> m_handles;
};

//! The allocations and the frees @a work made on the calling thread.
struct ProbeCounts
{
	int allocations;
	int frees;
};

template <typename Work>
ProbeCounts countAllocationsOf(Work&& work)
{
	lmms::test::resetAllocationCount();
	lmms::test::tlCountAllocations = true;
	work();
	lmms::test::tlCountAllocations = false;
	return {static_cast<int>(lmms::test::tlAllocationCount), static_cast<int>(lmms::test::tlFreeCount)};
}

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

	/*! cluster new-p12-94: the arpeggio reads the notes it plays over without building a list.
	 *
	 *  processNote() collected the track's note handles into a QList on EVERY note and every
	 *  period. A QList grows on the heap, and the calls that filled it - the collection, and
	 *  the fallback that appends the processed note when the track has none - allocated and
	 *  freed in the render path, which is what an audible dropout is made of.
	 *
	 *  The probe below counts operator new only, and a QList allocates through Qt's malloc,
	 *  which it does not see (docs/KNOWN-LIMITATIONS.md, "What it still does not prove"). The
	 *  collection's own regression is therefore the static sweep, which now declares this
	 *  function an audio-thread path (tests/rt-safety-scope.txt, cluster new-p12-94). This slot
	 *  is the runtime half of the rule: the same path must make no operator-new allocation
	 *  either, in any mode and with or without other notes sounding.
	 *
	 *  Every case drives the REAL InstrumentFunctionArpeggio::processNote() on the calling
	 *  thread - the thread the engine renders on. The three modes and both note layouts are
	 *  here because the modes read the collection differently: Sort mode takes its size, its
	 *  first note and its keys, Sync mode its size and first note, Free mode nothing at all.
	 *  The notes are 64 frames long, shorter than
	 *  one arpeggio step, so the render loop below the collection plays nothing: what is
	 *  measured is the collection alone (creating an arpeggio note has its own list to grow,
	 *  which is a different defect). */
	void theArpeggioReadsItsNotesWithoutAllocating()
	{
		const std::array<std::pair<int, bool>, 5> cases{{
			{SortArpMode, true},
			{SyncArpMode, true},
			{FreeArpMode, true},
			// No note of the track is sounding: the arpeggio falls back to the track's
			// preset-preview note, and with none of those to the note it is processing.
			{SortArpMode, false},
			{SyncArpMode, false},
		}};

		for (const auto& [mode, notesSounding] : cases)
		{
			InstrumentTrack track(Engine::getSong());
			QVERIFY(track.loadInstrument(QStringLiteral("no_such_instrument_in_this_test")) != nullptr);
			enableArpeggio(track, mode);
			QVERIFY(track.isArpeggioEnabled());

			std::unique_ptr<SoundingNotes> sounding;
			NotePlayHandle* note = nullptr;
			if (notesSounding)
			{
				sounding = std::make_unique<SoundingNotes>(track, std::initializer_list<int>{60, 64, 67}, 64);
				QCOMPARE(sounding->count(), 3);
				// The layout the finding describes: three notes to collect.
				QCOMPARE(static_cast<int>(NotePlayHandle::nphsOfInstrumentTrack(&track).size()), 3);
				note = sounding->first();  // sort mode lets the first note in play order through
			}
			else
			{
				const Note only(TimePos(48), TimePos(0), 60);
				note = NotePlayHandleManager::acquire(&track, 0, 64, only);
			}
			QVERIFY(note != nullptr);

			// The warm-up call pays the arpeggio's one-time construction (its chord table and
			// the models); the measured call is the same path in its steady state.
			track.m_arpeggio.processNote(note);
			const ProbeCounts counts = countAllocationsOf([&] { track.m_arpeggio.processNote(note); });

			const QString what = QStringLiteral("mode %1 with %2 note(s) sounding")
				.arg(mode).arg(notesSounding ? 3 : 1);
			QVERIFY2(counts.allocations == 0, qPrintable(what
				+ QStringLiteral(": the arpeggio allocated %1 time(s) on the render path")
					.arg(counts.allocations)));
			QVERIFY2(counts.frees == 0, qPrintable(what
				+ QStringLiteral(": the arpeggio freed %1 time(s) on the render path")
					.arg(counts.frees)));

			if (notesSounding) { sounding.reset(); }
			else { NotePlayHandleManager::release(note); }
		}
	}

	/*! cluster new-p12-94: sort mode still plays the keys it played before.
	 *
	 *  Sort mode was the only mode that needed the collected notes in an ORDER (it sorted them
	 *  by key and played the note at a position of that order), so the fix has to answer "which
	 *  key sits at position k of the ascending order" without a sorted list. This drives the
	 *  real processNote() one step at a time and reads the key of the note it created.
	 *
	 *  The step the arpeggio plays is (the first note's played frame count + one arp step) / one
	 *  arp step, so the played frame count is INJECTED: no sleep, no second thread, and no
	 *  dependence on how fast the machine renders. The notes are handed over in the order
	 *  67, 60, 64, so the ascending order the arpeggio plays is not the play order. */
	void sortModeArpeggioStillPlaysTheAscendingKeys()
	{
		InstrumentTrack track(Engine::getSong());
		QVERIFY(track.loadInstrument(QStringLiteral("no_such_instrument_in_this_test")) != nullptr);
		// Range 2 with one repeat and a one-pitch chord: six steps - the three base keys in
		// ascending order, then the same three an octave up.
		enableArpeggio(track, SortArpMode, 2);
		QVERIFY(track.isArpeggioEnabled());

		SoundingNotes sounding(track, {67, 60, 64}, 8192);
		QCOMPARE(sounding.count(), 3);
		NotePlayHandle* const first = sounding.first();
		QVERIFY(first != nullptr);
		QCOMPARE(first->key(), 67);
		// The arpeggio's timing runs off the note it would play first once the notes are in
		// ascending key order - 60 here, not the 67 that sort mode lets through.
		NotePlayHandle* const timed = sounding.withKey(60);
		QVERIFY(timed != nullptr);

		const auto arpFrames = static_cast<f_cnt_t>(
			25.0 / 1000.0 * Engine::audioEngine()->outputSampleRate());
		QVERIFY(arpFrames > 0);

		QList<int> played;
		for (int step = 0; step < 4; ++step)
		{
			timed->m_totalFramesPlayed = step * arpFrames;
			const int created = static_cast<int>(first->m_subNotes.size());
			track.m_arpeggio.processNote(first);
			QVERIFY2(static_cast<int>(first->m_subNotes.size()) > created,
				qPrintable(QStringLiteral("step %1 played no arpeggio note").arg(step)));
			played << first->m_subNotes.at(created)->key();
		}

		// The un-sorted play order was 67, 60, 64 and the arpeggio plays 60, 64, 67, 72: the
		// ascending key order, then the lowest key an octave up. Captured from an unfixed
		// render (the RED run logged in the report) and unchanged since.
		QCOMPARE(played, (QList<int>{60, 64, 67, 72}));
	}
};

QTEST_GUILESS_MAIN(InstrumentTrackRenderPathTest)
#include "InstrumentTrackRenderPathTest.moc"
