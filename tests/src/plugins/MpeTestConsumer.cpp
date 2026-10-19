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
 *     (InstrumentTrack::processOutEvent -> m_instrument->handleMidiEvent), and
 *     the release velocity a `MidiNoteOff` carried (Q050-039);
 *   - a note-on resets that channel's axes and its release velocity, so a
 *     block is never coloured by the note before it; a note-off clears the
 *     two axes and sets the release velocity;
 *   - it also keeps the 14-bit pitch bend that reached it, converted to the
 *     same cents the engine's frequency path uses, so Q050-016's streamed
 *     pitch can be measured too;
 *   - `playNoteImpl` renders a CONSTANT level for the note:
 *
 *         level = kBaseLevel * (1 + pressure/127) * (1 + timbre/127)
 *                            * (1 + releaseVelocity/127)
 *                            * (1 + cents/4800)
 *
 *     i.e. +100% for a fully pressed axis or a full-scale release velocity and
 *     up to +100% for a full bend, exactly 1.0 when nothing reached it. A
 *     constant is deliberate: it makes the measurement exact (no DSP
 *     coincidence can pass for the axis being applied) and it is the same
 *     technique tests/data/vst3-test-instrument uses for its own MIDI-to-audio
 *     proof.
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
#include "MpeConsumerProbe.h"
#include "NotePlayHandle.h"
#include "Plugin.h"
#include "SampleFrame.h"

#include "embed.h"

#include <QDomDocument>
#include <QDomElement>

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

//! The three axes of testmpe::Axis, as an array bound.
constexpr int kAxisCount = static_cast<int>( testmpe::Axis::Count );

//! The multiplier the two consumed axes ask for: each is worth up to +100%.
float expressionGain( int pressure, int timbre )
{
	return ( 1.0f + static_cast<float>( pressure ) / MidiMaxControllerValue )
		* ( 1.0f + static_cast<float>( timbre ) / MidiMaxControllerValue );
}

//! The multiplier a MIDI note-off velocity asks for (Q050-039): +100% at full
//! scale, exactly 1.0 when the note has no release velocity of its own.
float releaseGain( int releaseVelocity )
{
	return 1.0f + static_cast<float>( releaseVelocity ) / MidiMaxControllerValue;
}

//! Q050-016: the pitch-bend multiplier. 1.0 at centre, +100% at the +48
//! semitone MPE member range (which is what kBaseLevel's own cents axis uses),
//! so a streamed pitch point is visible in the rendered level.
float pitchGain( int cents )
{
	return 1.0f + static_cast<float>( cents ) / MpeNoteExpression::MaxPitchCents;
}

class MpeTestConsumer final : public Instrument, public testmpe::ConsumerProbe
{
public:
	explicit MpeTestConsumer( InstrumentTrack* instrumentTrack ) :
		Instrument( instrumentTrack, &mpe_test_consumer_plugin_descriptor )
	{
	}

	//! Q050-039: a release phase long enough for the test to render a block of
	//! it after the note-off, so the release velocity can be measured. The
	//! sustained blocks the other assertions use are unaffected (their note has
	//! not been released, so no release frames are consumed).
	float desiredReleaseTimeMs() const override { return 20.f; }

	//! Called by the track for every MIDI event aimed at the instrument -
	//! from a playing note (InstrumentTrack::processOutEvent) and from live
	//! MIDI input alike. Realtime-safe: fixed arrays and int writes.
	bool handleMidiEvent( const MidiEvent& event, const TimePos& = TimePos(),
						f_cnt_t offset = 0 ) override
	{
		const int channel = std::clamp<int>( event.channel(), 0, kChannelCount - 1 );
		logEvent( event, offset );

		switch( event.type() )
		{
			case MidiNoteOn:
				// A note starts with its channel's axes neutral: whatever the
				// previous note left behind must not colour this block.
				m_pressure[channel] = 0;
				m_timbre[channel] = 0;
				m_release[channel] = 0;
				m_pitchCents[channel] = 0;
				for( int axis = 0; axis < kAxisCount; ++axis )
				{
					m_value[axis][channel] = 0;
					m_offset[axis][channel] = 0;
					m_count[axis][channel] = 0;
				}
				return true;

			case MidiNoteOff:
				// Q050-039: the note-off's velocity is release velocity. The
				// release phase is no longer expressive, so the two axes are
				// cleared and the block is coloured by this value instead.
				m_pressure[channel] = 0;
				m_timbre[channel] = 0;
				m_release[channel] = std::clamp<int>( event.velocity(), 0, MidiMaxControllerValue );
				return true;

			case MidiPitchBend:
				// The same cents the engine's frequency path applies (+-48
				// semitone MPE member range), computed here rather than through
				// MpeExpression::pitchCentsForBend() so the fixture depends on
				// no non-exported LMMS symbol at load time.
				{
					constexpr int kHalfSpan = 8192;
					const int bendOffset = std::clamp<int>( event.pitchBend(), 0, 2 * kHalfSpan - 1 )
						- kHalfSpan;
					const int cents = std::clamp<int>(
						bendOffset * MpeNoteExpression::MaxPitchCents / kHalfSpan,
						-MpeNoteExpression::MaxPitchCents, MpeNoteExpression::MaxPitchCents );
					m_pitchCents[channel] = cents;
					record( static_cast<int>( testmpe::Axis::Pitch ), channel, cents, offset );
				}
				return true;

			case MidiChannelPressure:
				m_pressure[channel] = std::clamp<int>( event.channelPressure(), 0, 127 );
				record( static_cast<int>( testmpe::Axis::Pressure ), channel,
					m_pressure[channel], offset );
				return true;

			case MidiControlChange:
				// CC74 is MPE's timbre ("Y") axis; every other controller is
				// not this instrument's business.
				if( event.controllerNumber() == MpeTimbreController )
				{
					m_timbre[channel] = std::clamp<int>( event.controllerValue(), 0, 127 );
					record( static_cast<int>( testmpe::Axis::Timbre ), channel,
						m_timbre[channel], offset );
					return true;
				}
				return false;

			default:
				return false;
		}
	}

