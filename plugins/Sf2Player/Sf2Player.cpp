/*
 * Sf2Player.cpp - a soundfont2 player using fluidSynth
 *
 * Copyright (c) 2008 Paul Giblock <drfaygo/at/gmail/dot/com>
 * Copyright (c) 2009-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "Sf2Player.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fluidsynth.h>
#include <limits>
#include <utility>
#include <QDebug>
#include <QDomElement>
#include <QFile>
#include <QLabel>

#include "ArrayVector.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "FileDialog.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "InstrumentPlayHandle.h"
#include "Knob.h"
#include "NotePlayHandle.h"
#include "PathUtil.h"
#include "PixmapButton.h"
#include "Song.h"
#include "fluidsynthshims.h"

#include "PatchesDialog.h"
#include "LcdSpinBox.h"

#include "embed.h"
#include "plugin_export.h"

namespace lmms
{


#define FLUIDSYNTH_VERSION_HEX ((FLUIDSYNTH_VERSION_MAJOR << 16) \
	| (FLUIDSYNTH_VERSION_MINOR << 8) \
	| FLUIDSYNTH_VERSION_MICRO)
#define USE_NEW_EFFECT_API (FLUIDSYNTH_VERSION_HEX >= 0x020200)


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT sf2player_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"Sf2 Player",
	QT_TRANSLATE_NOOP( "PluginBrowser", "Player for SoundFont files" ),
	"Paul Giblock <drfaygo/at/gmail/dot/com>",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader( "logo" ),
	"sf2,sf3",
	nullptr,
} ;

}

// FluidVoice and Sf2PluginData live in the header: the instrument's per-note
// state is part of its interface to the note stage and the render stage.


namespace
{

//! One in-memory file the FluidSynth soundfont loader reads from.
struct Sf2MemoryFile
{
	//! The font's bytes. The loader's callbacks carry no user data of their
	//! own, so the file keeps the bytes alive while it is open.
	std::shared_ptr<const QByteArray> data;
	//! The read position in the bytes.
	qint64 position = 0;
};

//! The font the calling thread is loading right now.
//!
//! FluidSynth's loader callbacks have no user data, so the bytes they read
//! are handed over through this pointer. The load runs on the thread that
//! called fluid_synth_sfload() and it is over when that call returns.
thread_local std::shared_ptr<const QByteArray> s_loadingFont;

//! The name the in-memory font is loaded under. The default FluidSynth loader
//! never finds a file with that name, so the font is never loaded twice.
constexpr const char* s_memoryFontName = "zene-sf2-memory-font";


//! Opens the in-memory font. The name is ignored: the bytes are the ones the
//! loading thread handed over.
void* sf2MemoryOpen( const char* name )
{
	Q_UNUSED( name );
	if( s_loadingFont == nullptr ) { return nullptr; }
	return new Sf2MemoryFile{ s_loadingFont, 0 };
}


//! Reads \a count bytes. A short read is a failure, as FluidSynth expects it.
int sf2MemoryRead( void* buffer, fluid_long_long_t count, void* handle )
{
	auto* file = static_cast<Sf2MemoryFile*>( handle );
	if( count < 0 || file->position + count > file->data->size() ) { return FLUID_FAILED; }
	std::memcpy( buffer, file->data->constData() + file->position,
		static_cast<std::size_t>( count ) );
	file->position += count;
	return FLUID_OK;
}


//! Moves the read position.
int sf2MemorySeek( void* handle, fluid_long_long_t offset, int origin )
{
	auto* file = static_cast<Sf2MemoryFile*>( handle );
	qint64 base = 0;
	if( origin == SEEK_SET ) { base = 0; }
	else if( origin == SEEK_CUR ) { base = file->position; }
	else if( origin == SEEK_END ) { base = file->data->size(); }
	else { return FLUID_FAILED; }

	const qint64 target = base + offset;
	if( target < 0 || target > file->data->size() ) { return FLUID_FAILED; }
	file->position = target;
	return FLUID_OK;
}


//! The read position.
fluid_long_long_t sf2MemoryTell( void* handle )
{
	return static_cast<fluid_long_long_t>( static_cast<Sf2MemoryFile*>( handle )->position );
}


//! Closes the file.
int sf2MemoryClose( void* handle )
{
	delete static_cast<Sf2MemoryFile*>( handle );
	return FLUID_OK;
}

} // namespace



Sf2Instrument::Sf2Instrument( InstrumentTrack * _instrument_track ) :
	Instrument(_instrument_track, &sf2player_plugin_descriptor, nullptr, Flag::IsSingleStreamed),
	m_resampler(AudioResampler::Mode::Linear),
	m_synth(nullptr),
	m_filename( "" ),
	m_lastMidiPitch( -1 ),
	m_lastMidiPitchRange( -1 ),
	m_channel( 1 ),
	m_bankNum( 0, 0, 999, this, tr("Bank") ),
	m_patchNum( 0, 0, 127, this, tr("Patch") ),
	m_gain( 1.0f, 0.0f, 5.0f, 0.01f, this, tr( "Gain" ) ),
	m_reverbOn( false, this, tr( "Reverb" ) ),
	m_reverbRoomSize( FLUID_REVERB_DEFAULT_ROOMSIZE, 0, 1.0, 0.01f, this, tr( "Reverb room size" ) ),
	m_reverbDamping(FLUID_REVERB_DEFAULT_DAMP, 0, 1.f, 0.01f, this, tr("Reverb damping")),
	m_reverbWidth( FLUID_REVERB_DEFAULT_WIDTH, 0, 1.0, 0.01f, this, tr( "Reverb width" ) ),
	m_reverbLevel( FLUID_REVERB_DEFAULT_LEVEL, 0, 1.0, 0.01f, this, tr( "Reverb level" ) ),
	m_chorusOn( false, this, tr( "Chorus" ) ),
	m_chorusNum( FLUID_CHORUS_DEFAULT_N, 0, 10.0, 1.0, this, tr( "Chorus voices" ) ),
	m_chorusLevel(FLUID_CHORUS_DEFAULT_LEVEL, 0, 10.f, 0.01f, this, tr("Chorus level")),
	m_chorusSpeed(FLUID_CHORUS_DEFAULT_SPEED, 0.29f, 5.f, 0.01f, this, tr("Chorus speed")),
	m_chorusDepth(FLUID_CHORUS_DEFAULT_DEPTH, 0, 46.f, 0.05f, this, tr("Chorus depth"))
{


#if QT_VERSION_CHECK(FLUIDSYNTH_VERSION_MAJOR, FLUIDSYNTH_VERSION_MINOR, FLUIDSYNTH_VERSION_MICRO) >= QT_VERSION_CHECK(1,1,9)
	// Deactivate all audio drivers in fluidsynth
	const char *none[] = { nullptr };
	fluid_audio_driver_register( none );
#endif

#if FLUIDSYNTH_VERSION_MAJOR >= 2
	// Get the default values from the setting
	readSynthDefaults();
#endif

	// Build and publish the first synth; the render thread adopts it and it is
	// configured with the reverb/chorus/gain requests below.
	reloadSynth();

	// FIXME: there's no good way to tell if we're loading a preset or an empty instrument
	// We rely on instantiate() to load the default soundfont for new instruments,
	// but we don't need that when loading a project/preset/preview
	if (!Engine::getSong()->isLoadingProject() && !instrumentTrack()->isPreviewMode())
	{
		loadFile(ConfigManager::inst()->sf2File());
	}

	connect( &m_bankNum, SIGNAL( dataChanged() ), this, SLOT( updatePatch() ) );
	connect( &m_patchNum, SIGNAL( dataChanged() ), this, SLOT( updatePatch() ) );

	connect(Engine::audioEngine(), SIGNAL(sampleRateChanged()), this, SLOT(reloadSynth()));

	// Gain
	connect( &m_gain, SIGNAL( dataChanged() ), this, SLOT( updateGain() ) );

	// Reverb
	connect( &m_reverbOn, SIGNAL( dataChanged() ), this, SLOT( updateReverbOn() ) );
	connect( &m_reverbRoomSize, SIGNAL( dataChanged() ), this, SLOT( updateReverb() ) );
	connect( &m_reverbDamping, SIGNAL( dataChanged() ), this, SLOT( updateReverb() ) );
	connect( &m_reverbWidth, SIGNAL( dataChanged() ), this, SLOT( updateReverb() ) );
	connect( &m_reverbLevel, SIGNAL( dataChanged() ), this, SLOT( updateReverb() ) );

	// Chorus
	connect( &m_chorusOn, SIGNAL( dataChanged() ), this, SLOT( updateChorusOn() ) );
	connect( &m_chorusNum, SIGNAL( dataChanged() ), this, SLOT( updateChorus() ) );
	connect( &m_chorusLevel, SIGNAL( dataChanged() ), this, SLOT( updateChorus() ) );
	connect( &m_chorusSpeed, SIGNAL( dataChanged() ), this, SLOT( updateChorus() ) );
	connect( &m_chorusDepth, SIGNAL( dataChanged() ), this, SLOT( updateChorus() ) );
	
	// Microtuning
	connect(Engine::getSong(), &Song::scaleListChanged, this, &Sf2Instrument::updateTuning);
	connect(Engine::getSong(), &Song::keymapListChanged, this, &Sf2Instrument::updateTuning);
	connect(instrumentTrack()->microtuner()->enabledModel(), &Model::dataChanged, this, &Sf2Instrument::updateTuning, Qt::DirectConnection);
	connect(instrumentTrack()->microtuner()->scaleModel(), &Model::dataChanged, this, &Sf2Instrument::updateTuning, Qt::DirectConnection);
	connect(instrumentTrack()->microtuner()->keymapModel(), &Model::dataChanged, this, &Sf2Instrument::updateTuning, Qt::DirectConnection);
	connect(instrumentTrack()->microtuner()->keyRangeImportModel(), &Model::dataChanged, this, &Sf2Instrument::updateTuning, Qt::DirectConnection);
	connect(instrumentTrack()->baseNoteModel(), &Model::dataChanged, this, &Sf2Instrument::updateTuning, Qt::DirectConnection);

	// The models were initialised (and the default soundfont loaded) before the
	// connections above, so send their current values to the synth.
	updatePatch();
	updateReverbOn();
	updateReverb();
	updateChorusOn();
	updateChorus();
	updateGain();
	updateTuning();

	auto iph = new InstrumentPlayHandle(this, _instrument_track);
	Engine::audioEngine()->addPlayHandle( iph );
}



Sf2Instrument::~Sf2Instrument()
{
	Engine::audioEngine()->removePlayHandlesOfTypes( instrumentTrack(),
				PlayHandle::Type::NotePlayHandle
				| PlayHandle::Type::InstrumentPlayHandle );

	// No play handle can reach the instrument any more, so the render thread has
	// stopped: every synth the UI thread created can be deleted here. The
	// queues only carry the synths, the settings travel with the synths.
	Sf2SynthSwap pending;
	while( m_pendingSynths.pop( pending ) ) { /* the synths are deleted below */ }
	m_deferredSynth = {};
	fluid_synth_t* retired = nullptr;
	while( m_retiredSynths.pop( retired ) ) { /* the synths are deleted below */ }
	m_synth = nullptr;

	for( const auto& owned : m_ownedSynths )
	{
		if( owned.synth != nullptr ) { delete_fluid_synth( owned.synth ); }
		if( owned.settings != nullptr ) { delete_fluid_settings( owned.settings ); }
	}
	m_ownedSynths.clear();
}



