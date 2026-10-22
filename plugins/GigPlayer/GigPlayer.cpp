/*
 * GigPlayer.cpp - a GIG player using libgig (based on Sf2 player plugin)
 *
 * Copyright (c) 2008 Paul Giblock <drfaygo/at/gmail/dot/com>
 * Copyright (c) 2009-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * A few lines of code taken from LinuxSampler (also GPLv2) where noted:
 * Copyright (C) 2003,2004 by Benno Senoner and Christian Schoenebeck
 * Copyright (C) 2005-2008 Christian Schoenebeck
 * Copyright (C) 2009-2010 Christian Schoenebeck and Grigor Iliev
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
 *
 */

#include "GigPlayer.h"

#include <algorithm>
#include <cstring>
#include <QDebug>
#include <QLayout>
#include <QLabel>
#include <QDomDocument>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "SampleFrame.h"
#include "endian_handling.h"
#include "Engine.h"
#include "FileDialog.h"
#include "InstrumentTrack.h"
#include "InstrumentPlayHandle.h"
#include "Knob.h"
#include "MixHelpers.h"
#include "NotePlayHandle.h"
#include "PathUtil.h"
#include "Sample.h"
#include "Song.h"

#include "PatchesDialog.h"
#include "LcdSpinBox.h"

#include "embed.h"
#include "plugin_export.h"

// libgig 4.6 deprecates the sequential instrument/region getters in favour of the indexed API;
// the plugin keeps using the iterators, whose behaviour is unchanged, so silence that one warning.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace lmms
{

namespace
{

//! The hook that is called while a GIG file is opened, and its context.
std::atomic<GigInstrument::FileLoadHook> g_fileLoadHook { nullptr };
std::atomic<void*> g_fileLoadHookContext { nullptr };

} // namespace


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT gigplayer_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"GIG Player",
	QT_TRANSLATE_NOOP( "PluginBrowser", "Player for GIG files" ),
	"Garrett Wilson <g/at/floft/dot/net>",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader( "logo" ),
	"gig",
	nullptr,
} ;



/*! \brief Mix one resampled block of a sample into the instrument's output buffer.
 *
 * \p out is the buffer of the whole instrument for this period: EVERY sample of
 * EVERY note is mixed into it, so this only ADDS to what is already there.
 *
 * A resampler that made no progress - it consumed no input and produced no
 * output - contributes nothing at all. The frames it does not fill are left
 * exactly as they are, because the other samples have already mixed their
 * audio into them.
 *
 * \returns the number of frames mixed into \p out.
 *
 * This is exported so that the regression test
 * (tests/src/plugins/PluginPortsMigrationTest.cpp) can drive the no-progress
 * path with an injected resampler result. The render path in
 * GigInstrument::playImpl() calls exactly this function.
 */
f_cnt_t PLUGIN_EXPORT gigplayerMixResampledBlock(
		SampleFrame* out, f_cnt_t outOffset, f_cnt_t outFrames,
		const SampleFrame* mix, f_cnt_t inputFramesUsed, f_cnt_t outputFramesGenerated)
{
	if (inputFramesUsed == 0 && outputFramesGenerated == 0)
	{
		// This sample has nothing to contribute to the rest of the block.
		// The output buffer is SHARED with the other samples and notes, so
		// the frames this sample does not fill are left alone: clearing
		// them would erase the audio they have already mixed in.
		return 0;
	}

	const auto framesToMix = std::min(outputFramesGenerated, outFrames - outOffset);
	for (f_cnt_t i = 0; i < framesToMix; ++i)
	{
		out[outOffset + i] += mix[i];
	}

	return framesToMix;
}

} // extern "C"

GigInstrument::GigInstrument(InstrumentTrack* _instrument_track)
	: Instrument(_instrument_track, &gigplayer_plugin_descriptor, nullptr, Flag::IsSingleStreamed | Flag::IsNotBendable)
	, m_filename("")
	, m_bankNum(0, 0, 999, this, tr("Bank"))
	, m_patchNum(0, 0, 127, this, tr("Patch"))
	, m_gain(1.0f, 0.0f, 5.0f, 0.01f, this, tr("Gain"))
	, m_RandomSeed(0)
	, m_currentKeyDimension(0)
{
	auto iph = new InstrumentPlayHandle(this, _instrument_track);
	Engine::audioEngine()->addPlayHandle( iph );

	// The free lists are filled once, here, so that the audio thread only ever
	// hands out slots and voices that already exist.
	for( std::size_t i = 0; i < MaxGigNotes; ++i )
	{
		m_freeNotes[ i ] = static_cast<std::uint16_t>( MaxGigNotes - 1 - i );
	}
	m_freeNoteCount = static_cast<std::uint16_t>( MaxGigNotes );

	for( std::size_t i = 0; i < MaxGigVoices; ++i )
	{
		m_freeVoices[ i ] = static_cast<std::uint16_t>( MaxGigVoices - 1 - i );
	}
	m_freeVoiceCount = static_cast<std::uint16_t>( MaxGigVoices );

	updateSampleRate();

	connect( &m_bankNum, SIGNAL( dataChanged() ), this, SLOT( updatePatch() ) );
	connect( &m_patchNum, SIGNAL( dataChanged() ), this, SLOT( updatePatch() ) );
	connect( Engine::audioEngine(), SIGNAL( sampleRateChanged() ), this, SLOT( updateSampleRate() ) );
}




GigInstrument::~GigInstrument()
{
	Engine::audioEngine()->removePlayHandlesOfTypes( instrumentTrack(),
				PlayHandle::Type::NotePlayHandle
				| PlayHandle::Type::InstrumentPlayHandle );
	freeInstance();

	// The published state and the retired ones are deleted here: the
	// instrument is gone, so no audio thread can be rendering with them any
	// more.
	delete m_fileState.exchange( nullptr, std::memory_order_acq_rel );
	for( auto * state : m_retiredStates )
	{
		delete state;
	}
	m_retiredStates.clear();
}




void GigInstrument::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	_this.setAttribute( "src", m_filename );
	m_patchNum.saveSettings( _doc, _this, "patch" );
	m_bankNum.saveSettings( _doc, _this, "bank" );

	m_gain.saveSettings( _doc, _this, "gain" );
}




void GigInstrument::loadSettings( const QDomElement & _this )
{
	openFile( _this.attribute( "src" ), false );
	m_patchNum.loadSettings( _this, "patch" );
	m_bankNum.loadSettings( _this, "bank" );

	m_gain.loadSettings( _this, "gain" );

	updatePatch();
}




