/*
 * OrganicZeroOscTest.cpp - cluster new-p16-152: the Organic plugin must not
 * dereference a null oscillator chain when its oscillator count is zero.
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
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! OrganicInstrument::playNoteImpl() builds the oscillator chain from
 *  m_numOscillators entries and then renders through oscs_l[0]/oscs_r[0].
 *  With a count of zero the construction loop never runs, both chain heads
 *  stay null and the render dereferences them.
 *
 *  The contract this test pins down: a note that has no oscillator is silent,
 *  it never dereferences the null chain head. The test drives the real plugin
 *  code (the plugin implementation is compiled into this translation unit) and
 *  uses no timing, no sleeps and no threads, so the result is deterministic.
 */

#include <QtTest>

#include "AutomatableModel.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

#define private public // this TU drives the oscillator count the render uses
#include "Organic.h"
#undef private

// Keep the implementation in this translation unit so the test exercises the
// same code the plugin module runs.
#include "../../../plugins/Organic/Organic.cpp"

namespace lmms
{

class OrganicZeroOscTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
	}

	void cleanupTestCase()
	{
		Engine::getSong()->clearProject();
		Engine::destroy();
		NotePlayHandleManager::free();
	}

	//! A state with no active oscillator renders silence, it does not crash.
	void zeroOscillatorsRenderSilence()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(
			Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);

		auto* instrument = new OrganicInstrument(track);
		instrument->m_numOscillators = 0;

		Note note(TimePos(1), TimePos(0));
		NotePlayHandle handle(track, 0, 64, note);

		SampleFrame buffer[64] {};
		std::span<SampleFrame> out(buffer, 64);

		// Must not dereference the null oscillator chain head.
		instrument->playNoteImpl(&handle, out);

		for (const auto& frame : buffer)
		{
			QVERIFY2(frame[0] == 0.f && frame[1] == 0.f,
				"a note without oscillators is not silent");
		}
	}

	//! The note data of a note rendered with no oscillator is released by
	//! deleteNotePluginData(), which deletes the two null chain heads and the
	//! state, and never dereferences them. Contract pin for the finding's
	//! example: it passes on the unmodified branch, so it is not RED proof.
	void zeroOscillatorNoteDataIsDeleted()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(
			Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);

		auto* instrument = new OrganicInstrument(track);
		instrument->m_numOscillators = 0;

		Note note(TimePos(1), TimePos(0));
		NotePlayHandle handle(track, 0, 64, note);

		SampleFrame buffer[64] {};
		std::span<SampleFrame> out(buffer, 64);
		instrument->playNoteImpl(&handle, out);
		QVERIFY(handle.m_pluginData != nullptr);

		instrument->deleteNotePluginData(&handle);

		// Cleared so the handle's destructor does not release the data again.
		handle.m_pluginData = nullptr;
	}
};

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::OrganicZeroOscTest)
#include "OrganicZeroOscTest.moc"