void Sf2Instrument::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	_this.setAttribute( "src", m_filename );
	m_patchNum.saveSettings( _doc, _this, "patch" );
	m_bankNum.saveSettings( _doc, _this, "bank" );

	m_gain.saveSettings( _doc, _this, "gain" );

	m_reverbOn.saveSettings( _doc, _this, "reverbOn" );
	m_reverbRoomSize.saveSettings( _doc, _this, "reverbRoomSize" );
	m_reverbDamping.saveSettings( _doc, _this, "reverbDamping" );
	m_reverbWidth.saveSettings( _doc, _this, "reverbWidth" );
	m_reverbLevel.saveSettings( _doc, _this, "reverbLevel" );

	m_chorusOn.saveSettings( _doc, _this, "chorusOn" );
	m_chorusNum.saveSettings( _doc, _this, "chorusNum" );
	m_chorusLevel.saveSettings( _doc, _this, "chorusLevel" );
	m_chorusSpeed.saveSettings( _doc, _this, "chorusSpeed" );
	m_chorusDepth.saveSettings( _doc, _this, "chorusDepth" );
}




void Sf2Instrument::loadSettings( const QDomElement & _this )
{
	QString src = _this.attribute("src");
	if (!src.isEmpty())
	{
		openFile(src, false);
		m_patchNum.loadSettings(_this, "patch");
		m_bankNum.loadSettings(_this, "bank");
	}
	
	m_gain.loadSettings( _this, "gain" );

	m_reverbOn.loadSettings( _this, "reverbOn" );
	m_reverbRoomSize.loadSettings( _this, "reverbRoomSize" );
	m_reverbDamping.loadSettings( _this, "reverbDamping" );
	m_reverbWidth.loadSettings( _this, "reverbWidth" );
	m_reverbLevel.loadSettings( _this, "reverbLevel" );

	m_chorusOn.loadSettings( _this, "chorusOn" );
	m_chorusNum.loadSettings( _this, "chorusNum" );
	m_chorusLevel.loadSettings( _this, "chorusLevel" );
	m_chorusSpeed.loadSettings( _this, "chorusSpeed" );
	m_chorusDepth.loadSettings( _this, "chorusDepth" );

}




void Sf2Instrument::loadFile( const QString & _file )
{
	if( !_file.isEmpty() && QFileInfo( _file ).exists() )
	{
		openFile( _file, false );
	}

	// Setting the first bank and patch number that is found. The preset list is
	// the UI-side snapshot of the loaded soundfont; the audio synth stays private
	// to the render thread.
	if( !m_presets.empty() )
	{
		m_bankNum.setValue( m_presets.front().bank );
		m_patchNum.setValue( m_presets.front().program );
	}
	else
	{
		m_bankNum.setValue( 0 );
		m_patchNum.setValue( 0 );
	}
	updatePatch();
}




AutomatableModel * Sf2Instrument::childModel( const QString & _modelName )
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



QString Sf2Instrument::nodeName() const
{
	return sf2player_plugin_descriptor.name;
}




Sf2OwnedSynth Sf2Instrument::createSynth( sample_rate_t outputRate )
{
	Sf2OwnedSynth owned;

	// FluidSynth allows one synth per settings object, so every synth gets a
	// settings object of its own. It is kept until the synth is deleted.
	owned.settings = new_fluid_settings();
	if( owned.settings == nullptr ) { return owned; }

	// LMMS supports 192 kHz, FluidSynth only up to 96 kHz. When the requested
	// rate differs the player resamples with libsamplerate and the synth uses
	// linear interpolation to match it.
	fluid_settings_setnum( owned.settings, "synth.sample-rate", outputRate );

	// FluidSynth's own effects are on by default while the player's are off:
	// the synth has to start with the state the models ask for.
	fluid_settings_setint( owned.settings, "synth.reverb.active", m_reverbOn.value() ? 1 : 0 );
	fluid_settings_setint( owned.settings, "synth.chorus.active", m_chorusOn.value() ? 1 : 0 );

	// The render thread is the only user of this synth: it does not need
	// FluidSynth's API mutex, and taking it would make the audio callback wait
	// for another thread.
	fluid_settings_setint( owned.settings, "synth.threadsafe-api", 0 );

	owned.synth = new_fluid_synth( owned.settings );
	if( owned.synth == nullptr )
	{
		delete_fluid_settings( owned.settings );
		owned.settings = nullptr;
		return owned;
	}

	// The soundfont is loaded from the instrument's own copy of the file, so a
	// synth can be built even when the file behind the font is gone. The
	// loader reads that copy, and every synth gets a loader of its own: the
	// synth frees its loaders with itself.
	auto* loader = new_fluid_defsfloader( owned.settings );
	if( loader == nullptr ) { return owned; }
	fluid_sfloader_set_callbacks( loader, sf2MemoryOpen, sf2MemoryRead,
		sf2MemorySeek, sf2MemoryTell, sf2MemoryClose );
	fluid_synth_add_sfloader( owned.synth, loader );

	double synthRate = 0;
	fluid_settings_getnum( owned.settings, "synth.sample-rate", &synthRate );
	if( static_cast<sample_rate_t>( synthRate ) != outputRate )
	{
		fluid_synth_set_interp_method( owned.synth, -1, FLUID_INTERP_LINEAR );
	}

	m_ownedSynths.push_back( owned );
	return owned;
}