void GigInstrument::loadFile( const QString & _file )
{
	if( !_file.isEmpty() && QFileInfo( _file ).exists() )
	{
		openFile( _file, false );
		updatePatch();
		updateSampleRate();
	}
}




AutomatableModel * GigInstrument::childModel( const QString & _modelName )
{
	if( _modelName == "bank" )
	{
		return &m_bankNum;
	}
	else if( _modelName == "patch" )
	{
		return &m_patchNum;
	}

	qCritical() << "requested unknown model " << _modelName;

	return nullptr;
}




QString GigInstrument::nodeName() const
{
	return gigplayer_plugin_descriptor.name;
}




void GigInstrument::setFileLoadHook( FileLoadHook _hook, void* _context )
{
	g_fileLoadHookContext.store( _context, std::memory_order_release );
	g_fileLoadHook.store( _hook, std::memory_order_release );
}




extern "C" PLUGIN_EXPORT void lmms_gigplayer_set_file_load_hook( void (*hook)(void*), void* context )
{
	GigInstrument::setFileLoadHook( hook, context );
}




void GigInstrument::freeInstance()
{
	// Publish an empty state: the audio thread sees that no file is open and
	// drops its notes. The old state is retired, not deleted right here: an
	// audio thread may still be rendering with it.
	publishFileState( new GigFileState() );
}




// Publishes a new file state and retires the previous one. The swap is a
// single atomic store, so the audio thread never waits for the file that is
// being loaded.
void GigInstrument::publishFileState( GigFileState * _state )
{
	GigFileState * previous = m_fileState.exchange( _state, std::memory_order_seq_cst );
	if( previous != nullptr )
	{
		m_retiredStates.push_back( previous );
	}
	reapRetiredStates();
}




// Deletes the retired file states that no audio thread is reading. A state
// that is still in use stays in the list until the next call.
void GigInstrument::reapRetiredStates()
{
	if( m_fileReaders.load( std::memory_order_seq_cst ) != 0 )
	{
		return;
	}

	for( auto * state : m_retiredStates )
	{
		delete state;
	}
	m_retiredStates.clear();
}




void GigInstrument::openFile( const QString & _gigFile, bool updateTrackName )
{
	emit fileLoading();

	// The file is opened and the instrument is selected before anything is
	// published, so the audio thread never sees a half-loaded file and never
	// waits for the file to be read.
	GigFileState * state = new GigFileState();

	try
	{
		// The hook pauses the file preparation inside the block that the old
		// code protected with its mutex, so a test can pause the preparation
		// there and see that the audio thread goes on rendering.
		const FileLoadHook hook = g_fileLoadHook.load( std::memory_order_acquire );
		if( hook != nullptr )
		{
			hook( g_fileLoadHookContext.load( std::memory_order_acquire ) );
		}

		state->instance = new GigInstance( PathUtil::toAbsolute( _gigFile ) );
		m_filename = PathUtil::toShortestRelative( _gigFile );
	}
	catch( ... )
	{
		state->instance = nullptr;
		m_filename = "";
	}

	state->instrument.store( selectInstrument( state->instance ) );

	publishFileState( state );

	emit fileChanged();

	if( updateTrackName == true )
	{
		instrumentTrack()->setName(PathUtil::cleanName( _gigFile ) );
		updatePatch();
	}
}




void GigInstrument::updatePatch()
{
	if( m_bankNum.value() >= 0 && m_patchNum.value() >= 0 )
	{
		getInstrument();
	}
}




QString GigInstrument::getCurrentPatchName()
{
	// Take a reference on the published state, so that the file cannot be
	// deleted while it is read.
	m_fileReaders.fetch_add( 1, std::memory_order_seq_cst );
	GigFileState * state = m_fileState.load( std::memory_order_seq_cst );

	if( state == nullptr || state->instance == nullptr )
	{
		m_fileReaders.fetch_sub( 1, std::memory_order_seq_cst );
		return "";
	}

	int iBankSelected = m_bankNum.value();
	int iProgSelected = m_patchNum.value();

	gig::Instrument * pInstrument = state->instance->gig.GetFirstInstrument();

	while( pInstrument != nullptr )
	{
		int iBank = pInstrument->MIDIBank;
		int iProg = pInstrument->MIDIProgram;

		if( iBank == iBankSelected && iProg == iProgSelected )
		{
			QString name = QString::fromStdString( pInstrument->pInfo->Name );

			if( name == "" )
			{
				name = "<no name>";
			}

			m_fileReaders.fetch_sub( 1, std::memory_order_seq_cst );
			return name;
		}

		pInstrument = state->instance->gig.GetNextInstrument();
	}

	m_fileReaders.fetch_sub( 1, std::memory_order_seq_cst );
	return "";
}




// A key has been pressed
void GigInstrument::playNoteImpl(NotePlayHandle* _n, std::span<SampleFrame>)
{
	const float LOG440 = 2.643452676f;

	int midiNote = (int) floor( 12.0 * ( log2( _n->unpitchedFrequency() ) - LOG440 ) - 4.0 );

	// out of range?
	if( midiNote < 0 || midiNote >= 128 )
	{
		return;
	}

	if (!_n->m_pluginData)
	{
		const std::uint16_t slot = acquireNoteSlot();
		if( slot == MaxGigNotes )
		{
			// Every note slot is in use. The note is not accepted at all, so
			// that the engine does not wait for a note that never plays.
			return;
		}

		_n->m_pluginData = _n;

		const int baseVelocity = instrumentTrack()->midiPort()->baseVelocity();
		const uint velocity = _n->midiVelocity( baseVelocity );

		GigNoteSlot & noteSlot = m_noteSlots[ slot ];
		noteSlot.note = GigNote( midiNote, velocity, _n->unpitchedFrequency() );
		noteSlot.voiceCount = 0;
		noteSlot.active = true;
		noteSlot.fileState = m_fileState.load( std::memory_order_acquire );
		noteSlot.handle.store( _n, std::memory_order_release );
		m_activeNotes.fetch_add( 1, std::memory_order_release );
	}
}




// Takes a note slot out of the pool. Returns MaxGigNotes when the pool is
// empty.
std::uint16_t GigInstrument::acquireNoteSlot()
{
	if( m_freeNoteCount == 0 )
	{
		return MaxGigNotes;
	}

	const std::uint16_t slot = m_freeNotes[ --m_freeNoteCount ];
	GigNoteSlot & noteSlot = m_noteSlots[ slot ];
	noteSlot.generation.fetch_add( 1, std::memory_order_acq_rel );
	noteSlot.releaseGeneration.store( 0, std::memory_order_release );

	m_noteOrder[ m_noteOrderCount++ ] = slot;

	return slot;
}




