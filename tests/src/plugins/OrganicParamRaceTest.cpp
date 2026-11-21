/*
 * OrganicParamRaceTest.cpp - cluster new-p9-332: the Organic plugin must not
 * let the control thread write the oscillator parameters the audio thread
 * renders from.
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

/*! The Organic plugin's per-oscillator volume and detuning were plain float
 *  members of OscillatorObject. The control thread wrote them (the
 *  updateVolume()/updateDetuning() slots, fired by the knobs) while the audio
 *  thread read them through the references the Oscillator holds - a data race
 *  on the render path.
 *
 *  The contract this test pins down: the control thread PUBLISHES the
 *  parameters, the audio thread ADOPTS them at the start of a render period.
 *  The value the audio thread renders from is therefore only ever written by
 *  the audio thread, and a parameter change reaches the render one period
 *  later instead of racing with it.
 *
 *  The test drives the real plugin code (the plugin implementation is
 *  compiled into this translation unit) and uses no timing, no sleeps and no
 *  threads: every step is a direct call, so the result is deterministic. */

#include <QtTest>

#include "AutomatableModel.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

#define private public // this TU inspects the parameters the audio thread reads
#include "Organic.h"
#undef private

// Keep the implementation in this translation unit so the test exercises the
// same code the plugin module runs.
#include "../../../plugins/Organic/Organic.cpp"

namespace lmms
{

class OrganicParamRaceTest : public QObject
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

	//! The control thread publishes a volume change; the audio thread adopts
	//! it at the start of the next period.
	void controlThreadPublishDoesNotWriteTheAudioThreadsValue()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(
			Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);

		auto* instrument = new OrganicInstrument(track);

		Note note(TimePos(1), TimePos(0));
		NotePlayHandle handle(track, 0, 64, note);

		SampleFrame buffer[64] {};
		std::span<SampleFrame> out(buffer, 64);

		// One render period: the audio thread adopts the published parameters.
		instrument->playNoteImpl(&handle, out);
		const float adopted = instrument->m_osc[0]->m_volumeLeft;
		QVERIFY2(adopted > 0.f,
			"the audio thread did not adopt the published oscillator volume");

		// The control thread publishes a new volume (the knob's dataChanged
		// signal fires updateVolume()).
		instrument->m_osc[0]->m_volModel.setValue(50.0f);

		QVERIFY2(instrument->m_osc[0]->m_volumeLeft == adopted,
			"the control thread wrote the value the audio thread renders from - "
			"the parameters must be published, not written in place");

		// The next render period adopts the published value.
		instrument->playNoteImpl(&handle, out);
		QVERIFY2(instrument->m_osc[0]->m_volumeLeft < adopted,
			"the audio thread did not adopt the published oscillator volume");
	}
};

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::OrganicParamRaceTest)
#include "OrganicParamRaceTest.moc"