void Sf2Instrument::destroySynth( fluid_synth_t* synth ) noexcept
{
	if( synth == nullptr ) { return; }
	for( auto owned = m_ownedSynths.begin(); owned != m_ownedSynths.end(); ++owned )
	{
		if( owned->synth != synth ) { continue; }
		if( owned->synth != nullptr ) { delete_fluid_synth( owned->synth ); }
		if( owned->settings != nullptr ) { delete_fluid_settings( owned->settings ); }
		m_ownedSynths.erase( owned );
		return;
	}
}



bool Sf2Instrument::publishSynth( fluid_synth_t* synth, int fontId, sample_rate_t sampleRate )
{
	if( synth == nullptr ) { return false; }

	const Sf2SynthSwap swap{ synth, fontId, sampleRate };
	{
		// Project loads and UI edits can both publish; the lock is UI-side only
		// and the audio thread never takes it.
		const std::lock_guard lock( m_synthHandoffMutex );
		drainRetiredSynthsLocked();
		if( !m_pendingSynths.push( swap ) )
		{
			// The render thread is behind (or stopped). One more drain, then
			// refuse: the caller deletes the candidate and the old synth stays.
			drainRetiredSynthsLocked();
			if( !m_pendingSynths.push( swap ) ) { return false; }
		}
	}

	return true;
}



void Sf2Instrument::drainRetiredSynths()
{
	const std::lock_guard lock( m_synthHandoffMutex );
	drainRetiredSynthsLocked();
}



void Sf2Instrument::drainRetiredSynthsLocked()
{
	fluid_synth_t* retired = nullptr;
	while( m_retiredSynths.pop( retired ) )
	{
		// The synth and the settings it was built with go together.
		destroySynth( retired );
	}
}



void Sf2Instrument::collectPresets( fluid_synth_t* synth, std::vector<Sf2PresetInfo>& presets )
{
	presets.clear();
	if( synth == nullptr ) { return; }

	const int soundFontCount = fluid_synth_sfcount( synth );
	for( int i = 0; i < soundFontCount; ++i )
	{
		fluid_sfont_t* soundFont = fluid_synth_get_sfont( synth, i );
		if( soundFont == nullptr ) { continue; }

#ifdef CONFIG_FLUID_BANK_OFFSET
		const int bankOffset = fluid_synth_get_bank_offset( synth, fluid_sfont_get_id( soundFont ) );
#endif

		fluid_sfont_iteration_start( soundFont );
#if FLUIDSYNTH_VERSION_MAJOR < 2
		fluid_preset_t preset;
		fluid_preset_t* currentPreset = &preset;
#else
		fluid_preset_t* currentPreset = nullptr;
#endif
		while( ( currentPreset = fluid_sfont_iteration_next_wrapper( soundFont, currentPreset ) ) )
		{
			int bank = fluid_preset_get_banknum( currentPreset );
#ifdef CONFIG_FLUID_BANK_OFFSET
			bank += bankOffset;
#endif
			const char* name = fluid_preset_get_name( currentPreset );
			presets.push_back( Sf2PresetInfo{ bank,
				fluid_preset_get_num( currentPreset ),
				QString::fromUtf8( name != nullptr ? name : "" ) } );
		}
	}
}



void Sf2Instrument::openFile( const QString & _sf2File, bool updateTrackName )
{
	emit fileLoading();
	drainRetiredSynths();

	const QString absolutePath = PathUtil::toAbsolute( _sf2File );
	const QByteArray nativePath = QFile::encodeName( absolutePath );

	// The soundfont is read into the instrument's own copy, which keeps it
	// alive: a synth that plays it can be replaced without reading the file
	// again, so a reload still works when the file behind it is gone.
	if( !loadFont( nativePath ) )
	{
		collectErrorForUI( Sf2Instrument::tr( "A soundfont %1 could not be loaded." )
			.arg( QFileInfo( _sf2File ).baseName() ) );
		return;
	}

	// Build the candidate completely off the audio thread. Nothing is published
	// until it is a working synth, so a failed load leaves the current font and
	// the notes already playing exactly as they were.
	auto candidate = createSynth( Engine::audioEngine()->outputSampleRate() );
	const int candidateFontId = candidate.synth != nullptr ? addFontTo( candidate.synth ) : -1;

	const bool loaded = candidate.synth != nullptr && candidateFontId >= 0;
	if( loaded )
	{
		double configuredRate = 0;
		fluid_settings_getnum( candidate.settings, "synth.sample-rate", &configuredRate );
		const auto internalRate = static_cast<sample_rate_t>( configuredRate );

		std::vector<Sf2PresetInfo> presets;
		collectPresets( candidate.synth, presets );

		if( publishSynth( candidate.synth, candidateFontId, internalRate ) )
		{
			m_presets = std::move( presets );
			m_sourceFile = absolutePath;
			m_filename = PathUtil::toShortestRelative( _sf2File );

			// Don't reset patch/bank, so that it isn't cleared when
			// someone resolves a missing file
			emit fileChanged();

			if( updateTrackName || instrumentTrack()->displayName() == displayName() )
			{
				instrumentTrack()->setName( PathUtil::cleanName( _sf2File ) );
			}
		}
		else
		{
			destroySynth( candidate.synth );
			collectErrorForUI( Sf2Instrument::tr( "A soundfont %1 could not be loaded." )
				.arg( QFileInfo( _sf2File ).baseName() ) );
		}
	}
	else
	{
		destroySynth( candidate.synth );
		collectErrorForUI( Sf2Instrument::tr( "A soundfont %1 could not be loaded." )
			.arg( QFileInfo( _sf2File ).baseName() ) );
	}

	updatePatch();
}



bool Sf2Instrument::loadFont( const QByteArray& nativePath )
{
	if( !fluid_is_soundfont( nativePath.constData() ) ) { return false; }

	// The instrument keeps its own copy of the font's bytes: every synth that
	// plays the font parses that copy into a font instance of its own, so a
	// reload still works when the file behind the font is gone.
	QFile file( nativePath );
	if( !file.open( QIODevice::ReadOnly ) ) { return false; }
	const auto data = file.readAll();
	if( data.isEmpty() ) { return false; }

	m_fontData = std::make_shared<const QByteArray>( std::move( data ) );
	return true;
}



int Sf2Instrument::addFontTo( fluid_synth_t* synth )
{
	if( synth == nullptr || m_fontData == nullptr ) { return -1; }

	// The synth loads the font from the instrument's own copy of the file.
	// FluidSynth hands the ownership of a font to the synth it is added to and
	// frees the font with that synth, so the same font must never be added to
	// two synths: every synth parses the copy into a font of its own.
	s_loadingFont = m_fontData;
	const int fontId = fluid_synth_sfload( synth, s_memoryFontName, 1 );
	s_loadingFont = nullptr;
	return fontId;
}



void Sf2Instrument::updatePatch()
{
	drainRetiredSynths();
	m_requestedBank.store( m_bankNum.value(), std::memory_order_relaxed );
	m_requestedPatch.store( m_patchNum.value(), std::memory_order_relaxed );
}



QString Sf2Instrument::getCurrentPatchName()
{
	drainRetiredSynths();

	const int bank = m_bankNum.value();
	const int program = m_patchNum.value();
	for( const auto& preset : m_presets )
	{
		if( preset.bank == bank && preset.program == program ) { return preset.name; }
	}
	return "";
}



void Sf2Instrument::updateGain()
{
	m_requestedGain.store( m_gain.value(), std::memory_order_relaxed );
}



void Sf2Instrument::updateReverbOn()
{
	m_requestedReverbOn.store( m_reverbOn.value() ? 1 : 0, std::memory_order_relaxed );
}



void Sf2Instrument::updateReverb()
{
	m_requestedReverbRoomSize.store( m_reverbRoomSize.value(), std::memory_order_relaxed );
	m_requestedReverbDamping.store( m_reverbDamping.value(), std::memory_order_relaxed );
	m_requestedReverbWidth.store( m_reverbWidth.value(), std::memory_order_relaxed );
	m_requestedReverbLevel.store( m_reverbLevel.value(), std::memory_order_relaxed );
}



void Sf2Instrument::updateChorusOn()
{
	m_requestedChorusOn.store( m_chorusOn.value() ? 1 : 0, std::memory_order_relaxed );
}