// Gives a note slot back to the pool and drops the voices it was playing.
void GigInstrument::releaseNoteSlot( std::uint16_t _slot )
{
	GigNoteSlot & noteSlot = m_noteSlots[ _slot ];

	for( std::uint8_t v = 0; v < noteSlot.voiceCount; ++v )
	{
		releaseVoice( noteSlot.voices[ v ] );
	}
	noteSlot.voiceCount = 0;
	noteSlot.active = false;
	noteSlot.handle.store( nullptr, std::memory_order_release );
	m_activeNotes.fetch_sub( 1, std::memory_order_acq_rel );

	for( std::uint16_t i = 0; i < m_noteOrderCount; ++i )
	{
		if( m_noteOrder[ i ] == _slot )
		{
			for( std::uint16_t k = i; k + 1 < m_noteOrderCount; ++k )
			{
				m_noteOrder[ k ] = m_noteOrder[ k + 1 ];
			}
			--m_noteOrderCount;
			break;
		}
	}

	m_freeNotes[ m_freeNoteCount++ ] = _slot;
}




// Takes a voice out of the pool. Returns MaxGigVoices when the pool is empty.
std::uint16_t GigInstrument::acquireVoice()
{
	if( m_freeVoiceCount == 0 )
	{
		return MaxGigVoices;
	}

	return m_freeVoices[ --m_freeVoiceCount ];
}




// Gives a voice back to the pool.
void GigInstrument::releaseVoice( std::uint16_t _voice )
{
	m_freeVoices[ m_freeVoiceCount++ ] = _voice;
}




// Drops every note and gives the voices back to the voice pool. This is
// called on the audio thread only.
void GigInstrument::clearNotes()
{
	while( m_noteOrderCount > 0 )
	{
		releaseNoteSlot( m_noteOrder[ m_noteOrderCount - 1 ] );
	}
}




// Process the notes and output a certain number of frames (e.g. 256, set in
// the preferences)
void GigInstrument::playImpl(std::span<SampleFrame> out)
{
	const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
	const auto rate = Engine::audioEngine()->outputSampleRate();

	// Initialize to zeros
	zeroSampleFrames(out.data(), out.size());

	// Take a reference on the published file state, so that the file cannot be
	// deleted while it is read. This never waits: the state is published by
	// swapping a pointer, and the file it owns is only deleted when no audio
	// thread is inside a render.
	m_fileReaders.fetch_add( 1, std::memory_order_seq_cst );
	GigFileState * state = m_fileState.load( std::memory_order_seq_cst );

	if( state == nullptr )
	{
		m_fileReaders.fetch_sub( 1, std::memory_order_seq_cst );
		return;
	}

	// The notes belong to the file that was open when they were started, so
	// they are dropped when another file is published.
	if( state != m_activeState )
	{
		m_activeState = state;
	}

	gig::Instrument * instrument = state->instrument.load( std::memory_order_acquire );
	if( state->instance == nullptr || instrument == nullptr )
	{
		m_fileReaders.fetch_sub( 1, std::memory_order_seq_cst );
		return;
	}

	// Another thread may have asked for the notes to be dropped
	if( m_clearNotes.exchange( false, std::memory_order_acquire ) )
	{
		clearNotes();
	}

	// The notes are processed in the order they were started. A note that is
	// removed shifts the ones behind it, so the index is handled by hand.
	std::uint16_t n = 0;
	while( n < m_noteOrderCount )
	{
		const std::uint16_t slotIndex = m_noteOrder[ n ];
		GigNoteSlot & slot = m_noteSlots[ slotIndex ];
		if( !slot.active )
		{
			++n;
			continue;
		}
		GigNote & note = slot.note;

		// A note that was started in a file that is not open any more is
		// dropped: its samples belong to that file.
		if( slot.fileState != state )
		{
			releaseNoteSlot( slotIndex );
			continue;
		}

		// A release that another thread asked for
		const std::uint32_t requested = slot.releaseGeneration.load( std::memory_order_acquire );
		if( requested != 0 && requested == slot.generation.load( std::memory_order_acquire ) )
		{
			slot.releaseGeneration.store( 0, std::memory_order_release );

			// Mark the note as being released, but only if it was playing or
			// was just pressed (i.e. not if the key was already released)
			if( note.state == GigState::KeyDown || note.state == GigState::PlayingKeyDown )
			{
				note.state = GigState::KeyUp;
			}
		}

		// Process notes in the KeyUp state, adding release samples if desired
		if( note.state == GigState::KeyUp )
		{
			// If there are no samples, we're done
			if( slot.voiceCount == 0 )
			{
				note.state = GigState::Completed;
			}
			else
			{
				note.state = GigState::PlayingKeyUp;

				// Notify each sample that the key has been released
				for( std::uint8_t v = 0; v < slot.voiceCount; ++v )
				{
					m_voices[ slot.voices[ v ] ].adsr.keyup();
				}

				// Add release samples if available
				if( note.release == true )
				{
					addSamples( slot, true, instrument );
				}
			}
		}
		// Process notes in the KeyDown state, adding samples for the notes
		else if( note.state == GigState::KeyDown )
		{
			note.state = GigState::PlayingKeyDown;
			addSamples( slot, false, instrument );
		}

		// Delete ended samples
		std::uint8_t v = 0;
		while( v < slot.voiceCount )
		{
			GigSample & sample = m_voices[ slot.voices[ v ] ];

			// Delete if the ADSR for a sample is complete for normal
			// notes, or if a release sample, then if we've reached
			// the end of the sample. The position counts frames already
			// read, so every frame must be rendered before the sample is
			// removed; a one-frame release sample starts at position zero.
			if( sample.sample == nullptr || sample.adsr.done() ||
				( note.isRelease == true &&
				  sample.pos >= sample.sample->SamplesTotal ) )
			{
				releaseVoice( slot.voices[ v ] );
				for( std::uint8_t k = v; k + 1 < slot.voiceCount; ++k )
				{
					slot.voices[ k ] = slot.voices[ k + 1 ];
				}
				--slot.voiceCount;

				if( v >= slot.voiceCount )
				{
					break;
				}
				++v;
			}
			else
			{
				++v;
			}
		}

		// Delete ended notes (either in the completed state or all the samples ended)
		if( note.state == GigState::Completed || slot.voiceCount == 0 )
		{
			// The note behind this one takes its place, and the note that was
			// removed is not looked at again in this period.
			releaseNoteSlot( slotIndex );
			++n;
			continue;
		}

		++n;
	}

	// Fill buffer with portions of the note samples
	for( std::uint16_t n = 0; n < m_noteOrderCount; ++n )
	{
		GigNoteSlot & slot = m_noteSlots[ m_noteOrder[ n ] ];
		if( !slot.active || slot.fileState != state )
		{
			continue;
		}
		GigNote & note = slot.note;

		// Only process the notes if we're in a playing state
		if (!(note.state == GigState::PlayingKeyDown || note.state == GigState::PlayingKeyUp ))
		{
			continue;
		}

		for( std::uint8_t v = 0; v < slot.voiceCount; ++v )
		{
			GigSample & sample = m_voices[ slot.voices[ v ] ];
			if (sample.sample == nullptr || sample.region == nullptr) { continue; }

			float freq_factor = 1.0; // How much to resample

			// Resample to be the correct pitch when the sample provided isn't
			// solely for this one note (e.g. one or two samples per octave) or
			// we are processing at a different sample rate
			if (sample.region->PitchTrack == true || rate != sample.sample->SamplesPerSecond)
			{
				// Factor just for resampling
				freq_factor = 1.0 * rate / sample.sample->SamplesPerSecond;

				// Factor for pitch shifting as well as resampling
				if (sample.region->PitchTrack == true) { freq_factor *= sample.freqFactor; }
			}

			// Apply ADSR using a copy so if we don't use these samples when
			// resampling, the ADSR doesn't get messed up
			ADSR copy = sample.adsr;

			sample.m_resampler.setRatio(freq_factor);

			// TODO: These kind of playback pipelines/graphs are repeated within other parts of the codebase that work
			// with audio samples. We should find a way to unify this but the right abstraction is not so clear yet.
			auto framesMixed = f_cnt_t{0};
			while (framesMixed < frames)
			{
				if (sample.m_sourceBufferView.empty())
				{
					loadSample(sample, sample.m_sourceBuffer.data(), sample.m_sourceBuffer.size());

					for (auto& frame : sample.m_sourceBuffer)
					{
						frame *= copy.value();
					}

					sample.pos += sample.m_sourceBuffer.size();
					sample.adsr.inc(sample.m_sourceBuffer.size());
					sample.m_sourceBufferView = sample.m_sourceBuffer;
				}

				if (sample.m_mixBufferView.empty()) { sample.m_mixBufferView = sample.m_mixBuffer; }

				const auto [inputFramesUsed, outputFramesGenerated] = sample.m_resampler.process(
					{&sample.m_sourceBufferView.data()[0][0], 2, sample.m_sourceBufferView.size()},
					{&sample.m_mixBufferView.data()[0][0], 2, sample.m_mixBufferView.size()});

				// The output buffer is shared by every sample of every note, so
				// mixing only adds to it. A resampler that made no progress
				// contributes nothing and leaves the rest of the block alone.
				const auto framesToMix = gigplayerMixResampledBlock(out.data(), framesMixed, frames,
						sample.m_mixBufferView.data(), inputFramesUsed, outputFramesGenerated);

				if (inputFramesUsed == 0 && outputFramesGenerated == 0)
				{
					break;
				}

				sample.m_sourceBufferView = sample.m_sourceBufferView.subspan(inputFramesUsed);
				sample.m_mixBufferView = sample.m_mixBufferView.subspan(framesToMix);
				framesMixed += framesToMix;
			}
		}
	}

	m_fileReaders.fetch_sub( 1, std::memory_order_seq_cst );

	// Set gain properly based on volume control
	for( f_cnt_t i = 0; i < frames; ++i )
	{
		out[i][0] *= m_gain.value();
		out[i][1] *= m_gain.value();
	}
}




