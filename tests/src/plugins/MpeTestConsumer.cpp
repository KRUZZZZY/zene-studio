/*
 * MpeTestConsumer.cpp - the instrument that CONSUMES per-note MPE pressure
 *                       and timbre, so their delivery can be measured
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*! A TEST SUBJECT, not a product plug-in (task #649, docs/MPE.md).
 *
 *  Why it exists: the tree's own instruments either ignore MIDI entirely
 *  (TripleOscillator and the rest of the built-in synthesisers are driven by
 *  a note's frequency and volume, and `Instrument::handleMidiEvent` is the
 *  base no-op) or hand MIDI to a plug-in binary nobody has installed
 *  (Vestige / Lv2Instrument / ClapEffect / CarlaBase). So when task #649
 *  routes a note's captured channel pressure and CC74 to "the consuming
 *  instrument", NOTHING IN THE TREE could be observed consuming them: a test
 *  that only checked the events left the engine would be exactly the
 *  "drivable but inert" shape this project keeps paying for. This fixture is
 *  the consumer, and it is the vehicle of the proof: the registered ctest
 *  `MpePlaybackTest` renders one block through it with the expression and the
 *  same block without, and asserts the measured level differs by the ratio
 *  the expression asks for.
 *
 *  What it does, in the smallest honest form:
 *
 *   - it keeps, per MIDI channel, the channel pressure and the CC74 (MPE
 *     timbre) that reached it through `Instrument::handleMidiEvent`, which is
 *     the track's own instrument-facing path
 *     (InstrumentTrack::processOutEvent -> m_instrument->handleMidiEvent);
 *   - a note-on resets that channel's two axes, so a block is never coloured
 *     by the note before it;
 *   - `playNoteImpl` renders a CONSTANT level for the note:
 *
 *         level = kBaseLevel * (1 + pressure/127) * (1 + timbre/127)
 *
 *     i.e. +100% for a fully pressed axis, exactly 1.0 when no expression
 *     reached it. A constant is deliberate: it makes the measurement exact
 *     (no DSP coincidence can pass for the axis being applied) and it is the
 *     same technique tests/data/vst3-test-instrument uses for its own
 *     MIDI-to-audio proof.
 *
 *  Realtime contract: the audio path allocates nothing, locks nothing and
 *  grows nothing - fixed arrays, plain int writes, and a loop over the frames
 *  the note asked for. It is a test subject that runs on the audio thread.
 *
 *  Never installed: `tests/CMakeLists.txt` builds it from tests/ (never from
 *  plugins/) into its own directory, and it is not in the product's plug-in
 *  set or its install rules.
 */

#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MidiEvent.h"
#include "MpeExpression.h"
#include "NotePlayHandle.h"
#include "Plugin.h"
#include "SampleFrame.h"

#include "embed.h"

#include <algorithm>
#include <cstddef>
#include <span>

// This module is not built by cmake/modules/BuildPlugin.cmake, so it has no
// generated plugin_export.h. Mirror tests/reference/plugin_export.h - the
// header the ported reference plugins use - so the fixture compiles with
// cl.exe as well as with GCC/Clang, exactly as tests/src/plugins/
// SyntheticAudioPlugin.cpp does for the same reason.
#if defined(_MSC_VER)
#define PLUGIN_EXPORT __declspec(dllexport)
#else
#define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT mpe_test_consumer_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),          // name - must match the module file
	"MPE Test Consumer",                    // displayName
	QT_TRANSLATE_NOOP( "PluginBrowser", "test subject: consumes per-note MPE pressure and timbre" ),
	"Zene Studio test suite",               // author
	0x0100,
	Plugin::Type::Instrument,
	nullptr,                                // logo: a test subject needs none
	nullptr,                                // supportedFileTypes
	nullptr,                                // subPluginFeatures
};

} // extern "C"