void Sf2Instrument::updateChorus()
{
	m_requestedChorusNum.store( m_chorusNum.value(), std::memory_order_relaxed );
	m_requestedChorusLevel.store( m_chorusLevel.value(), std::memory_order_relaxed );
	m_requestedChorusSpeed.store( m_chorusSpeed.value(), std::memory_order_relaxed );
	m_requestedChorusDepth.store( m_chorusDepth.value(), std::memory_order_relaxed );
}



void Sf2Instrument::updateTuning()
{
	// The table holds the offset of every key in cents, relative to the equal
	// temperament: zero means the normal equal temperament, so the microtuner
	// off (or a scale that is the equal temperament) leaves the notes alone.
	std::array<double, 128> offset{};
	const bool enabled = instrumentTrack()->microtuner()->enabledModel()->value();
	if( enabled )
	{
		double lowestHz = std::exp2( -69. / 12. ) * 440.; // Frequency of MIDI note 0, which is approximately 8.175798916 Hz
		for( int i = 0; i < 128; ++i )
		{
			// Get desired Hz of note
			double noteHz = instrumentTrack()->microtuner()->keyToFreq( i, DefaultBaseKey );
			// Convert Hz to cents. The equal temperament plays key i at i * 100
			// cents, so the difference is the offset of the scale.
			const double cents = noteHz == 0. ? 0. : 1200. * log2( noteHz / lowestHz );
			offset[i] = enabled ? cents - i * 100. : 0.;
		}
	}

	// Seqlock: the generation goes odd while the table is written and even once
	// it is complete. The render thread reads only a complete, even snapshot.
	m_tuningGeneration.fetch_add( 1, std::memory_order_acq_rel );
	for( int i = 0; i < 128; ++i )
	{
		m_requestedTuning[i].store( offset[i], std::memory_order_relaxed );
	}
	m_tuningGeneration.fetch_add( 1, std::memory_order_release );
}



double Sf2Instrument::tuningOffsetForKey( int key ) const noexcept
{
	if( key < 0 || key >= 128 ) { return 0.; }
	const std::uint32_t generation = m_tuningGeneration.load( std::memory_order_acquire );
	if( generation & 1u ) { return 0.; }
	const double offset = m_requestedTuning[key].load( std::memory_order_relaxed );
	// Only take the value when the UI writer did not start another update.
	if( m_tuningGeneration.load( std::memory_order_acquire ) != generation ) { return 0.; }
	return offset;
}



void Sf2Instrument::readSynthDefaults()
{
	// The defaults come from a throw-away settings object: every synth has a
	// settings object of its own.
	fluid_settings_t* defaults = new_fluid_settings();
	if( defaults == nullptr ) { return; }

	double settingVal = 0;
	fluid_settings_getnum_default( defaults, "synth.reverb.room-size", &settingVal );
	m_reverbRoomSize.setInitValue(settingVal);
	fluid_settings_getnum_default( defaults, "synth.reverb.damping", &settingVal );
	m_reverbDamping.setInitValue(settingVal);
	fluid_settings_getnum_default( defaults, "synth.reverb.width", &settingVal );
	m_reverbWidth.setInitValue(settingVal);
	fluid_settings_getnum_default( defaults, "synth.reverb.level", &settingVal );
	m_reverbLevel.setInitValue(settingVal);

	fluid_settings_getnum_default( defaults, "synth.chorus.nr", &settingVal );
	m_chorusNum.setInitValue(settingVal);
	fluid_settings_getnum_default( defaults, "synth.chorus.level", &settingVal );
	m_chorusLevel.setInitValue(settingVal);
	fluid_settings_getnum_default( defaults, "synth.chorus.speed", &settingVal );
	m_chorusSpeed.setInitValue(settingVal);
	fluid_settings_getnum_default( defaults, "synth.chorus.depth", &settingVal );
	m_chorusDepth.setInitValue(settingVal);

	delete_fluid_settings( defaults );
}



void Sf2Instrument::reloadSynth()
{
	const auto outputRate = Engine::audioEngine()->outputSampleRate();

	// Build the replacement off the audio thread. The render thread adopts it at
	// the start of its next period and retires the old one for this thread to
	// delete, so the audio callback never waits for the reload.
	auto candidate = createSynth( outputRate );
	if( candidate.synth == nullptr ) { return; }

	double tempRate = 0;
	// Set & get, returns the true sample rate
	fluid_settings_getnum( candidate.settings, "synth.sample-rate", &tempRate );
	const auto internalSampleRate = static_cast<sample_rate_t>( tempRate );

	// The candidate plays the font that is already loaded: the reload must work
	// even when the file behind the font is gone, and the candidate parses the
	// instrument's own copy of the font into a font instance of its own.
	const int fontId = addFontTo( candidate.synth );
	if( fontId < 0 && m_fontData != nullptr )
	{
		// The font could not be loaded: keep the synth that still plays it, so
		// the playback and the preset list survive the reload.
		destroySynth( candidate.synth );
		return;
	}

	std::vector<Sf2PresetInfo> presets;
	if( fontId >= 0 ) { collectPresets( candidate.synth, presets ); }

	if( !publishSynth( candidate.synth, fontId, internalSampleRate ) )
	{
		destroySynth( candidate.synth );
		return;
	}
	if( fontId >= 0 ) { m_presets = std::move( presets ); }

	// Reset last MIDI pitch properties, which will be set to the correct values
	// upon playing the next note
	m_lastMidiPitch = -1;
	m_lastMidiPitchRange = -1;
}



Sf2OwnedSynth Sf2Instrument::createPatchDialogSynth()
{
	Sf2OwnedSynth owned;
	if( m_sourceFile.isEmpty() ) { return owned; }

	// The dialog gets a synth of its own, with a settings object of its own, so
	// it never shares one with the synth the render thread plays.
	owned = createSynth( Engine::audioEngine()->outputSampleRate() );
	if( owned.synth == nullptr ) { return owned; }

	const int fontId = addFontTo( owned.synth );
	if( fontId < 0 )
	{
		destroySynth( owned.synth );
		owned = {};
		return owned;
	}

	fluid_synth_program_select( owned.synth, m_channel, fontId, m_bankNum.value(), m_patchNum.value() );
	return owned;
}



Sf2PluginDataSlot* Sf2Instrument::acquirePluginDataSlot(std::uint32_t& generation) noexcept
{
	for (Sf2PluginDataSlot& slot : m_pluginDataSlots)
	{
		auto state = slot.state.load(std::memory_order_acquire);
		while ((state & 1u) == 0u) // the slot is free
		{
			// Claim it with the next generation, so an event that names an older
			// generation of this slot is recognised as stale.
			const auto claimed = ((state >> 1) + 1u) * 2u + 1u;
			if (slot.state.compare_exchange_weak(state, claimed,
					std::memory_order_acq_rel, std::memory_order_acquire))
			{
				generation = claimed >> 1;
				slot.data.fluidVoices.clear();
				slot.data.noteOffSent = false;
				slot.data.lastPanning = 0;
				return &slot;
			}
			// The compare-exchange refreshed `state`: try the slot again.
		}
	}
	return nullptr;
}



void Sf2Instrument::releasePluginDataSlot(Sf2PluginDataSlot* slot) noexcept
{
	if (slot == nullptr) { return; }
	// Freeing the slot also moves its generation on, so an event queued for the
	// note that owned it does not match the slot any more.
	slot->state.fetch_add(1, std::memory_order_acq_rel);
}



bool Sf2Instrument::publishNoteEvent(NotePlayHandle* handle, Sf2PluginDataSlot* slot,
	std::uint32_t generation, f_cnt_t offset, bool isNew) noexcept
{
	Sf2NoteEvent event;
	event.handle = handle;
	event.slot = slot;
	event.generation = generation;
	event.offset = offset;
	event.midiNote = slot != nullptr ? slot->data.midiNote : 0;
	event.isNew = isNew;
	event.standalone = false;
	// The queue is bounded: a refused event is counted by the queue itself and
	// the note keeps its state, so the note is silent for this period instead
	// of the audio path growing a container or waiting.
	return m_noteEvents.push( event );
}




int Sf2Instrument::runningNotesForTest() const
{
	int running = 0;
	for( const auto& notes : m_notesRunning )
	{
		running += notes.load( std::memory_order_relaxed );
	}
	return running;
}




int Sf2Instrument::fillNoteEventQueueForTest()
{
	// The filler events name a slot that is never claimed, so the render stage
	// drops them: the queue fills up without changing what the synth plays.
	Sf2NoteEvent event;
	event.slot = &m_fillerSlot;
	event.generation = 0;
	event.offset = 0;
	event.midiNote = 0;
	event.isNew = true;
	event.standalone = false;

	// The queue is bounded, so the loop ends when the queue refuses the event.
	int queued = 0;
	while( m_noteEvents.push( event ) ) { ++queued; }
	return queued;
}