namespace
{

bool readSampleSegment(gig::Sample& sample, std::int8_t* destination, f_cnt_t sourcePosition,
		f_cnt_t frames, bool reverse)
{
	const auto frameSize = sample.FrameSize;
	const auto readStart = reverse ? sourcePosition - frames + 1 : sourcePosition;
	sample.SetPos(readStart);
	const long samplesRead = sample.Read(destination, static_cast<long>(frames));
	const f_cnt_t framesRead = samplesRead > 0 ? static_cast<f_cnt_t>(samplesRead) : 0;

	if (reverse)
	{
		for (f_cnt_t left = 0; left < framesRead / 2; ++left)
		{
			const f_cnt_t right = framesRead - left - 1;
			for (unsigned long byte = 0; byte < frameSize; ++byte)
			{
				std::swap(destination[left * frameSize + byte], destination[right * frameSize + byte]);
			}
		}
	}

	if (framesRead < frames)
	{
		std::memset(destination + framesRead * frameSize, 0, (frames - framesRead) * frameSize);
	}
	return framesRead == frames;
}

bool repeatSampleFrame(gig::Sample& sample, std::int8_t* destination,
		f_cnt_t sourcePosition, f_cnt_t frames)
{
	const auto frameSize = sample.FrameSize;
	if (!readSampleSegment(sample, destination, sourcePosition, 1, false))
	{
		std::memset(destination + frameSize, 0, (frames - 1) * frameSize);
		return false;
	}

	for (f_cnt_t frame = 1; frame < frames; ++frame)
	{
		std::memcpy(destination + frame * frameSize, destination, frameSize);
	}
	return true;
}

}



void GigInstrument::loadBidirectionalSample(gig::Sample& sample, std::int8_t* buffer,
		f_cnt_t position, f_cnt_t samples, f_cnt_t loopStart, f_cnt_t loopEnd)
{
	const f_cnt_t loopLength = loopEnd - loopStart;
	const f_cnt_t halfPeriod = loopLength > 0 ? loopLength - 1 : 0;
	const f_cnt_t fullPeriod = halfPeriod * 2;
	f_cnt_t totalRead = 0;

	while (totalRead < samples)
	{
		const f_cnt_t currentPosition = position + totalRead;
		const f_cnt_t remaining = samples - totalRead;
		f_cnt_t sourcePosition = currentPosition;
		f_cnt_t segmentLength = remaining;
		bool reverse = false;

		if (currentPosition < loopStart)
		{
			segmentLength = std::min(remaining, loopStart - currentPosition);
		}
		else if (currentPosition < loopEnd)
		{
			segmentLength = std::min(remaining, loopEnd - currentPosition);
		}
		else if (halfPeriod == 0)
		{
			std::int8_t* destination = buffer + totalRead * sample.FrameSize;
			if (!repeatSampleFrame(sample, destination, loopStart, remaining))
			{
				break;
			}
			totalRead += remaining;
			continue;
		}
		else
		{
			const f_cnt_t loopPosition = (currentPosition - loopEnd) % fullPeriod;
			sourcePosition = getPingPongIndex(currentPosition, loopStart, loopEnd);
			if (loopPosition < halfPeriod)
			{
				segmentLength = std::min(remaining, halfPeriod - loopPosition);
				reverse = true;
			}
			else
			{
				segmentLength = std::min(remaining, fullPeriod - loopPosition);
			}
		}

		std::int8_t* destination = buffer + totalRead * sample.FrameSize;
		if (!readSampleSegment(sample, destination, sourcePosition, segmentLength, reverse))
		{
			const f_cnt_t tailFrames = remaining - segmentLength;
			std::memset(destination + segmentLength * sample.FrameSize, 0,
				tailFrames * sample.FrameSize);
			break;
		}
		totalRead += segmentLength;
	}
}



