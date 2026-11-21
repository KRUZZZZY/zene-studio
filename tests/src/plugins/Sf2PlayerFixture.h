/*
 * Sf2PlayerFixture.h - the fixture the SF2 player's regression tests share:
 *                     the product's own plugin module, an instrument of its
 *                     own, and the helpers that read the player back.
 *
 * ONE definition for the tests that use it, the same rule
 * tests/src/core/RackTestSupport.h follows for the rack tests: a second copy
 * of a fixture is the drift this split exists to prevent. The fixture drives
 * the SHIPPED module - it loads the plugin library and calls the player's own
 * public interface - so the assertions are about the product, not about a
 * copy of it.
 *
 * The fixture lives in its own header because the test file that uses it is
 * measured by the file-length ratchet (tests/file-length-gate.sh): a test file
 * that carries the fixture AND every case in it goes over the limit, and the
 * gate answers a new file over the limit with a failure.
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
 */

#ifndef LMMS_SF2_PLAYER_FIXTURE_H
#define LMMS_SF2_PLAYER_FIXTURE_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include <fluidsynth.h>
#include <QLibrary>
#include <QString>

#include "AudioEngine.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Model.h"
#include "NotePlayHandle.h"
#include "Plugin.h"
#include "SampleFrame.h"
#include "Song.h"

namespace sf2test
{

//! The frequency of the strongest period of the signal, found with an
//! autocorrelation. Deterministic: no timing, no randomness.
inline auto fundamentalFrequency(const std::vector<float>& signal, double sampleRate) -> double
{
	if (signal.size() < 4096) { return 0.; }

	// The search starts above the shortest lags: a tone that is mixed with
	// noise correlates with itself at every short lag.
	double best = 0.;
	std::size_t bestLag = 0;
	const std::size_t maxLag = std::min<std::size_t>(4000, signal.size() / 2);
	for (std::size_t lag = 20; lag < maxLag; ++lag)
	{
		double sum = 0.;
		double energyA = 0.;
		double energyB = 0.;
		for (std::size_t i = 0; i + lag < signal.size(); ++i)
		{
			sum += static_cast<double>(signal[i]) * signal[i + lag];
			energyA += static_cast<double>(signal[i]) * signal[i];
			energyB += static_cast<double>(signal[i + lag]) * signal[i + lag];
		}
		if (energyA <= 0. || energyB <= 0.) { continue; }
		const double correlation = sum / std::sqrt(energyA * energyB);
		if (correlation > best) { best = correlation; bestLag = lag; }
	}
	if (bestLag == 0 || best < 0.5) { return 0.; }
	return sampleRate / static_cast<double>(bestLag);
}


//! The SF2 module and an instrument of its own.
struct Sf2Fixture
{
	QLibrary library{QString::fromUtf8(SF2PLAYER_MODULE)};
	std::unique_ptr<lmms::InstrumentTrack> track;
	std::unique_ptr<lmms::Instrument> instrument;

	//! Loads the module and creates the instrument.
	//! @return false when this build has no SF2 player module.
	bool create()
	{
		if (QString::fromUtf8(SF2PLAYER_MODULE).isEmpty()) { return false; }
		library.setLoadHints(QLibrary::PreventUnloadHint);
		if (!library.load()) { return false; }

		auto entry = reinterpret_cast<lmms::Plugin* (*)(lmms::Model*, void*)>(
			library.resolve("lmms_plugin_main"));
		if (entry == nullptr) { return false; }

		track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
		instrument.reset(static_cast<lmms::Instrument*>(entry(track.get(), nullptr)));
		return instrument != nullptr;
	}

	//! The FluidSynth synth the player uses.
	auto synth() const -> fluid_synth_t*
	{
		quintptr address = 0;
		if (!QMetaObject::invokeMethod(instrument.get(), "currentSynthAddress",
				Qt::DirectConnection, Q_RETURN_ARG(quintptr, address)))
		{
			return nullptr;
		}
		return reinterpret_cast<fluid_synth_t*>(address);
	}

	//! The SoundFont the synth plays, read through FluidSynth's own API.
	auto font() const -> fluid_sfont_t*
	{
		return synth() != nullptr ? fluid_synth_get_sfont( synth(), 0 ) : nullptr;
	}

	//! The plugin's own model with that display name.
	template<typename T> auto findModel(const char* name) const -> T*
	{
		const auto models = instrument->findChildren<T*>();
		for (auto* model : models)
		{
			if (model->displayName() == QString::fromUtf8(name)) { return model; }
		}
		return nullptr;
	}

	//! The number of notes the player counts as running.
	auto runningNotes() const -> int
	{
		int running = 0;
		if (!QMetaObject::invokeMethod(instrument.get(), "runningNotesForTest",
				Qt::DirectConnection, Q_RETURN_ARG(int, running)))
		{
			return -1;
		}
		return running;
	}

	//! Fills the player's bounded note event queue, so that the next event it
	//! publishes is refused.
	auto fillNoteEventQueue() const -> int
	{
		int queued = 0;
		if (!QMetaObject::invokeMethod(instrument.get(), "fillNoteEventQueueForTest",
				Qt::DirectConnection, Q_RETURN_ARG(int, queued)))
		{
			return -1;
		}
		return queued;
	}

	//! Plays a note and returns the frequency of the tone in the rendered audio.
	auto playedFrequency(std::span<lmms::SampleFrame> span, int key) const -> double
	{
		const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
		// The note lasts for the whole measurement, so the tone is there all
		// the time.
		const auto noteFrames = static_cast<lmms::f_cnt_t>(frames * 32);
		const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(noteFrames)},
			lmms::TimePos{0}, key};
		lmms::NotePlayHandle handle{track.get(), 0, noteFrames, note, nullptr, -1,
			lmms::NotePlayHandle::Origin::MidiClip};

		std::vector<float> mono;
		for (int period = 0; period < 32; ++period)
		{
			instrument->playNote(&handle, span);
			instrument->play(span);
			for (const auto& frame : span) { mono.push_back(frame[0]); }
		}
		return fundamentalFrequency(mono, lmms::Engine::audioEngine()->outputSampleRate());
	}
};

} // namespace sf2test

#endif // LMMS_SF2_PLAYER_FIXTURE_H