namespace
{

//! The level a note renders at when NOTHING reached this instrument. Any
//! non-zero value works; the test asserts RATIOS, so the number itself only
//! has to be far enough from 0 to be measurable.
constexpr float kBaseLevel = 0.25f;

//! MPE's sixteen channels, indexed the way MidiEvent::channel() indexes them.
constexpr int kChannelCount = 16;

//! The multiplier the two consumed axes ask for: each is worth up to +100%.
float expressionGain( int pressure, int timbre )
{
	return ( 1.0f + static_cast<float>( pressure ) / MidiMaxControllerValue )
		* ( 1.0f + static_cast<float>( timbre ) / MidiMaxControllerValue );
}

class MpeTestConsumer final : public Instrument
{
public:
	explicit MpeTestConsumer( InstrumentTrack* instrumentTrack ) :
		Instrument( instrumentTrack, &mpe_test_consumer_plugin_descriptor )
	{
	}

	//! Called by the track for every MIDI event aimed at the instrument -
	//! from a playing note (InstrumentTrack::processOutEvent) and from live
	//! MIDI input alike. Realtime-safe: fixed arrays and int writes.
	bool handleMidiEvent( const MidiEvent& event, const TimePos& = TimePos(),
						f_cnt_t = 0 ) override
	{
		const int channel = std::clamp<int>( event.channel(), 0, kChannelCount - 1 );

		switch( event.type() )
		{
			case MidiNoteOn:
				// A note starts with its channel's axes neutral: whatever the
				// previous note left behind must not colour this block.
				m_pressure[channel] = 0;
				m_timbre[channel] = 0;
				return true;

			case MidiChannelPressure:
				m_pressure[channel] = std::clamp<int>( event.channelPressure(), 0, 127 );
				return true;

			case MidiControlChange:
				// CC74 is MPE's timbre ("Y") axis; every other controller is
				// not this instrument's business.
				if( event.controllerNumber() == MpeTimbreController )
				{
					m_timbre[channel] = std::clamp<int>( event.controllerValue(), 0, 127 );
					return true;
				}
				return false;

			default:
				return false;
		}
	}

	//! Instrument (a SerializingObject) requires the three serialization
	//! entry points. A test subject has no state of its own to persist: the
	//! expression it consumes belongs to the NOTE and is saved with it
	//! (Note::saveSettings writes mpepitch/mpepressure/mpetimbre), so these
	//! are deliberately no-ops rather than a second store.
	QString nodeName() const override { return QStringLiteral( "mpetestconsumer" ); }
	void saveSettings( QDomDocument&, QDomElement& ) override {}
	void loadSettings( const QDomElement& ) override {}

protected:
	void playNoteImpl( NotePlayHandle* nph, std::span<SampleFrame> out ) override
	{
		const int channel = std::clamp<int>( nph->midiChannel(), 0, kChannelCount - 1 );
		const float level = kBaseLevel * expressionGain( m_pressure[channel], m_timbre[channel] );

		const f_cnt_t frames = nph->framesLeftForCurrentPeriod();
		const f_cnt_t offset = nph->noteOffset();

		for( f_cnt_t f = 0; f < frames; ++f )
		{
			const std::size_t at = static_cast<std::size_t>( offset ) + f;
			if( at >= out.size() )
			{
				break;
			}
			out[at][0] = level;
			out[at][1] = level;
		}
	}

	//! No interface, deliberately (KNOWN-LIMITATIONS: this feature is drivable
	//! through the socket, not from the interface).
	gui::PluginView* instantiateView( QWidget* ) override
	{
		return nullptr;
	}

private:
	int m_pressure[kChannelCount] = {};
	int m_timbre[kChannelCount] = {};
};

} // namespace

extern "C"
{

PLUGIN_EXPORT Plugin* lmms_plugin_main( Model* parent, void* )
{
	return new MpeTestConsumer( static_cast<InstrumentTrack*>( parent ) );
}

} // extern "C"

} // namespace lmms