void Sf2Instrument::playNoteImpl(NotePlayHandle* _n, std::span<SampleFrame>)
{
	if( _n->isMasterNote() || ( _n->hasParent() && _n->isReleased() ) )
	{
		return;
	}

	int masterPitch = instrumentTrack()->useMasterPitchModel()->value() ? Engine::getSong()->masterPitch() : 0;
	int baseNote = instrumentTrack()->baseNoteModel()->value();
	int midiNote = _n->midiKey() - baseNote + DefaultBaseKey + masterPitch;

	// out of range?
	if (midiNote < 0 || midiNote >= 128)
	{
		return;
	}

	if (!_n->m_pluginData)
	{
		const int baseVelocity = instrumentTrack()->midiPort()->baseVelocity();

		// The note claims one of the preallocated slots: the note path must not
		// allocate on the audio thread.
		std::uint32_t generation = 0;
		auto* slot = acquirePluginDataSlot(generation);
		if (slot == nullptr)
		{
			// Every slot is taken by a note that is still alive. The note stays
			// silent for this period instead of the audio path allocating a
			// slot; it takes a slot in one of the next periods, because it
			// still has no plugin data.
			return;
		}

		auto& pluginData = slot->data;
		pluginData.midiNote = midiNote;
		pluginData.lastPanning = 0;
		pluginData.lastVelocity = _n->midiVelocity( baseVelocity );
		pluginData.noteOnDone.store( false, std::memory_order_relaxed );
		pluginData.noteOffSent.store( false, std::memory_order_relaxed );
		pluginData.noteOffDone.store( false, std::memory_order_relaxed );
		pluginData.panning = _n->getPanning();

		_n->m_pluginData = slot;

		// hand the note-on to the render stage
		if( !publishNoteEvent( _n, slot, generation, _n->offset(), true ) )
		{
			// A bounded queue that refused the event would leave the slot
			// claimed forever; give it back. The note still has no plugin data,
			// so it tries again in one of the next periods: a full queue
			// delays the note, it never drops it.
			_n->m_pluginData = nullptr;
			releasePluginDataSlot( slot );
			return;
		}
	}
	else if( _n->isReleased() && ! _n->instrumentTrack()->isSustainPedalPressed() ) // note is released during this period
	{
		auto* slot = static_cast<Sf2PluginDataSlot*>(_n->m_pluginData);
		const auto generation = slot->state.load(std::memory_order_acquire) >> 1;

		// hand the note-off to the render stage once
		if( !slot->data.noteOffSent.load( std::memory_order_acquire )
			&& publishNoteEvent( _n, slot, generation, _n->framesBeforeRelease(), false ) )
		{
			slot->data.noteOffSent.store( true, std::memory_order_release );
		}
	}

	// Update the pitch of all the voices
	if (const auto slot = static_cast<Sf2PluginDataSlot*>(_n->m_pluginData)) {
		const auto detuning = _n->currentDetuning();
		// The scale of the microtuner shifts this key away from the equal
		// temperament. FluidSynth's coarse tune counts semitones, so the cents
		// of the offset are divided by 100. This is the same shift that
		// fluid_synth_activate_key_tuning() would make, without its tuning
		// object: the audio thread must not allocate.
		const auto tuning = static_cast<float>( tuningOffsetForKey( slot->data.midiNote ) / 100. );
		for (const auto& voice : slot->data.fluidVoices) {
			if (voice.isValid()) {
				fluid_voice_gen_set(voice.get(), GEN_COARSETUNE, voice.coarseTune() + detuning + tuning);
				fluid_voice_update_param(voice.get(), GEN_COARSETUNE);
			}
		}
	}
}


void Sf2Instrument::noteOn( Sf2PluginData * n )
{
	if( m_synth == nullptr ) { return; }

	// get list of current voice IDs so we can easily spot the new
	// voice after the fluid_synth_noteon() call
	const int poly = fluid_synth_get_polyphony( m_synth );
#ifndef _MSC_VER
	fluid_voice_t* voices[poly];
#else
	const auto voices = static_cast<fluid_voice_t**>(_alloca(poly * sizeof(fluid_voice_t*)));
#endif

	fluid_synth_noteon( m_synth, m_channel, n->midiNote, n->lastVelocity );

	// Get any new voices and store them in the plugin data
	fluid_synth_get_voicelist(m_synth, voices, poly, -1);
	for (int i = 0; i < poly && voices[i] && !n->fluidVoices.full(); ++i)
	{
		const auto voice = voices[i];
		// FluidSynth stops voices with the same channel and pitch upon note-on,
		// so voices with the current channel and pitch are playing this note.
		if (fluid_voice_get_channel(voice) == m_channel
			&& fluid_voice_get_key(voice) == n->midiNote
			&& fluid_voice_is_on(voice)
		) {
			n->fluidVoices.emplace_back(voices[i]);
		}
	}

#if FLUIDSYNTH_VERSION_MAJOR >= 2
	// Smallest balance value that results in full attenuation of one channel.
	// Corresponds to internal FluidSynth macro `FLUID_CB_AMP_SIZE`.
	constexpr static auto maxBalance = 1441.f;
	// Convert panning from linear to exponential for FluidSynth
	const auto panning = n->panning;
	const auto factor = 1.f - std::abs(panning) / static_cast<float>(PanningRight);
	const auto balance = std::copysign(
		factor <= 0 ? maxBalance : std::min(-200.f * std::log10(factor), maxBalance),
		panning
	);
	// Set note panning on all the voices
	for (const auto& voice : n->fluidVoices) {
		if (voice.isValid()) {
			fluid_voice_gen_set(voice.get(), GEN_CUSTOM_BALANCE, balance);
			fluid_voice_update_param(voice.get(), GEN_CUSTOM_BALANCE);
		}
	}
#endif

	// The scale of the microtuner shifts this key away from the equal
	// temperament. The audio thread must not allocate, so the shift goes into
	// the voice's coarse tune (FluidSynth counts semitones there) instead of
	// into a FluidSynth tuning object.
	const auto tuning = static_cast<float>( tuningOffsetForKey( n->midiNote ) / 100. );
	for (const auto& voice : n->fluidVoices) {
		if (voice.isValid()) {
			fluid_voice_gen_set(voice.get(), GEN_COARSETUNE, voice.coarseTune() + tuning);
			fluid_voice_update_param(voice.get(), GEN_COARSETUNE);
		}
	}

	n->noteOnDone.store( true, std::memory_order_release );
	m_notesRunning[ n->midiNote ].fetch_add(1, std::memory_order_relaxed);
}


void Sf2Instrument::noteOff( Sf2PluginData * n )
{
	n->noteOffSent.store( true, std::memory_order_release );
	const int notes = m_notesRunning[ n->midiNote ].fetch_sub(1, std::memory_order_relaxed) - 1;

	if( notes <= 0 && m_synth != nullptr )
	{
		fluid_synth_noteoff( m_synth, m_channel, n->midiNote );
	}
	n->noteOffDone.store( true, std::memory_order_release );
}


void Sf2Instrument::serviceSynthHandoff() noexcept
{
	// A synth that could not be adopted last period because the retire queue was
	// full is retried first. The render thread never waits for the UI thread.
	auto adopt = [this](const Sf2SynthSwap& next) noexcept
	{
		if( next.synth == nullptr ) { return true; }
		if( m_synth != nullptr && !m_retiredSynths.push( m_synth ) ) { return false; }

		m_synth = next.synth;
		m_activeFontId = next.fontId;
		m_internalSampleRate.store( next.sampleRate, std::memory_order_release );
		m_resampler.setRatio( next.sampleRate, Engine::audioEngine()->outputSampleRate() );
		m_bufferView = {};
		// The new synth gets the whole requested state, not just the changes
		// the old synth had already seen.
		resetAppliedControls();

		// The old synth owned the voices the notes referenced. Clearing the
		// references is enough: the old synth is retired whole and the notes
		// continue on the new one.
		for( auto& slot : m_pluginDataSlots )
		{
			slot.data.fluidVoices.clear();
		}
		return true;
	};

	if( m_deferredSynth.synth != nullptr )
	{
		if( !adopt( m_deferredSynth ) ) { return; }
		m_deferredSynth = {};
	}

	Sf2SynthSwap next;
	while( m_pendingSynths.pop( next ) )
	{
		if( next.synth == nullptr ) { continue; }
		if( !adopt( next ) )
		{
			m_deferredSynth = next;
			return;
		}
	}
}