void GigInstrument::loadSample( GigSample& sample, SampleFrame* sampleData, f_cnt_t samples )
{
	if( sampleData == nullptr || samples < 1 )
	{
		return;
	}

	// Determine if we need to loop part of this sample
	bool loop = false;
	gig::loop_type_t loopType = gig::loop_type_normal;
	f_cnt_t loopStart = 0;
	f_cnt_t loopLength = 0;

	if( sample.region->pSampleLoops != nullptr )
	{
		for( uint32_t i = 0; i < sample.region->SampleLoops; ++i )
		{
			loop = true;
			loopType = static_cast<gig::loop_type_t>( sample.region->pSampleLoops[i].LoopType );
			loopStart = sample.region->pSampleLoops[i].LoopStart;
			loopLength = sample.region->pSampleLoops[i].LoopLength;

			// Currently only support at max one loop
			break;
		}
	}

	unsigned long allocationsize = samples * sample.sample->FrameSize;
	int8_t buffer[allocationsize];

	// Load the sample in different ways depending on if we're looping or not
	if( loop == true && ( sample.pos >= loopStart || sample.pos + samples > loopStart ) )
	{
		if( loopType == gig::loop_type_bidirectional )
		{
			loadBidirectionalSample(*sample.sample, buffer, sample.pos, samples,
				loopStart, loopStart + loopLength);
		}
		else
		{
			// Calculate the new position based on the type of forward loop
			sample.pos = getLoopedIndex( sample.pos, loopStart, loopStart + loopLength );
			// TODO: also implement loop_type_backward support
			sample.sample->SetPos( sample.pos );

			// Load samples around the exclusive end, wrapping to the loop start.
			long samplestoread = samples;
			long samplestoloopend = 0;
			long readsamples = 0;
			long totalreadsamples = 0;
			long loopEnd = loopStart + loopLength;

			do
			{
				samplestoloopend = loopEnd - sample.sample->GetPos();
				readsamples = sample.sample->Read( &buffer[totalreadsamples * sample.sample->FrameSize],
						std::min( samplestoread, samplestoloopend ) );
				samplestoread -= readsamples;
				totalreadsamples += readsamples;

				if( readsamples >= samplestoloopend )
				{
					sample.sample->SetPos( loopStart );
				}
			}
			while( samplestoread > 0 && readsamples > 0 );
		}
	}
	else
	{
		sample.sample->SetPos( sample.pos );

		unsigned long size = sample.sample->Read( &buffer, samples ) * sample.sample->FrameSize;
		std::memset( (int8_t*) &buffer + size, 0, allocationsize - size );
	}

	// Convert from 16 or 24 bit into 32-bit float
	if( sample.sample->BitDepth == 24 ) // 24 bit
	{
		auto pInt = reinterpret_cast<uint8_t*>(&buffer);

		for( f_cnt_t i = 0; i < samples; ++i )
		{
			// libgig gives 24-bit data as little endian, so we must
			// convert if on a big endian system
			int32_t valueLeft = swap32IfBE(
						( pInt[ 3 * sample.sample->Channels * i ] << 8 ) |
						( pInt[ 3 * sample.sample->Channels * i + 1 ] << 16 ) |
						( pInt[ 3 * sample.sample->Channels * i + 2 ] << 24 ) );

			// Store the notes to this buffer before saving to output
			// so we can fade them out as needed
			sampleData[i][0] = 1.0 / 0x80000000 * sample.attenuation * valueLeft;

			if( sample.sample->Channels == 1 )
			{
				sampleData[i][1] = sampleData[i][0];
			}
			else
			{
				int32_t valueRight = swap32IfBE(
							( pInt[ 3 * sample.sample->Channels * i + 3 ] << 8 ) |
							( pInt[ 3 * sample.sample->Channels * i + 4 ] << 16 ) |
							( pInt[ 3 * sample.sample->Channels * i + 5 ] << 24 ) );

				sampleData[i][1] = 1.0 / 0x80000000 * sample.attenuation * valueRight;
			}
		}
	}
	else // 16 bit
	{
		auto pInt = reinterpret_cast<int16_t*>(&buffer);

		for( f_cnt_t i = 0; i < samples; ++i )
		{
			sampleData[i][0] = 1.0 / 0x8000 *
				pInt[ sample.sample->Channels * i ] * sample.attenuation;

			if( sample.sample->Channels == 1 )
			{
				sampleData[i][1] = sampleData[i][0];
			}
			else
			{
				sampleData[i][1] = 1.0 / 0x8000 *
					pInt[ sample.sample->Channels * i + 1 ] * sample.attenuation;
			}
		}
	}
}




// These two loop index functions taken from SampleBuffer.cpp
f_cnt_t GigInstrument::getLoopedIndex( f_cnt_t index, f_cnt_t startf, f_cnt_t endf ) const
{
	if( index < endf )
	{
		return index;
	}

	return startf + ( index - startf )
				% ( endf - startf );
}




f_cnt_t GigInstrument::getPingPongIndex( f_cnt_t index, f_cnt_t startf, f_cnt_t endf ) const
{
	if( index < endf )
	{
		return index;
	}

	const f_cnt_t looplen = endf - startf;
	if (looplen <= 1)
	{
		return startf;
	}
	const f_cnt_t halfPeriod = looplen - 1;
	const f_cnt_t looppos = ( index - endf ) % ( halfPeriod * 2 );

	return ( looppos < halfPeriod )
		? endf - 2 - looppos
		: startf + 1 + ( looppos - halfPeriod );
}




