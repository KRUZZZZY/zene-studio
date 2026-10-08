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
 */

#include <QtTest>

#include <memory>

#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "PianoRoll.h"
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

private:
	QTemporaryDir m_home;
	Track* m_track = nullptr;
	MidiClip* m_clip = nullptr;
	std::unique_ptr<PianoRollWindow> m_window;
};

QTEST_MAIN(PianoRollRecordTest)
#include "PianoRollRecordTest.moc"