void Sf2Instrument::resetAppliedControls() noexcept
{
	m_appliedGain = -1.0f;
	m_appliedReverbOn = -1;
	m_appliedReverbRoomSize = -1.0f;
	m_appliedReverbDamping = -1.0f;
	m_appliedReverbWidth = -1.0f;
	m_appliedReverbLevel = -1.0f;
	m_appliedChorusOn = -1;
	m_appliedChorusNum = -1.0f;
	m_appliedChorusLevel = -1.0f;
	m_appliedChorusSpeed = -1.0f;
	m_appliedChorusDepth = -1.0f;
	m_appliedBank = -1;
	m_appliedPatch = -1;
	m_appliedFontId = -2;
}



void Sf2Instrument::applyControls() noexcept
{
	if( m_synth == nullptr ) { return; }

	// The synth runs with FluidSynth's thread-safe API switched off, so none of
	// these calls takes a mutex: the render thread never waits for another
	// thread, and the UI thread never calls into this synth at all.
	const float gain = m_requestedGain.load( std::memory_order_relaxed );
	if( gain != m_appliedGain )
	{
		fluid_synth_set_gain( m_synth, gain );
		m_appliedGain = gain;
	}

	// The effect state is sent even when it is "off": FluidSynth's own default
	// is "on", so a synth that never got the state would play with the effects.
	const int reverbOn = m_requestedReverbOn.load( std::memory_order_relaxed );
	if( reverbOn != m_appliedReverbOn )
	{
#if USE_NEW_EFFECT_API
		fluid_synth_reverb_on( m_synth, -1, reverbOn );
#else
		fluid_synth_set_reverb_on( m_synth, reverbOn );
#endif
		m_appliedReverbOn = reverbOn;
	}

	const float roomSize = m_requestedReverbRoomSize.load( std::memory_order_relaxed );
	const float damping = m_requestedReverbDamping.load( std::memory_order_relaxed );
	const float width = m_requestedReverbWidth.load( std::memory_order_relaxed );
	const float level = m_requestedReverbLevel.load( std::memory_order_relaxed );
	if( roomSize != m_appliedReverbRoomSize || damping != m_appliedReverbDamping
		|| width != m_appliedReverbWidth || level != m_appliedReverbLevel )
	{
#if USE_NEW_EFFECT_API
		fluid_synth_set_reverb_group_roomsize( m_synth, -1, roomSize );
		fluid_synth_set_reverb_group_damp( m_synth, -1, damping );
		fluid_synth_set_reverb_group_width( m_synth, -1, width );
		fluid_synth_set_reverb_group_level( m_synth, -1, level );
#else
		fluid_synth_set_reverb( m_synth, roomSize, damping, width, level );
#endif
		m_appliedReverbRoomSize = roomSize;
		m_appliedReverbDamping = damping;
		m_appliedReverbWidth = width;
		m_appliedReverbLevel = level;
	}

	const int chorusOn = m_requestedChorusOn.load( std::memory_order_relaxed );
	if( chorusOn != m_appliedChorusOn )
	{
#if USE_NEW_EFFECT_API
		fluid_synth_chorus_on( m_synth, -1, chorusOn );
#else
		fluid_synth_set_chorus_on( m_synth, chorusOn );
#endif
		m_appliedChorusOn = chorusOn;
	}

	const float chorusNum = m_requestedChorusNum.load( std::memory_order_relaxed );
	const float chorusLevel = m_requestedChorusLevel.load( std::memory_order_relaxed );
	const float chorusSpeed = m_requestedChorusSpeed.load( std::memory_order_relaxed );
	const float chorusDepth = m_requestedChorusDepth.load( std::memory_order_relaxed );
	if( chorusNum != m_appliedChorusNum || chorusLevel != m_appliedChorusLevel
		|| chorusSpeed != m_appliedChorusSpeed || chorusDepth != m_appliedChorusDepth )
	{
#if USE_NEW_EFFECT_API
		fluid_synth_set_chorus_group_nr( m_synth, -1, static_cast<int>( chorusNum ) );
		fluid_synth_set_chorus_group_level( m_synth, -1, chorusLevel );
		fluid_synth_set_chorus_group_speed( m_synth, -1, chorusSpeed );
		fluid_synth_set_chorus_group_depth( m_synth, -1, chorusDepth );
		fluid_synth_set_chorus_group_type( m_synth, -1, FLUID_CHORUS_MOD_SINE );
#else
		fluid_synth_set_chorus( m_synth, static_cast<int>( chorusNum ), chorusLevel,
			chorusSpeed, chorusDepth, FLUID_CHORUS_MOD_SINE );
#endif
		m_appliedChorusNum = chorusNum;
		m_appliedChorusLevel = chorusLevel;
		m_appliedChorusSpeed = chorusSpeed;
		m_appliedChorusDepth = chorusDepth;
	}

	const int bank = m_requestedBank.load( std::memory_order_relaxed );
	const int patch = m_requestedPatch.load( std::memory_order_relaxed );
	const int fontId = m_activeFontId;
	if( fontId >= 0 && ( bank != m_appliedBank || patch != m_appliedPatch
		|| fontId != m_appliedFontId ) )
	{
		fluid_synth_program_select( m_synth, m_channel, fontId, bank, patch );
		m_appliedBank = bank;
		m_appliedPatch = patch;
		m_appliedFontId = fontId;
	}
}



void Sf2Instrument::playImpl(std::span<SampleFrame> out)
{
	serviceSynthHandoff();
	if( m_synth == nullptr )
	{
		std::fill_n( out.data(), out.size(), SampleFrame{} );
		applyPendingTeardownNoteOffs();
		return;
	}
	applyControls();

	// set midi pitch for this period
	const int currentMidiPitch = instrumentTrack()->midiPitch();
	if( m_lastMidiPitch != currentMidiPitch )
	{
		m_lastMidiPitch = currentMidiPitch;
		fluid_synth_pitch_bend( m_synth, m_channel, m_lastMidiPitch );
	}

	const int currentMidiPitchRange = instrumentTrack()->midiPitchRange();
	if( m_lastMidiPitchRange != currentMidiPitchRange )
	{
		m_lastMidiPitchRange = currentMidiPitchRange;
		fluid_synth_pitch_wheel_sens( m_synth, m_channel, m_lastMidiPitchRange );
	}

	// Take over the note events the note stage queued for this period. The
	// queue is bounded and preallocated: draining it neither allocates nor
	// waits, and the events are re-ordered by their offset below, so the order
	// the pushes happened in does not matter.
	m_pendingEventCount = 0;
	Sf2NoteEvent event;
	while( m_pendingEventCount < m_pendingEvents.size() && m_noteEvents.pop( event ) )
	{
		m_pendingEvents[ m_pendingEventCount++ ] = event;
	}

	// if we have no new noteons/noteoffs, just render a period and call it a day
	if( m_pendingEventCount == 0 )
	{
		renderFrames(out.size(), out.data());
		applyPendingTeardownNoteOffs();
		return;
	}

	// processing loop
	// go through the note events in processing order
	f_cnt_t currentFrame = 0;

	while( m_pendingEventCount > 0 )
	{
		// find the event with lowest offset
		std::size_t currentIndex = 0;
		for( std::size_t i = 1; i < m_pendingEventCount; ++i )
		{
			if( m_pendingEvents[i].offset < m_pendingEvents[currentIndex].offset )
			{
				currentIndex = i;
			}
		}

		Sf2NoteEvent& currentEvent = m_pendingEvents[currentIndex];

		// A note that was deleted before this period rendered has no event left
		// to process: its slot was given back, so the generation does not match
		// any more. The event is dropped without touching the dead note.
		if( !currentEvent.standalone )
		{
			const auto state = currentEvent.slot->state.load( std::memory_order_acquire );
			if( ( state & 1u ) == 0u || ( state >> 1 ) != currentEvent.generation )
			{
				removePendingEvent( currentIndex );
				continue;
			}
		}

		// A teardown note-off has no live handle and runs at the end of the
		// period, after any note-on queued for the same pitch.
		const f_cnt_t eventOffset = std::min<f_cnt_t>( currentEvent.offset, static_cast<f_cnt_t>( out.size() ) ); // one type: f_cnt_t is not size_t on macOS
		if( eventOffset > currentFrame )
		{
			renderFrames(eventOffset - currentFrame, out.data() + currentFrame);
			currentFrame = eventOffset;
		}

		if( currentEvent.isNew )
		{
			noteOn( &currentEvent.slot->data );
			if (currentEvent.handle != nullptr && currentEvent.handle->isReleased()) // if the note is released during the same period, we have to process it again for noteoff
			{
				currentEvent.isNew = false;
				currentEvent.offset = currentEvent.handle->framesBeforeRelease();
			}
			else // otherwise remove the event
			{
				removePendingEvent( currentIndex );
			}
		}
		else if( currentEvent.standalone )
		{
			const int notes = m_notesRunning[ currentEvent.midiNote ].fetch_sub(1, std::memory_order_relaxed) - 1;
			if( notes <= 0 && m_synth != nullptr )
			{
				fluid_synth_noteoff( m_synth, m_channel, currentEvent.midiNote );
			}
			removePendingEvent( currentIndex );
		}
		else
		{
			noteOff( &currentEvent.slot->data );
			removePendingEvent( currentIndex );
		}
	}

	if (currentFrame < out.size())
	{
		renderFrames(out.size() - currentFrame, out.data() + currentFrame);
	}

	// The teardown note-offs the bounded queue refused are applied at the end
	// of the period, like the ones it did take: a note-on of the same pitch in
	// this period is not cut by them.
	applyPendingTeardownNoteOffs();
}