// A key has been released
void GigInstrument::deleteNotePluginData( NotePlayHandle * _n )
{
	// The notes belong to the audio thread, so the release is only requested
	// here: a single atomic store per note slot, which neither blocks nor
	// allocates. The audio thread picks the request up in the next period.
	for( auto & slot : m_noteSlots )
	{
		const std::uint32_t generation = slot.generation.load( std::memory_order_acquire );
		if( slot.handle.load( std::memory_order_acquire ) == _n )
		{
			slot.releaseGeneration.store( generation, std::memory_order_release );
			break;
		}
	}

	_n->m_pluginData = nullptr;
}




gui::PluginView* GigInstrument::instantiateView( QWidget * _parent )
{
	return new gui::GigInstrumentView( this, _parent );
}




// Add the desired samples (either the normal samples or the release samples)
// to the GigNote
//
// Note: not thread safe since libgig stores current region position data in
// the instrument object
void GigInstrument::addSamples( GigNoteSlot & _slot, bool wantReleaseSample,
		gig::Instrument * _instrument )
{
	GigNote & gignote = _slot.note;

	// Change key dimension, e.g. change samples based on what key is pressed
	// in a certain range. From LinuxSampler
	if( gignote.midiNote >= _instrument->DimensionKeyRange.low &&
			gignote.midiNote <= _instrument->DimensionKeyRange.high )
	{
		m_currentKeyDimension = float( gignote.midiNote -
				_instrument->DimensionKeyRange.low ) / (
					_instrument->DimensionKeyRange.high -
					_instrument->DimensionKeyRange.low + 1 );
	}

	gig::Region* pRegion = _instrument->GetFirstRegion();

	while( pRegion != nullptr )
	{
		Dimension dim = getDimensions( pRegion, gignote.velocity, wantReleaseSample );
		gig::DimensionRegion * pDimRegion = pRegion->GetDimensionRegionByValue( dim.DimValues );
		gig::Sample * pSample = pDimRegion->pSample;

		// If this is a release sample, the note won't ever be
		// released, so we handle it differently
		gignote.isRelease = wantReleaseSample;

		// Does this note have release samples? Set this only on the original
		// notes and not when we get the release samples.
		if( wantReleaseSample != true )
		{
			gignote.release = gignote.release || dim.release;
		}

		if( pSample != nullptr && pSample->SamplesTotal != 0 )
		{
			int keyLow = pRegion->KeyRange.low;
			int keyHigh = pRegion->KeyRange.high;

			if( gignote.midiNote >= keyLow && gignote.midiNote <= keyHigh )
			{
				float attenuation = pDimRegion->GetVelocityAttenuation( gignote.velocity );
				float length = (float) pSample->SamplesTotal / Engine::audioEngine()->outputSampleRate();

				// TODO: sample panning? crossfade different layers?

				if( wantReleaseSample == true )
				{
					// From LinuxSampler, not sure how it was created
					attenuation *= 1 - 0.01053 * ( 256 >> pDimRegion->ReleaseTriggerDecay ) * length;
				}
				else
				{
					attenuation *= pDimRegion->SampleAttenuation;
				}

				if( _slot.voiceCount < MaxGigSamplesPerNote )
				{
					const std::uint16_t voice = acquireVoice();
					if( voice != MaxGigVoices )
					{
						// The voice was built before the audio thread started, so
						// attaching a sample to it neither allocates nor frees.
						m_voices[ voice ].attach( pSample, pDimRegion, attenuation, gignote.frequency );
						_slot.voices[ _slot.voiceCount++ ] = voice;
					}
				}			}
		}

		pRegion = _instrument->GetNextRegion();
	}
}




// Based on our input parameters, generate a "dimension" that specifies which
// note we wish to select from the GIG file with libgig. libgig will use this
// information to select the sample.
Dimension GigInstrument::getDimensions( gig::Region * pRegion, int velocity, bool release )
{
	Dimension dim;

	if( pRegion == nullptr )
	{
		return dim;
	}

	for( int i = pRegion->Dimensions - 1; i >= 0; --i )
	{
		switch( pRegion->pDimensionDefinitions[i].dimension )
		{
			case gig::dimension_layer:
				// TODO: implement this
				dim.DimValues[i] = 0;
				break;
			case gig::dimension_velocity:
				dim.DimValues[i] = velocity;
				break;
			case gig::dimension_releasetrigger:
				dim.release = true;
				dim.DimValues[i] = (uint) release;
				break;
			case gig::dimension_keyboard:
				dim.DimValues[i] = (uint) ( m_currentKeyDimension * pRegion->pDimensionDefinitions[i].zones );
				break;
			case gig::dimension_roundrobin:
			case gig::dimension_roundrobinkeyboard:
				// TODO: implement this
				dim.DimValues[i] = 0;
				break;
			case gig::dimension_random:
				// From LinuxSampler, untested
				m_RandomSeed = m_RandomSeed * 1103515245 + 12345;
				dim.DimValues[i] = uint(
					m_RandomSeed / 4294967296.0f * pRegion->pDimensionDefinitions[i].bits );
				break;
			case gig::dimension_samplechannel:
			case gig::dimension_channelaftertouch:
			case gig::dimension_modwheel:
			case gig::dimension_breath:
			case gig::dimension_foot:
			case gig::dimension_portamentotime:
			case gig::dimension_effect1:
			case gig::dimension_effect2:
			case gig::dimension_genpurpose1:
			case gig::dimension_genpurpose2:
			case gig::dimension_genpurpose3:
			case gig::dimension_genpurpose4:
			case gig::dimension_sustainpedal:
			case gig::dimension_portamento:
			case gig::dimension_sostenutopedal:
			case gig::dimension_softpedal:
			case gig::dimension_genpurpose5:
			case gig::dimension_genpurpose6:
			case gig::dimension_genpurpose7:
			case gig::dimension_genpurpose8:
			case gig::dimension_effect1depth:
			case gig::dimension_effect2depth:
			case gig::dimension_effect3depth:
			case gig::dimension_effect4depth:
			case gig::dimension_effect5depth:
			case gig::dimension_none:
			default:
				dim.DimValues[i] = 0;
				break;
		}
	}

	return dim;
}




// Get the selected instrument from the GIG file we opened if we haven't gotten
// it already. This is based on the bank and patch numbers.
gig::Instrument * GigInstrument::selectInstrument( GigInstance * _instance ) const
{
	if( _instance == nullptr )
	{
		return nullptr;
	}

	const int iBankSelected = m_bankNum.value();
	const int iProgSelected = m_patchNum.value();

	gig::Instrument * pInstrument = _instance->gig.GetFirstInstrument();

	while( pInstrument != nullptr )
	{
		const int iBank = pInstrument->MIDIBank;
		const int iProg = pInstrument->MIDIProgram;

		if( iBank == iBankSelected && iProg == iProgSelected )
		{
			break;
		}

		pInstrument = _instance->gig.GetNextInstrument();
	}

	return pInstrument;
}




