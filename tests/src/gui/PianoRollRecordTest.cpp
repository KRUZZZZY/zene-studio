/*
 * PianoRollRecordTest.cpp - a live MIDI record pass in the real piano roll, offscreen
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (if not, see COPYING); if not,
 * write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! The piano roll's live-record pass, in the real application offscreen.
 *
 *  A record pass is the one piano-roll path a test could not reach before: PianoRoll::record()
 *  arms the pass and Song::playMidiClip() starts the transport, and the pass's pending starts
 *  are fed by the instrument track's midiNoteOn / midiNoteOff signals - which a test emits
 *  directly, the way the MIDI input does.
 *
 *  The case here is the stale pending start: hold a key, stop the transport before its key-up,
 *  then record again and play the same pitch.  A start left over from the previous pass sits in
 *  front of the new one, so the release pairs with the OLD start and the note is recorded from
 *  where the abandoned pass began.
 *
 *  The other two suites here are the note-property edits that must be journal steps of their
 *  own: a wheel change and an accepted Enter Value dialog.  Both reach the clip through the
 *  real PianoRoll, and both are undone through the same ProjectJournal the Edit menu uses.
 */

#include <QtTest>

#include <memory>

#include <QApplication>
#include <QInputDialog>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>

#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "PianoRoll.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr int Pitch = 60;
constexpr tick_t FirstPassStart = 0;
constexpr tick_t SecondPassStart = 480;

} // namespace

class PianoRollRecordTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		m_track = Track::create(Track::Type::Instrument, Engine::getSong());
		QVERIFY(m_track != nullptr);
		auto* instrument = dynamic_cast<InstrumentTrack*>(m_track);
		QVERIFY(instrument != nullptr);
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		QVERIFY(m_clip != nullptr);

		m_window = std::make_unique<PianoRollWindow>();
		m_window->resize(1200, 700);
		m_window->setCurrentMidiClip(m_clip);
		m_window->show();
		QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
	}

	void cleanup()
	{
		m_window->stop();
		Engine::getSong()->stop();
		m_window->setCurrentMidiClip(nullptr);
		m_window.reset();
		// The song owns the track and the shell owns its view: clearProject() removes both in
		// the order they were made, so the case does not free a track the view still points at.
		Engine::getSong()->clearProject();
		m_track = nullptr;
		m_clip = nullptr;
	}

	void aStopDuringAHeldNoteLeavesNoStartForTheNextPass()
	{
		auto* instrument = dynamic_cast<InstrumentTrack*>(m_track);

		// Pass one: the key goes down and is never released - the transport stops under it.
		m_window->record();
		QVERIFY(m_window->isRecording());
		QVERIFY(Engine::getSong()->isPlaying());
		Engine::getSong()->setPlayPos(FirstPassStart, Song::PlayMode::MidiClip);
		Q_EMIT instrument->midiNoteOn(Note(TimePos(0), TimePos(0), Pitch));
		QCOMPARE(static_cast<int>(m_clip->notes().size()), 0);

		m_window->stop();
		QVERIFY(!m_window->isRecording());
		QVERIFY(!Engine::getSong()->isPlaying());

		// Pass two: the same pitch, a whole bar later, released properly.
		m_window->record();
		QVERIFY(m_window->isRecording());
		Engine::getSong()->setPlayPos(SecondPassStart, Song::PlayMode::MidiClip);
		Q_EMIT instrument->midiNoteOn(Note(TimePos(0), TimePos(0), Pitch));
		Engine::getSong()->setPlayPos(SecondPassStart + 96, Song::PlayMode::MidiClip);
		Q_EMIT instrument->midiNoteOff(Note(TimePos(96), TimePos(0), Pitch));

		QCOMPARE(static_cast<int>(m_clip->notes().size()), 1);
		// Exactly the second pass's note: it starts where that pass's key-down was, and lasts until its
		// key-up. A release that paired with the abandoned pass's start would move the start to tick 0.
		const Note* recorded = m_clip->notes().front();
		QCOMPARE(static_cast<int>(recorded->pos()), static_cast<int>(SecondPassStart));
		QCOMPARE(static_cast<int>(recorded->length()), 96);
	}

	//! Q050-039: the live handle an MPE note-off stamps its release velocity on
	//! is the Note the roll's record pass is handed, so the note it saves keeps
	//! that velocity (PianoRoll::finishRecordNote).
	void theRecordedNoteKeepsTheReleaseVelocityOfItsNoteOff()
	{
		auto* instrument = dynamic_cast<InstrumentTrack*>(m_track);

		m_window->record();
		QVERIFY(m_window->isRecording());
		QVERIFY(Engine::getSong()->isPlaying());
		Engine::getSong()->setPlayPos(FirstPassStart, Song::PlayMode::MidiClip);

		Q_EMIT instrument->midiNoteOn(Note(TimePos(0), TimePos(0), Pitch));

		// The key-up as the MPE input path hands it to the roll: the note-off
		// velocity rides on the note (InstrumentTrack::trackMpeInputEvent).
		Note release(TimePos(96), TimePos(0), Pitch);
		release.setReleaseVelocity(96);
		Engine::getSong()->setPlayPos(FirstPassStart + 96, Song::PlayMode::MidiClip);
		Q_EMIT instrument->midiNoteOff(release);

		QCOMPARE(static_cast<int>(m_clip->notes().size()), 1);
		QCOMPARE(m_clip->notes().front()->releaseVelocity(), 96);
	}

	//! A live start is paired with the release of its OWN note handle, not merely with the
	//! next release of the same pitch.  Two overlapping starts of one pitch - what MPE sends
	//! on two member channels - released in the opposite order must keep their own start
	//! positions and lengths.
	void overlappingStartsOfOnePitchKeepTheirOwnRelease()
	{
		auto* instrument = dynamic_cast<InstrumentTrack*>(m_track);

		m_window->record();
		QVERIFY(m_window->isRecording());
		QVERIFY(Engine::getSong()->isPlaying());

		Note first(TimePos(0), TimePos(0), Pitch);
		Note second(TimePos(0), TimePos(0), Pitch);

		Engine::getSong()->setPlayPos(FirstPassStart, Song::PlayMode::MidiClip);
		Q_EMIT instrument->midiNoteOn(first);
		Engine::getSong()->setPlayPos(FirstPassStart + 96, Song::PlayMode::MidiClip);
		Q_EMIT instrument->midiNoteOn(second);

		// The key-ups arrive in the opposite order; each carries its own handle identity and
		// the length the engine stamped on it before emitting midiNoteOff.
		second.setLength(TimePos(96));
		Engine::getSong()->setPlayPos(FirstPassStart + 192, Song::PlayMode::MidiClip);
		Q_EMIT instrument->midiNoteOff(second);
		first.setLength(TimePos(240));
		Engine::getSong()->setPlayPos(FirstPassStart + 240, Song::PlayMode::MidiClip);
		Q_EMIT instrument->midiNoteOff(first);

		QCOMPARE(static_cast<int>(m_clip->notes().size()), 2);
		const Note* atZero = nullptr;
		const Note* at96 = nullptr;
		for (const Note* note : m_clip->notes())
		{
			if (note->pos() == TimePos(0)) { atZero = note; }
			if (note->pos() == TimePos(96)) { at96 = note; }
		}
		QVERIFY(atZero != nullptr);
		QVERIFY(at96 != nullptr);
		// Key-only pairing gives these the swapped lengths (96 / 240).
		QCOMPARE(static_cast<int>(atZero->length()), 240);
		QCOMPARE(static_cast<int>(at96->length()), 96);
	}

	//! A wheel edit of the note volume is one journal step: Undo puts the volume back instead
	//! of leaving the wheel change (and unwinding an older checkpoint).
	void aWheelEditIsOneJournalStepAndUndoRestoresTheVolume()
	{
		Note* note = m_clip->addNote(Note(TimePos(96), TimePos(0), Pitch, DefaultVolume), false);
		QVERIFY(note != nullptr);
		note->setSelected(true);

		PianoRoll* roll = m_window->findChild<PianoRoll*>();
		QVERIFY(roll != nullptr);

		const int depth = Engine::projectJournal()->undoDepth();
		QWheelEvent up(QPointF(100, roll->height() - 50), QPointF(100, roll->height() - 50),
			QPoint(0, 0), QPoint(0, 120), Qt::MouseButtons(), Qt::KeyboardModifiers(),
			Qt::ScrollPhase(), false);
		QApplication::sendEvent(roll, &up);

		QCOMPARE(static_cast<int>(note->getVolume()), static_cast<int>(DefaultVolume) + 1);
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth + 1);

		Engine::projectJournal()->undo();
		QCOMPARE(static_cast<int>(m_clip->notes().size()), 1);
		QCOMPARE(static_cast<int>(m_clip->notes().front()->getVolume()), static_cast<int>(DefaultVolume));
	}

	//! An accepted Enter Value is one journal step through its real caller.  The mouse press
	//! that precedes the double click checkpoints the edit area already, so the dialog's
	//! change reverses with it; this guard fails if that caller checkpoint is removed.
	void anAcceptedEnterValueIsOneJournalStepThroughTheMousePath()
	{
		Note* note = m_clip->addNote(Note(TimePos(96), TimePos(0), Pitch, DefaultVolume), false);
		QVERIFY(note != nullptr);
		note->setSelected(true);

		PianoRoll* roll = m_window->findChild<PianoRoll*>();
		QVERIFY(roll != nullptr);
		const QPoint point(100, roll->height() - 50);

		const int depth = Engine::projectJournal()->undoDepth();
		// A real double click is a press (which journals the edit area) followed by the
		// double-click event; QTest::mouseDClick sends only the latter in this build.
		QTest::mouseClick(roll, Qt::LeftButton, Qt::NoModifier, point);
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth + 1);

		bool dialogDriven = false;
		QTimer::singleShot(0, [&dialogDriven] {
			auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
			if (dialog == nullptr) { return; }
			auto* spin = dialog->findChild<QSpinBox*>();
			if (spin == nullptr) { dialog->reject(); return; }
			spin->setValue(64);
			dialog->accept();
			dialogDriven = true;
		});
		QTest::mouseDClick(roll, Qt::LeftButton, Qt::NoModifier, point);
		QVERIFY(dialogDriven);

		QCOMPARE(static_cast<int>(note->getVolume()), 64);
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth + 1);

		Engine::projectJournal()->undo();
		QCOMPARE(static_cast<int>(m_clip->notes().size()), 1);
		QCOMPARE(static_cast<int>(m_clip->notes().front()->getVolume()), static_cast<int>(DefaultVolume));
	}

private:
	QTemporaryDir m_home;
	Track* m_track = nullptr;
	MidiClip* m_clip = nullptr;
	std::unique_ptr<PianoRollWindow> m_window;
};

QTEST_MAIN(PianoRollRecordTest)
#include "PianoRollRecordTest.moc"