void Sf2Instrument::removePendingEvent(std::size_t index) noexcept
{
	// The events keep their order: the loop above picks the first event with
	// the lowest offset, so two events at the same offset are processed in the
	// order they were queued.
	for( std::size_t i = index; i + 1 < m_pendingEventCount; ++i )
	{
		m_pendingEvents[i] = m_pendingEvents[i + 1];
	}
	--m_pendingEventCount;
}




void Sf2Instrument::applyPendingTeardownNoteOffs() noexcept
{
	// The flag is cleared before the counters are read. A teardown that is
	// added while this runs sets the flag again, and the next period takes it.
	if( !m_pendingTeardownDirty.exchange( false, std::memory_order_acq_rel ) ) { return; }

	for( int pitch = 0; pitch < 128; ++pitch )
	{
		const int pending = m_pendingTeardownNotes[ pitch ].exchange( 0, std::memory_order_acq_rel );
		if( pending <= 0 ) { continue; }
		// The same count the note-off event would have taken: the note is no
		// longer running, and the synth stops the voice when the last note of
		// the pitch is gone.
		const int notes = m_notesRunning[ pitch ].fetch_sub( pending, std::memory_order_relaxed ) - pending;
		if( notes <= 0 && m_synth != nullptr )
		{
			fluid_synth_noteoff( m_synth, m_channel, pitch );
		}
	}
}


void Sf2Instrument::renderFrames( f_cnt_t frames, SampleFrame* buf )
{
	if( m_synth == nullptr )
	{
		std::fill_n( buf, frames, SampleFrame{} );
		return;
	}

	fluid_synth_get_gain(m_synth); // This flushes voice updates as a side effect

	if (m_internalSampleRate.load(std::memory_order_relaxed) == Engine::audioEngine()->outputSampleRate()) {
		fluid_synth_write_float(m_synth, frames, buf, 0, 2, buf, 1, 2);
		return;
	}

	// TODO: These kind of playback pipelines/graphs are repeated within other parts of the codebase that work with
	// audio samples. We should find a way to unify this but the right abstraction is not so clear yet.
	while (frames > 0)
	{
		if (m_bufferView.empty())
		{
			fluid_synth_write_float(m_synth, m_buffer.size(), m_buffer.data(), 0, 2, m_buffer.data(), 1, 2);
			m_bufferView = m_buffer;
		}

		const auto [inputFramesUsed, outputFramesGenerated]
			= m_resampler.process({&m_bufferView.data()[0][0], 2, m_bufferView.size()}, {&buf[0][0], 2, frames});

		if (inputFramesUsed == 0 && outputFramesGenerated == 0)
		{
			std::fill_n(buf, frames, SampleFrame{});
			break;
		}

		m_bufferView = m_bufferView.subspan(inputFramesUsed);
		buf += outputFramesGenerated;
		frames -= outputFramesGenerated;
	}
}




void Sf2Instrument::deleteNotePluginData( NotePlayHandle * _n )
{
	auto* slot = static_cast<Sf2PluginDataSlot*>(_n->m_pluginData);
	if( slot == nullptr ) { return; }

	auto* pluginData = &slot->data;
	// A note that was started and whose note-off has not been processed yet gets
	// a standalone note-off event: it names the pitch, not the note handle, so
	// it stays valid while the handle is being destroyed. The slot itself is
	// released immediately and any event still naming its generation is dropped.
	if( pluginData->noteOnDone.load( std::memory_order_acquire )
		&& !pluginData->noteOffDone.load( std::memory_order_acquire ) )
	{
		pluginData->noteOffSent.store( true, std::memory_order_release );
		Sf2NoteEvent event;
		event.offset = std::numeric_limits<f_cnt_t>::max();
		event.midiNote = pluginData->midiNote;
		event.isNew = false;
		event.standalone = true;
		if( !m_noteEvents.push( event ) )
		{
			// The bounded queue was full and refused the note-off. Dropping it
			// would leave the voice sounding and the note counted for ever,
			// so the pitch's teardown counter takes it instead: the render
			// stage applies the count, and a started note always stops. The
			// counter has a fixed size, so this path neither allocates nor
			// waits for anything.
			m_pendingTeardownNotes[ pluginData->midiNote ].fetch_add( 1, std::memory_order_relaxed );
			// The flag is set after the counter, so the render stage that sees
			// the flag also sees the count.
			m_pendingTeardownDirty.store( true, std::memory_order_release );
		}
	}

	// Give the slot back to the pool. A note event that is still queued for
	// this note names the old generation, so the render stage drops it instead
	// of touching the dead note.
	releasePluginDataSlot( slot );
	_n->m_pluginData = nullptr;
}




gui::PluginView * Sf2Instrument::instantiateView( QWidget * _parent )
{
	return new gui::Sf2InstrumentView( this, _parent );
}