void GigInstrument::getInstrument()
{
	// Take a reference on the published state, so that the file cannot be
	// deleted while the instrument is selected in it.
	m_fileReaders.fetch_add( 1, std::memory_order_seq_cst );
	GigFileState * state = m_fileState.load( std::memory_order_seq_cst );

	if( state != nullptr )
	{
		state->instrument.store( selectInstrument( state->instance ) );
	}

	m_fileReaders.fetch_sub( 1, std::memory_order_seq_cst );
}




// Since the sample rate changes when we start an export, clear all the
// currently-playing notes when we get this signal. Then, the export won't
// include leftover notes that were playing in the program.
//
// The notes belong to the audio thread, so this only asks it to drop them.
void GigInstrument::updateSampleRate()
{
	// Asking the audio thread to drop the notes it plays is only worth it
	// when there are notes.
	if( m_activeNotes.load( std::memory_order_acquire ) > 0 )
	{
		m_clearNotes.store( true, std::memory_order_release );
	}
}



namespace gui
{


class gigKnob : public Knob
{
public:
	gigKnob( QWidget * _parent ) :
			Knob( KnobType::Bright26, _parent )
	{
		setFixedSize( 31, 38 );
	}
} ;




GigInstrumentView::GigInstrumentView( Instrument * _instrument, QWidget * _parent ) :
        InstrumentViewFixedSize( _instrument, _parent )
{
	auto k = castModel<GigInstrument>();

	connect( &k->m_bankNum, SIGNAL( dataChanged() ), this, SLOT( updatePatchName() ) );
	connect( &k->m_patchNum, SIGNAL( dataChanged() ), this, SLOT( updatePatchName() ) );

	// File Button
	m_fileDialogButton = new PixmapButton( this );
	m_fileDialogButton->setCursor(Qt::PointingHandCursor);
	m_fileDialogButton->setActiveGraphic( PLUGIN_NAME::getIconPixmap( "fileselect_on" ) );
	m_fileDialogButton->setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "fileselect_off" ) );
	m_fileDialogButton->move( 223, 68 );

	connect( m_fileDialogButton, SIGNAL( clicked() ), this, SLOT( showFileDialog() ) );

	m_fileDialogButton->setToolTip(tr("Open GIG file"));

	// Patch Button
	m_patchDialogButton = new PixmapButton( this );
	m_patchDialogButton->setCursor(Qt::PointingHandCursor);
	m_patchDialogButton->setActiveGraphic( PLUGIN_NAME::getIconPixmap( "patches_on" ) );
	m_patchDialogButton->setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "patches_off" ) );
	m_patchDialogButton->setEnabled( false );
	m_patchDialogButton->move( 223, 94 );

	connect( m_patchDialogButton, SIGNAL( clicked() ), this, SLOT( showPatchDialog() ) );

	m_patchDialogButton->setToolTip(tr("Choose patch"));

	// LCDs
	m_bankNumLcd = new LcdSpinBox( 3, "21pink", this );
	m_bankNumLcd->move( 111, 150 );

	m_patchNumLcd = new LcdSpinBox( 3, "21pink", this );
	m_patchNumLcd->move( 161, 150 );

	// Next row
	m_filenameLabel = new QLabel( this );
	m_filenameLabel->setGeometry( 61, 70, 156, 14 );
	m_patchLabel = new QLabel( this );
	m_patchLabel->setGeometry( 61, 94, 156, 14 );

	// Gain
	m_gainKnob = new gigKnob( this );
	m_gainKnob->setHintText( tr( "Gain:" ) + " ", "" );
	m_gainKnob->move( 32, 140 );

	setAutoFillBackground( true );
	QPalette pal;
	pal.setBrush( backgroundRole(), PLUGIN_NAME::getIconPixmap( "artwork" ) );
	setPalette( pal );

	updateFilename();
}




void GigInstrumentView::modelChanged()
{
	auto k = castModel<GigInstrument>();
	m_bankNumLcd->setModel( &k->m_bankNum );
	m_patchNumLcd->setModel( &k->m_patchNum );

	m_gainKnob->setModel( &k->m_gain );

	connect( k, SIGNAL( fileChanged() ), this, SLOT( updateFilename() ) );
	connect( k, SIGNAL( fileLoading() ), this, SLOT( invalidateFile() ) );

	updateFilename();
}




void GigInstrumentView::updateFilename()
{
	auto i = castModel<GigInstrument>();
	QFontMetrics fm( m_filenameLabel->font() );
	QString file = i->m_filename.endsWith( ".gig", Qt::CaseInsensitive ) ?
			i->m_filename.left( i->m_filename.length() - 4 ) :
			i->m_filename;
	m_filenameLabel->setText( fm.elidedText( file, Qt::ElideLeft, m_filenameLabel->width() ) );

	m_patchDialogButton->setEnabled( !i->m_filename.isEmpty() );

	updatePatchName();
	update();
}




void GigInstrumentView::updatePatchName()
{
	auto i = castModel<GigInstrument>();
	QFontMetrics fm( font() );
	QString patch = i->getCurrentPatchName();
	m_patchLabel->setText( fm.elidedText( patch, Qt::ElideLeft, m_patchLabel->width() ) );

	update();
}




void GigInstrumentView::invalidateFile()
{
	m_patchDialogButton->setEnabled( false );
}




void GigInstrumentView::showFileDialog()
{
	auto k = castModel<GigInstrument>();

	FileDialog ofd( nullptr, tr( "Open GIG file" ) );
	ofd.setFileMode( FileDialog::ExistingFiles );

	QStringList types;
	types << tr( "GIG Files (*.gig)" );
	ofd.setNameFilters( types );

	if( k->m_filename != "" )
	{
		QString f = PathUtil::toAbsolute( k->m_filename );
		ofd.setDirectory( QFileInfo( f ).absolutePath() );
		ofd.selectFile( QFileInfo( f ).fileName() );
	}
	else
	{
		ofd.setDirectory( ConfigManager::inst()->gigDir() );
	}

	m_fileDialogButton->setEnabled( false );

	if( ofd.exec() == QDialog::Accepted && !ofd.selectedFiles().isEmpty() )
	{
		QString f = ofd.selectedFiles()[0];

		if( f != "" )
		{
			k->openFile( f );
			Engine::getSong()->setModified();
		}
	}

	m_fileDialogButton->setEnabled( true );
}