	// ---- testmpe::ConsumerProbe ----
	int value( testmpe::Axis axis, int channel ) const override
	{
		return m_value[std::clamp<int>( static_cast<int>( axis ), 0, kAxisCount - 1 )]
			[std::clamp<int>( channel, 0, kChannelCount - 1 )];
	}

	std::uint64_t lastOffset( testmpe::Axis axis, int channel ) const override
	{
		return m_offset[std::clamp<int>( static_cast<int>( axis ), 0, kAxisCount - 1 )]
			[std::clamp<int>( channel, 0, kChannelCount - 1 )];
	}

	int eventCount( testmpe::Axis axis, int channel ) const override
	{
		return m_count[std::clamp<int>( static_cast<int>( axis ), 0, kAxisCount - 1 )]
			[std::clamp<int>( channel, 0, kChannelCount - 1 )];
	}

	//! Instrument (a SerializingObject) requires the three serialization
	//! entry points. A test subject has no state of its own to persist: the
	//! expression it consumes belongs to the NOTE and is saved with it
	//! (Note::saveSettings writes mpepitch/mpepressure/mpetimbre), so these
	//! are deliberately no-ops rather than a second store.
	QString nodeName() const override { return QStringLiteral( "mpetestconsumer" ); }
	//! The one exception (Q050-015): the order the instrument was handed note-ons, program changes and
	//! controllers in, as the 'events' attribute ("k60 p3 c32=127": a key, a program, a controller=value),
	//! so ArticulationPlaybackTest can read what an articulation sent ahead of its note. Read it from the
	//! test thread after the blocks are rendered; loadSettings() of an element with a 'clearlog' attribute starts a new log.
	void saveSettings( QDomDocument&, QDomElement& element ) override
	{
		QStringList events;
		for( int i = 0; i < m_loggedEvents; ++i )
		{
			const LoggedEvent& e = m_log[i];
			events << ( e.type == MidiControlChange ? QStringLiteral( "c%1=%2" ).arg( e.first ).arg( e.second )
				: e.type == MidiProgramChange ? QStringLiteral( "p%1" ).arg( e.first )
				: QStringLiteral( "k%1" ).arg( e.first ) );
		}
		element.setAttribute( QStringLiteral( "events" ), events.join( QLatin1Char( ' ' ) ) );
		// The frame offset in the period each logged event was handed in, and how many keys are down now.
		QStringList offsets;
		for( int i = 0; i < m_loggedEvents; ++i ) { offsets << QString::number( m_log[i].offset ); }
		element.setAttribute( QStringLiteral( "offsets" ), offsets.join( QLatin1Char( ' ' ) ) );
		element.setAttribute( QStringLiteral( "held" ), m_held );
	}
	void loadSettings( const QDomElement& element ) override
	{
		if( element.hasAttribute( QStringLiteral( "clearlog" ) ) ) { m_loggedEvents = 0; m_held = 0; }
	}

protected:
	void playNoteImpl( NotePlayHandle* nph, std::span<SampleFrame> out ) override
	{
		const int channel = std::clamp<int>( nph->midiChannel(), 0, kChannelCount - 1 );
		const float level = kBaseLevel * expressionGain( m_pressure[channel], m_timbre[channel] )
			* releaseGain( m_release[channel] ) * pitchGain( m_pitchCents[channel] );

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
	struct LoggedEvent { int type; int first; int second; int offset; };
	static constexpr int kLogCapacity = 64;

	//! Fixed storage and plain int writes, so the audio thread that calls handleMidiEvent allocates nothing.
	void logEvent( const MidiEvent& event, f_cnt_t offset )
	{
		if( event.type() == MidiNoteOn ) { ++m_held; }
		else if( event.type() == MidiNoteOff ) { --m_held; }
		if( m_loggedEvents >= kLogCapacity ) { return; }
		const int at = static_cast<int>( offset );
		switch( event.type() )
		{
			case MidiNoteOn: m_log[m_loggedEvents++] = { MidiNoteOn, event.key(), 0, at }; break;
			case MidiProgramChange: m_log[m_loggedEvents++] = { MidiProgramChange, event.program(), 0, at }; break;
			case MidiControlChange:
				m_log[m_loggedEvents++] = { MidiControlChange, event.controllerNumber(), event.controllerValue(), at };
				break;
			default: break;
		}
	}

	LoggedEvent m_log[kLogCapacity] = {};
	int m_loggedEvents = 0;
	int m_held = 0;  //!< note-ons minus note-offs
	//! Remember one event for the probe: its value, the period offset it
	//! arrived at, and a count. Fixed arrays, no allocation.
	void record( int axis, int channel, int value, f_cnt_t offset )
	{
		if( axis < 0 || axis >= kAxisCount ) { return; }
		m_value[axis][channel] = value;
		m_offset[axis][channel] = offset;
		++m_count[axis][channel];
	}

	int m_pressure[kChannelCount] = {};
	int m_timbre[kChannelCount] = {};
	//! The release velocity the last note-off on each channel carried (Q050-039).
	int m_release[kChannelCount] = {};
	int m_pitchCents[kChannelCount] = {};

	// What the probe reads back (axis x channel).
	int m_value[kAxisCount][kChannelCount] = {};
	std::uint64_t m_offset[kAxisCount][kChannelCount] = {};
	int m_count[kAxisCount][kChannelCount] = {};
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
