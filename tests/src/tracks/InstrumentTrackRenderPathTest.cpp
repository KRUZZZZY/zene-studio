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

#include <memory>

#include "../core/AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "DetuningHelper.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Song.h"

using namespace lmms;

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
};

QTEST_GUILESS_MAIN(InstrumentTrackRenderPathTest)
#include "InstrumentTrackRenderPathTest.moc"