void GigInstrumentView::showPatchDialog()
{
	auto k = castModel<GigInstrument>();
	PatchesDialog pd( this );

	// Take a reference on the published state, so that the file cannot be
	// deleted while the dialog is open.
	k->m_fileReaders.fetch_add( 1, std::memory_order_seq_cst );
	GigFileState * state = k->m_fileState.load( std::memory_order_seq_cst );
	GigInstance * instance = state != nullptr ? state->instance : nullptr;

	pd.setup( instance, 1, k->instrumentTrack()->name(), &k->m_bankNum, &k->m_patchNum, m_patchLabel );
	pd.exec();

	k->m_fileReaders.fetch_sub( 1, std::memory_order_seq_cst );
}


} // namespace gui


// Store information related to playing a sample from the GIG file
//
// A voice is built before the audio thread starts, so that attaching a
// sample to it on the audio thread neither allocates nor frees. The
// resampler and the two frame buffers belong to the voice.
GigSample::GigSample()
	: sample(nullptr)
	, region(nullptr)
	, attenuation(0.0f)
	, pos(0)
	, pitchtrack(false)
	, m_resampler(AudioResampler::Mode::Linear, DEFAULT_CHANNELS)
	, sampleFreq(0)
	, freqFactor(1)
{
}




// Attaches a sample to this voice. This does exactly what the old GigSample
// constructor did, except that the resampler is reset instead of being built
// from scratch: a reset converter behaves like a fresh one.
void GigSample::attach( gig::Sample* pSample, gig::DimensionRegion* pDimRegion, float attenuation,
		float desiredFreq )
{
	sample = pSample;
	region = pDimRegion;
	this->attenuation = attenuation;
	pos = 0;
	pitchtrack = false;
	sampleFreq = 0;
	freqFactor = 1;
	m_sourceBufferView = std::span<SampleFrame>();
	m_mixBufferView = std::span<SampleFrame>();
	m_resampler.reset();

	if( sample != nullptr && region != nullptr )
	{
		// Calculate note pitch and frequency factor only if we're actually
		// going to be changing the pitch of the notes
		if( region->PitchTrack == true )
		{
			// Calculate what frequency the provided sample is
			sampleFreq = 440.0f * std::exp2((region->UnityNote - 69 - region->FineTune * 0.01) / 12.0f);
			freqFactor = sampleFreq / desiredFreq;
		}

		// The sample rate we pass in is affected by how we are going to be
		// resampling the note so that a 1.5 second release ends up being 1.5
		// seconds after resampling
		adsr = ADSR( region, sample->SamplesPerSecond / freqFactor );
	}
	else
	{
		adsr = ADSR();
	}
}

ADSR::ADSR()
	: preattack( 0 ), attack( 0 ), decay1( 0 ), decay2( 0 ), infiniteSustain( false ),
	  sustain( 0 ), release( 0 ),
	  amplitude( 0 ), isAttack( true ), isRelease( false ), isDone( false ),
	  attackPosition( 0 ), attackLength( 0 ), decayLength( 0 ),
	  releasePosition( 0 ), releaseLength( 0 )
{
}




// Create the ADSR envelope from the settings in the GIG file
ADSR::ADSR( gig::DimensionRegion * region, int sampleRate )
	: preattack( 0 ), attack( 0 ), decay1( 0 ), decay2( 0 ), infiniteSustain( false ),
	  sustain( 0 ), release( 0 ),
	  amplitude( 0 ), isAttack( true ), isRelease( false ), isDone( false ),
	  attackPosition( 0 ), attackLength( 0 ), decayLength( 0 ),
	  releasePosition( 0 ), releaseLength( 0 )
{
	if( region != nullptr )
	{
		// Parameters from GIG file
		preattack = 1.0 * region->EG1PreAttack / 1000; // EG1PreAttack is 0-1000 permille
		attack = region->EG1Attack;
		decay1 = region->EG1Decay1;
		decay2 = region->EG1Decay2;
		infiniteSustain = region->EG1InfiniteSustain;
		sustain = 1.0 * region->EG1Sustain / 1000; // EG1Sustain is 0-1000 permille
		release = region->EG1Release;

		// Simple ADSR using positions in sample
		amplitude = preattack;
		attackLength = attack * sampleRate;
		decayLength = decay1 * sampleRate; // TODO: ignoring decay2 for now
		releaseLength = release * sampleRate;

		// If there is no attack or decay, start at the sustain amplitude
		if( attackLength == 0 && decayLength == 0 )
		{
			amplitude = sustain;
		}
		// If there is no attack, start at the full amplitude
		else if( attackLength == 0 )
		{
			amplitude = 1.0;
		}
	}
}




// Next time we get the amplitude, we'll be releasing the note
void ADSR::keyup()
{
	isRelease = true;
}




// Can we delete the sample now?
bool ADSR::done()
{
	return isDone;
}




// Return the current amplitude and increment internal positions
float ADSR::value()
{
	float currentAmplitude = amplitude;

	// If we're done, don't output any signal
	if( isDone == true )
	{
		return 0;
	}
	// If we're still in the attack phase, release from the current volume
	// instead of jumping to the sustain volume and fading out
	else if( isAttack == true && isRelease == true )
	{
		sustain = amplitude;
		isAttack = false;
	}

	// If we're in the attack phase, start at the preattack amplitude and
	// increase to the full before decreasing to sustain
	if( isAttack == true )
	{
		if( attackPosition < attackLength )
		{
			amplitude = preattack + ( 1.0 - preattack ) / attackLength * attackPosition;
		}
		else if( attackPosition < attackLength + decayLength )
		{
			amplitude = 1.0 - ( 1.0 - sustain ) / decayLength * ( attackPosition - attackLength );
		}
		else
		{
			isAttack = false;
		}

		++attackPosition;
	}
	// If we're in the sustain phase, decrease from sustain to zero
	else if( isRelease == true )
	{
		// Maybe not the best way of doing this, but it appears to be about right
		// Satisfies f(0) = sustain and f(releaseLength) = very small
		amplitude = (sustain + 1e-3) * std::exp(-5.0f / releaseLength * releasePosition) - 1e-3;

		// Don't have an infinite exponential decay
		if( amplitude <= 0 || releasePosition >= releaseLength )
		{
			amplitude = 0;
			isDone = true;
		}

		++releasePosition;
	}

	return currentAmplitude;
}




// Increment internal positions a certain number of times
void ADSR::inc( f_cnt_t num )
{
	for( f_cnt_t i = 0; i < num; ++i )
	{
		value();
	}
}




extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model *m, void * )
{
	return new GigInstrument( static_cast<InstrumentTrack *>( m ) );
}

}


} // namespace lmms

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