namespace gui
{


class Sf2Knob : public Knob
{
public:
	Sf2Knob( QWidget * _parent ) :
			Knob( KnobType::Styled, _parent )
	{
		setFixedSize( 31, 38 );
	}
};



Sf2InstrumentView::Sf2InstrumentView( Instrument * _instrument, QWidget * _parent ) :
	InstrumentViewFixedSize( _instrument, _parent )
{
//	QVBoxLayout * vl = new QVBoxLayout( this );
//	QHBoxLayout * hl = new QHBoxLayout();

	auto k = castModel<Sf2Instrument>();

	connect(&k->m_bankNum, SIGNAL(dataChanged()), this, SLOT(updatePatchName()));
	connect(&k->m_patchNum, SIGNAL(dataChanged()), this, SLOT(updatePatchName()));

	// File Button
	m_fileDialogButton = new PixmapButton(this);
	m_fileDialogButton->setCursor(Qt::PointingHandCursor);
	m_fileDialogButton->setActiveGraphic(PLUGIN_NAME::getIconPixmap("fileselect_on"));
	m_fileDialogButton->setInactiveGraphic(PLUGIN_NAME::getIconPixmap("fileselect_off"));
	m_fileDialogButton->move(217, 107);

	connect(m_fileDialogButton, SIGNAL(clicked()), this, SLOT(showFileDialog()));

	m_fileDialogButton->setToolTip(tr("Open SoundFont file"));

	// Patch Button
	m_patchDialogButton = new PixmapButton(this);
	m_patchDialogButton->setCursor(Qt::PointingHandCursor);
	m_patchDialogButton->setActiveGraphic(PLUGIN_NAME::getIconPixmap("patches_on"));
	m_patchDialogButton->setInactiveGraphic(PLUGIN_NAME::getIconPixmap("patches_off"));
	m_patchDialogButton->setEnabled(false);
	m_patchDialogButton->move(217, 125);

	connect(m_patchDialogButton, SIGNAL(clicked()), this, SLOT(showPatchDialog()));

	m_patchDialogButton->setToolTip(tr("Choose patch"));

	// LCDs
	m_bankNumLcd = new LcdSpinBox(3, "21pink", this);
	m_bankNumLcd->move(131, 62);
//	m_bankNumLcd->addTextForValue( -1, "---" );
//	m_bankNumLcd->setEnabled( false );

	m_patchNumLcd = new LcdSpinBox( 3, "21pink", this );
	m_patchNumLcd->move(190, 62);
//	m_patchNumLcd->addTextForValue( -1, "---" );
//	m_patchNumLcd->setEnabled( false );

	/*hl->addWidget( m_fileDialogButton );
	hl->addWidget( m_bankNumLcd );
	hl->addWidget( m_patchNumLcd );
	hl->addWidget( m_patchDialogButton );

	vl->addLayout( hl );*/

	// Next row

	//hl = new QHBoxLayout();

	m_filenameLabel = new QLabel( this );
	m_filenameLabel->setGeometry( 58, 109, 156, 11 );
	m_patchLabel = new QLabel( this );
	m_patchLabel->setGeometry( 58, 127, 156, 11 );

	//hl->addWidget( m_filenameLabel );
//	vl->addLayout( hl );

	// Gain
	m_gainKnob = new Sf2Knob( this );
	m_gainKnob->setHintText( tr("Gain:"), "" );
	m_gainKnob->move( 86, 55 );
//	vl->addWidget( m_gainKnob );

	// Reverb
//	hl = new QHBoxLayout();


	m_reverbButton = new PixmapButton( this );
	m_reverbButton->setCheckable( true );
	m_reverbButton->move( 14, 180 );
	m_reverbButton->setActiveGraphic( PLUGIN_NAME::getIconPixmap( "reverb_on" ) );
	m_reverbButton->setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "reverb_off" ) );
	m_reverbButton->setToolTip(tr("Apply reverb (if supported)"));


	m_reverbRoomSizeKnob = new Sf2Knob( this );
	m_reverbRoomSizeKnob->setHintText( tr("Room size:"), "" );
	m_reverbRoomSizeKnob->move( 93, 160 );

	m_reverbDampingKnob = new Sf2Knob( this );
	m_reverbDampingKnob->setHintText( tr("Damping:"), "" );
	m_reverbDampingKnob->move( 130, 160 );

	m_reverbWidthKnob = new Sf2Knob( this );
	m_reverbWidthKnob->setHintText( tr("Width:"), "" );
	m_reverbWidthKnob->move( 167, 160 );

	m_reverbLevelKnob = new Sf2Knob( this );
	m_reverbLevelKnob->setHintText( tr("Level:"), "" );
	m_reverbLevelKnob->move( 204, 160 );

/*	hl->addWidget( m_reverbOnLed );
	hl->addWidget( m_reverbRoomSizeKnob );
	hl->addWidget( m_reverbDampingKnob );
	hl->addWidget( m_reverbWidthKnob );
	hl->addWidget( m_reverbLevelKnob );

	vl->addLayout( hl );
*/

	// Chorus
//	hl = new QHBoxLayout();

	m_chorusButton = new PixmapButton( this );
	m_chorusButton->setCheckable( true );
	m_chorusButton->move( 14, 226 );
	m_chorusButton->setActiveGraphic( PLUGIN_NAME::getIconPixmap( "chorus_on" ) );
	m_chorusButton->setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "chorus_off" ) );
	m_chorusButton->setToolTip(tr("Apply chorus (if supported)"));

	m_chorusNumKnob = new Sf2Knob( this );
	m_chorusNumKnob->setHintText( tr("Voices:"), "" );
	m_chorusNumKnob->move( 93, 206 );

	m_chorusLevelKnob = new Sf2Knob( this );
	m_chorusLevelKnob->setHintText( tr("Level:"), "" );
	m_chorusLevelKnob->move( 130 , 206 );

	m_chorusSpeedKnob = new Sf2Knob( this );
	m_chorusSpeedKnob->setHintText( tr("Speed:"), "" );
	m_chorusSpeedKnob->move( 167 , 206 );

	m_chorusDepthKnob = new Sf2Knob( this );
	m_chorusDepthKnob->setHintText( tr("Depth:"), "" );
	m_chorusDepthKnob->move( 204 , 206 );
/*
	hl->addWidget( m_chorusOnLed );
	hl->addWidget( m_chorusNumKnob);
	hl->addWidget( m_chorusLevelKnob);
	hl->addWidget( m_chorusSpeedKnob);
	hl->addWidget( m_chorusDepthKnob);

	vl->addLayout( hl );
*/
	setAutoFillBackground( true );
	QPalette pal;
	pal.setBrush( backgroundRole(), PLUGIN_NAME::getIconPixmap( "artwork" ) );
	setPalette( pal );

	updateFilename();
}




void Sf2InstrumentView::modelChanged()
{
	auto k = castModel<Sf2Instrument>();
	m_bankNumLcd->setModel( &k->m_bankNum );
	m_patchNumLcd->setModel( &k->m_patchNum );

	m_gainKnob->setModel( &k->m_gain );

	m_reverbButton->setModel( &k->m_reverbOn );
	m_reverbRoomSizeKnob->setModel( &k->m_reverbRoomSize );
	m_reverbDampingKnob->setModel( &k->m_reverbDamping );
	m_reverbWidthKnob->setModel( &k->m_reverbWidth );
	m_reverbLevelKnob->setModel( &k->m_reverbLevel );

	m_chorusButton->setModel( &k->m_chorusOn );
	m_chorusNumKnob->setModel( &k->m_chorusNum );
	m_chorusLevelKnob->setModel( &k->m_chorusLevel );
	m_chorusSpeedKnob->setModel( &k->m_chorusSpeed );
	m_chorusDepthKnob->setModel( &k->m_chorusDepth );


	connect( k, SIGNAL( fileChanged() ), this, SLOT( updateFilename() ) );

	connect( k, SIGNAL( fileLoading() ), this, SLOT( invalidateFile() ) );

	updateFilename();
}




void Sf2InstrumentView::updateFilename()
{
	auto i = castModel<Sf2Instrument>();
	QFontMetrics fm( m_filenameLabel->font() );
	QString file = i->m_filename.endsWith( ".sf2", Qt::CaseInsensitive ) ?
			i->m_filename.left( i->m_filename.length() - 4 ) :
			i->m_filename;
	m_filenameLabel->setText( fm.elidedText( file, Qt::ElideLeft, m_filenameLabel->width() ) );
			//		i->m_filename + "\nPatch: TODO" );

	m_patchDialogButton->setEnabled( !i->m_filename.isEmpty() );

	updatePatchName();

	update();
}




void Sf2InstrumentView::updatePatchName()
{
	auto i = castModel<Sf2Instrument>();
	QFontMetrics fm( font() );
	QString patch = i->getCurrentPatchName();
	m_patchLabel->setText( fm.elidedText( patch, Qt::ElideLeft, m_patchLabel->width() ) );


	update();
}




void Sf2InstrumentView::invalidateFile()
{
	auto i = castModel<Sf2Instrument>();
	m_patchDialogButton->setEnabled( !i->m_filename.isEmpty() );
}




void Sf2InstrumentView::showFileDialog()
{
	auto k = castModel<Sf2Instrument>();

	FileDialog ofd( nullptr, tr( "Open SoundFont file" ) );
	ofd.setFileMode( FileDialog::ExistingFiles );

	QStringList types;
	types << tr( "SoundFont Files (*.sf2 *.sf3)" );
	ofd.setNameFilters( types );

	if( k->m_filename != "" )
	{
		QString f = PathUtil::toAbsolute( k->m_filename );
		ofd.setDirectory( QFileInfo( f ).absolutePath() );
		ofd.selectFile( QFileInfo( f ).fileName() );
	}
	else
	{
		ofd.setDirectory( ConfigManager::inst()->sf2Dir() );
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




void Sf2InstrumentView::showPatchDialog()
{
	auto k = castModel<Sf2Instrument>();

	// The dialog enumerates (and previews) a private synth loaded from the
	// instrument's own copy of the file, never the synth the render thread is
	// playing. It has a settings object of its own, like every other synth.
	auto dialogSynth = k->createPatchDialogSynth();
	if( dialogSynth.synth == nullptr ) { return; }

	PatchesDialog pd( this );

	pd.setup( dialogSynth.synth, 1, k->instrumentTrack()->name(), &k->m_bankNum, &k->m_patchNum, m_patchLabel );

	pd.exec();
	k->destroySynth( dialogSynth.synth );
}


} // namespace gui

extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model *m, void * )
{
	return new Sf2Instrument( static_cast<InstrumentTrack *>( m ) );
}
}


} // namespace lmms
