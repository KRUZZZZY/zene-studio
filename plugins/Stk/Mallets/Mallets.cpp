/*
 * Mallets.cpp - tuned instruments that one would bang upon
 *
 * Copyright (c) 2006-2008 Danny McRae <khjklujn/at/users.sourceforge.net>
 * Copyright (c) 2009-2015 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2016 Oskar Wallgren <oskarwallgren13/at/gmail.com>
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

#include "Mallets.h"

#include <QDir>
#include <QDomElement>
#include <QMessageBox>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <stk/BandedWG.h>
#include <stk/ModalBar.h>
#include <stk/TubeBell.h>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "FontHelper.h"
#include "GuiApplication.h"
#include "InstrumentTrack.h"
#include "PlayHandle.h"

#include "embed.h"
#include "lmms_math.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT malletsstk_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"Mallets",
	QT_TRANSLATE_NOOP( "PluginBrowser",
				"Tuneful things to bang on" ),
	"Danny McRae <khjklujn/at/users.sf.net>",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader( "logo" ),
	nullptr,
	nullptr,
} ;

}


namespace
{

//! How many notes one Mallets instrument voices at the same time. This is the
//! product limit of the instrument: the pool of prepared voices is sized
//! after it, so the limit is enforced by the instrument itself and no other
//! instrument can take its voices away.
constexpr std::size_t MALLETS_VOICE_LIMIT = 128;

//! The number of STK families a prepared voice pool holds a voice for. The
//! order matches MalletsSynth::Family.
constexpr std::size_t MALLETS_FAMILY_COUNT =
	static_cast<std::size_t>( MalletsSynth::Family::BandedWG ) + 1;

//! Reserves one note of the instrument's own voice budget. The budget is
//! shared by the current pool and every retired pool, so notes that keep
//! rendering through a retired pool still count against the product limit.
//! Lock-free: a compare-exchange, never a wait.
bool acquireVoiceBudget( std::atomic<int>& _voicesInUse ) noexcept
{
	int used = _voicesInUse.load( std::memory_order_relaxed );
	while( used < static_cast<int>( MALLETS_VOICE_LIMIT ) )
	{
		if( _voicesInUse.compare_exchange_weak( used, used + 1,
				std::memory_order_acquire, std::memory_order_relaxed ) )
		{
			return true;
		}
	}
	return false;
}

//! Gives one note of the budget back when its voice is released.
void releaseVoiceBudget( std::atomic<int>& _voicesInUse ) noexcept
{
	_voicesInUse.fetch_sub( 1, std::memory_order_release );
}

} // namespace


//! One prepared voice per STK family plus the claim state of the slot. The
//! voices are built once, on the constructing thread, so the render thread
//! only resets and arms the voice of the family its note belongs to.
struct MalletsVoiceSlot
{
	MalletsSynth voices[MALLETS_FAMILY_COUNT];
	std::atomic<bool> inUse{false};
	std::atomic<int> family{0};
};


/*! The prepared voices of one Mallets instrument.

	The voices, their delay buffers and the one-time STK global setup are
	created here, off the audio thread; the render thread only claims a slot
	with a non-blocking compare-exchange, arms the prepared voice in place and
	gives the slot back when the note ends. No allocation and no lock runs on
	the render thread.

	An instrument voices at most MALLETS_VOICE_LIMIT notes at the same time. The
	limit is one budget shared by the current pool and every retired pool of
	the instrument, so a device change during sustained notes cannot voice a
	second pool's worth of them. A note that starts while every voice is busy
	is not dropped: it stays silent and takes a voice as soon as one of the
	running notes ends.
*/
class MalletsVoicePool
{
public:
	explicit MalletsVoicePool( sample_rate_t _sample_rate ) :
		m_slots( std::make_unique<MalletsVoiceSlot[]>( MALLETS_VOICE_LIMIT ) )
	{
		for( std::size_t i = 0; i < MALLETS_VOICE_LIMIT; ++i )
		{
			m_slots[i].voices[static_cast<std::size_t>(MalletsSynth::Family::ModalBar)]
				.init( MalletsSynth::Family::ModalBar, _sample_rate );
			m_slots[i].voices[static_cast<std::size_t>(MalletsSynth::Family::TubeBell)]
				.init( MalletsSynth::Family::TubeBell, _sample_rate );
			m_slots[i].voices[static_cast<std::size_t>(MalletsSynth::Family::BandedWG)]
				.init( MalletsSynth::Family::BandedWG, _sample_rate );
		}
	}

	//! Claims a prepared voice of the given family, or returns nullptr when
	//! every voice of the instrument is busy.
	MalletsVoiceSlot* claim( MalletsSynth::Family _family ) noexcept
	{
		MalletsVoiceSlot* slot = findFreeSlot();
		if( slot == nullptr ) { return nullptr; }
		// The family is published before the note uses the voice.
		slot->family.store( static_cast<int>( _family ), std::memory_order_release );
		return slot;
	}

	//! Gives the voice of a finished note back to the pool.
	static void release( MalletsVoiceSlot* _slot ) noexcept
	{
		if( _slot == nullptr ) { return; }
		_slot->inUse.store( false, std::memory_order_release );
	}

private:
	//! An unused slot of the pool.
	MalletsVoiceSlot* findFreeSlot() noexcept
	{
		for( std::size_t i = 0; i < MALLETS_VOICE_LIMIT; ++i )
		{
			bool expected = false;
			if( m_slots[i].inUse.compare_exchange_strong( expected, true,
					std::memory_order_acquire, std::memory_order_relaxed ) )
			{
				return &m_slots[i];
			}
		}
		return nullptr;
	}

	std::unique_ptr<MalletsVoiceSlot[]> m_slots;
};


namespace
{

//! The STK family a preset belongs to.
MalletsSynth::Family familyForPreset( const int _preset )
{
	if( _preset < 9 ) { return MalletsSynth::Family::ModalBar; }
	if( _preset == 9 ) { return MalletsSynth::Family::TubeBell; }
	return MalletsSynth::Family::BandedWG;
}

} // namespace


MalletsInstrument::MalletsInstrument( InstrumentTrack * _instrument_track ):
	Instrument( _instrument_track, &malletsstk_plugin_descriptor ),
	m_voicePool( std::make_unique<MalletsVoicePool>( Engine::audioEngine()->outputSampleRate() ) ),
	m_hardnessModel(64.0f, 0.0f, 128.0f, 0.1f, this, tr( "Hardness" )),
	m_positionModel(64.0f, 0.0f, 64.0f, 0.1f, this, tr( "Position" )),
	m_vibratoGainModel(0.0f, 0.0f, 128.0f, 0.1f, this, tr( "Vibrato gain" )),
	m_vibratoFreqModel(0.0f, 0.0f, 128.0f, 0.1f, this, tr( "Vibrato frequency" )),
	m_stickModel(0.0f, 0.0f, 128.0f, 0.1f, this, tr( "Stick mix" )),
	m_modulatorModel(64.0f, 0.0f, 128.0f, 0.1f, this, tr( "Modulator" )),
	m_crossfadeModel(64.0f, 0.0f, 128.0f, 0.1f, this, tr( "Crossfade" )),
	m_lfoSpeedModel(64.0f, 0.0f, 128.0f, 0.1f, this, tr( "LFO speed" )),
	m_lfoDepthModel(64.0f, 0.0f, 128.0f, 0.1f, this, tr( "LFO depth" )),
	m_adsrModel(64.0f, 0.0f, 128.0f, 0.1f, this, tr( "ADSR" )),
	m_pressureModel(64.0f, 0.1f, 128.0f, 0.1f, this, tr( "Pressure" )),
	m_motionModel(64.0f, 0.0f, 128.0f, 0.1f, this, tr( "Motion" )),
//	TODO: m_vibratoModel
	m_velocityModel(64.0f, 0.1f, 128.0f, 0.1f, this, tr( "Speed" )),
	m_strikeModel( true, this, tr( "Bowed" ) ),
	m_presetsModel(this, tr("Instrument")),
	m_spreadModel(0, 0, 255, 1, this, tr( "Spread" )),
	m_randomModel(0.0f, 0.0f, 1.0f, 0.01f, this, tr("Randomness")),
	m_versionModel( MALLETS_PRESET_VERSION, 0, MALLETS_PRESET_VERSION, this, "" ),
	m_isOldVersionModel( false, this, "" ),
	m_filesMissing( !QDir( ConfigManager::inst()->stkDir() ).exists() ||
		!QFileInfo( ConfigManager::inst()->stkDir() + "/sinewave.raw" ).exists() )
{
	// ModalBar
	m_presetsModel.addItem( tr( "Marimba" ) );
	m_scalers.append( 4.0 );
	m_presetsModel.addItem( tr( "Vibraphone" ) );
	m_scalers.append( 4.0 );
	m_presetsModel.addItem( tr( "Agogo" ) );
	m_scalers.append( 5.0 );
	m_presetsModel.addItem( tr( "Wood 1" ) );
	m_scalers.append( 4.0 );
	m_presetsModel.addItem( tr( "Reso" ) );
	m_scalers.append( 2.5 );
	m_presetsModel.addItem( tr( "Wood 2" ) );
	m_scalers.append( 5.0 );
	m_presetsModel.addItem( tr( "Beats" ) );
	m_scalers.append( 20.0 );
	m_presetsModel.addItem( tr( "Two fixed" ) );
	m_scalers.append( 5.0 );
	m_presetsModel.addItem( tr( "Clump" ) );
	m_scalers.append( 4.0 );
	
	// TubeBell
	m_presetsModel.addItem( tr( "Tubular bells" ) );
	m_scalers.append(1.8f);
	
	// BandedWG
	m_presetsModel.addItem( tr( "Uniform bar" ) );
	m_scalers.append( 25.0 );
	m_presetsModel.addItem( tr( "Tuned bar" ) );
	m_scalers.append( 10.0 );
	m_presetsModel.addItem( tr( "Glass" ) );
	m_scalers.append( 16.0 );
	m_presetsModel.addItem( tr( "Tibetan bowl" ) );
	m_scalers.append( 7.0 );

	// Prepared voices are voiced for one sample rate; a device change rebuilds
	// the pool off the audio thread. The pool of the old rate stays alive
	// while notes still hold a voice of it.
	QObject::connect( Engine::audioEngine(), &AudioEngine::sampleRateChanged, this, [this]() {
		auto pool = std::make_unique<MalletsVoicePool>( Engine::audioEngine()->outputSampleRate() );
		m_retiredPools.push_back( std::move( m_voicePool ) );
		m_voicePool = std::move( pool );
	} );
}




//! The prepared voices and the pools of earlier sample rates are freed here,
//! on the control thread. A note that outlives its instrument already reaches
//! a deleted instrument in the engine (InstrumentTrack's destructor deletes
//! the instrument while the engine still owns the note handles), so the pool
//! is freed with the instrument it belongs to.
MalletsInstrument::~MalletsInstrument() = default;




void MalletsInstrument::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	// ModalBar
	m_hardnessModel.saveSettings( _doc, _this, "hardness" );
	m_positionModel.saveSettings( _doc, _this, "position" );
	m_vibratoGainModel.saveSettings( _doc, _this, "vib_gain" );
	m_vibratoFreqModel.saveSettings( _doc, _this, "vib_freq" );
	m_stickModel.saveSettings( _doc, _this, "stick_mix" );

	// TubeBell
	m_modulatorModel.saveSettings( _doc, _this, "modulator" );
	m_crossfadeModel.saveSettings( _doc, _this, "crossfade" );
	m_lfoSpeedModel.saveSettings( _doc, _this, "lfo_speed" );
	m_lfoDepthModel.saveSettings( _doc, _this, "lfo_depth" );
	m_adsrModel.saveSettings( _doc, _this, "adsr" );
	
	// BandedWG
	m_pressureModel.saveSettings( _doc, _this, "pressure" );
//	m_motionModel.saveSettings( _doc, _this, "motion" );
//	m_vibratoModel.saveSettings( _doc, _this, "vibrato" );
	m_velocityModel.saveSettings( _doc, _this, "velocity" );
	m_strikeModel.saveSettings( _doc, _this, "strike" );

	m_presetsModel.saveSettings( _doc, _this, "preset" );
	m_spreadModel.saveSettings( _doc, _this, "spread" );
	m_randomModel.saveSettings(_doc, _this, "randomness");
	m_versionModel.saveSettings( _doc, _this, "version" );
	m_isOldVersionModel.saveSettings( _doc, _this, "oldversion" );
}




void MalletsInstrument::loadSettings( const QDomElement & _this )
{
	m_versionModel.loadSettings( _this, "version" );

	// ModalBar
	m_hardnessModel.loadSettings( _this, "hardness" );
	m_positionModel.loadSettings( _this, "position" );
	m_vibratoGainModel.loadSettings( _this, "vib_gain" );
	m_vibratoFreqModel.loadSettings( _this, "vib_freq" );
	m_stickModel.loadSettings( _this, "stick_mix" );

	// TubeBell
	m_modulatorModel.loadSettings( _this, "modulator" );
	m_crossfadeModel.loadSettings( _this, "crossfade" );
	m_lfoSpeedModel.loadSettings( _this, "lfo_speed" );
	m_lfoDepthModel.loadSettings( _this, "lfo_depth" );
	m_adsrModel.loadSettings( _this, "adsr" );
	
	// BandedWG
	m_pressureModel.loadSettings( _this, "pressure" );
//	m_motionModel.loadSettings( _this, "motion" );
//	m_vibratoModel.loadSettings( _this, "vibrato" );
	m_velocityModel.loadSettings( _this, "velocity" );
	m_strikeModel.loadSettings( _this, "strike" );

	m_presetsModel.loadSettings( _this, "preset" );
	m_spreadModel.loadSettings( _this, "spread" );
	m_randomModel.loadSettings(_this, "randomness");
	m_isOldVersionModel.loadSettings( _this, "oldversion" );

	// To maintain backward compatibility
	if( !_this.hasAttribute( "version" ) )
	{
		m_isOldVersionModel.setValue( true );
		m_vibratoGainModel.setValue( 0.0f );
		if( m_presetsModel.value() == 1 )
		{
		/* 	Earlier mallets used the stk internal
			default of 0.2. 0.2 * 128.0 = 25.6 	*/
			m_vibratoGainModel.setValue( 25.6f );
		}
		if( m_presetsModel.value() != 1 )
		{
			// Frequency actually worked for Vibraphone!
			m_vibratoFreqModel.setValue( 0.0f );
		}
		/*	Modalbar preset values, see stk, ModalBar.cpp
			void ModalBar :: setPreset( int preset )
			Stick Mix * 128.0
			m_positionModel values over 64 is formatted to the
			new knob by 128 - x	*/

		switch( m_presetsModel.value() )
		{
			case 0:
				m_hardnessModel.setValue( 55.0f );
				m_positionModel.setValue( 57.0f );
				m_stickModel.setValue( 12.0f );
				break;
			case 1:
				m_hardnessModel.setValue( 50.0f );
				m_positionModel.setValue( 55.0f );// 128 - 73!
				m_stickModel.setValue( 10.0f );
				break;
			case 2:
				m_hardnessModel.setValue( 78.0f );
				m_positionModel.setValue( 46.0f );
				m_stickModel.setValue( 18.0f );
				break;
			case 3:
				m_hardnessModel.setValue( 59.0f );
				m_positionModel.setValue( 48.0f );
				m_stickModel.setValue( 6.0f );
				break;
			case 4:
				m_hardnessModel.setValue( 58.0f );
				m_positionModel.setValue( 32.0f );
				m_stickModel.setValue( 13.0f );
				break;
			case 5:
				m_hardnessModel.setValue( 40.0f );
				m_positionModel.setValue( 57.0f );
				m_stickModel.setValue( 14.0f );
				break;
			case 6:
				m_hardnessModel.setValue( 51.0f );
				m_positionModel.setValue( 38.0f );
				m_stickModel.setValue( 9.0f );
				break;
			case 7:
				m_hardnessModel.setValue( 58.0f );
				m_positionModel.setValue( 58.0f );
				m_stickModel.setValue( 9.0f );
				break;
			case 8:
				m_hardnessModel.setValue( 50.0f );
				m_positionModel.setValue( 55.0f );// 128 - 73!
				m_stickModel.setValue( 10.0f );
				break;
		}
	}
}




QString MalletsInstrument::nodeName() const
{
	return( malletsstk_plugin_descriptor.name );
}




void MalletsInstrument::playNoteImpl(NotePlayHandle* _n, std::span<SampleFrame> out)
{
	if( m_filesMissing )
	{
		return;
	}

	int p = m_presetsModel.value();

	const float freq = _n->frequency();
	if (!_n->m_pluginData)
	{
		// If newer projects, adjust velocity to within stk's limits
		float velocityAdjust =
			m_isOldVersionModel.value() ? 100.0 : 200.0;
		const float vel = _n->getVolume() / velocityAdjust;

		const float random = m_randomModel.value();
		float hardness = m_hardnessModel.value();
		float position = m_positionModel.value();
		float modulator = m_modulatorModel.value();
		float crossfade = m_crossfadeModel.value();
		float pressure = m_pressureModel.value();
		float speed = m_velocityModel.value();

		if (p < 9)
		{
			hardness += random * fastRandInc(-64.f, 64.f);
			hardness = std::clamp(hardness, 0.0f, 128.0f);

			position += random * fastRandInc(-32.f, 32.f);
			position = std::clamp(position, 0.0f, 64.0f);
		}
		else if (p == 9)
		{
			modulator += random * fastRandInc(-64.f, 64.f);
			modulator = std::clamp(modulator, 0.0f, 128.0f);

			crossfade += random * fastRandInc(-64.f, 64.f);
			crossfade = std::clamp(crossfade, 0.0f, 128.0f);
		}
		else
		{
			pressure += random * fastRandInc(-64.f, 64.f);
			pressure = std::clamp(pressure, 0.0f, 128.0f);

			speed += random * fastRandInc(-64.f, 64.f);
			speed = std::clamp(speed, 0.0f, 128.0f);
		}

		// STK is not thread-safe, so its global setup and voice construction
		// happen off the audio thread; here the render thread only claims a
		// prepared voice and arms it in place. No allocation, no lock.
		const auto family = familyForPreset( p );
		if( !acquireVoiceBudget( m_voicesInUse ) )
		{
			// Every voice of this instrument is busy - in the current pool or
			// in a retired pool that still serves a sustained note: the note
			// waits for one and takes it in a later period.
			return;
		}
		MalletsVoiceSlot* slot = m_voicePool->claim( family );
		if( slot == nullptr )
		{
			releaseVoiceBudget( m_voicesInUse );
			return;
		}

		auto* ps = &slot->voices[static_cast<std::size_t>( family )];
		if( !ps->ready() )
		{
			// The STK voice could not be built: give the slot and its budget
			// back.
			MalletsVoicePool::release( slot );
			releaseVoiceBudget( m_voicesInUse );
			return;
		}

		armVoice( ps, p, freq, vel, hardness, position, modulator, crossfade,
				pressure, speed );
		ps->setPresetIndex(p);
		_n->m_pluginData = slot;
	}

	const f_cnt_t frames = _n->framesLeftForCurrentPeriod();
	const f_cnt_t offset = _n->noteOffset();

	auto* slot = static_cast<MalletsVoiceSlot*>(_n->m_pluginData);
	auto ps = &slot->voices[static_cast<std::size_t>( slot->family.load( std::memory_order_acquire ) )];
	ps->setFrequency(freq);

	p = ps->presetIndex();
	if (p < 9) // ModalBar updates
	{
		ps->setVibratoGain(m_vibratoGainModel.value());
		ps->setVibratoFreq(m_vibratoFreqModel.value());
	}
	else if (p == 9) // Tubular Bells updates
	{
		ps->setADSR(m_adsrModel.value());
		ps->setLFODepth(m_lfoDepthModel.value());
		ps->setLFOSpeed(m_lfoSpeedModel.value());
	}

	sample_t add_scale = 0.0f;
	if( p == 10 && m_isOldVersionModel.value() == true )
	{
		add_scale = static_cast<sample_t>( m_strikeModel.value() ) * freq * 2.5f;
	}

	for( f_cnt_t frame = offset; frame < frames + offset; ++frame )
	{
		out[frame][0] = ps->nextSampleLeft() * (m_scalers[p] + add_scale);
		out[frame][1] = ps->nextSampleRight() * (m_scalers[p] + add_scale);
	}
}




void MalletsInstrument::armVoice( MalletsSynth * _ps, const int _preset, const float _freq,
		const float _vel, const float _hardness, const float _position, const float _modulator,
		const float _crossfade, const float _pressure, const float _speed ) const
{
	if( _preset < 9 )
	{
		_ps->armModalBar( _freq,
					_vel,
					m_stickModel.value(),
					_hardness,
					_position,
					m_vibratoGainModel.value(),
					m_vibratoFreqModel.value(),
					_preset,
					(uint8_t) m_spreadModel.value() );
	}
	else if( _preset == 9 )
	{
		_ps->armTubeBell( _freq,
					_vel,
					_preset,
					m_lfoDepthModel.value(),
					_modulator,
					_crossfade,
					m_lfoSpeedModel.value(),
					m_adsrModel.value(),
					(uint8_t) m_spreadModel.value() );
	}
	else
	{
		_ps->armBandedWG( _freq,
					_vel,
					_pressure,
					m_motionModel.value(),
					m_vibratoModel.value(),
					_preset - 10,
					m_strikeModel.value() * 128.0,
					_speed,
					(uint8_t) m_spreadModel.value() );
	}
}




void MalletsInstrument::deleteNotePluginData( NotePlayHandle * _n )
{
	auto* slot = static_cast<MalletsVoiceSlot*>( _n->m_pluginData );
	_n->m_pluginData = nullptr;
	if( slot == nullptr ) { return; }
	MalletsVoicePool::release( slot );
	releaseVoiceBudget( m_voicesInUse );
}




gui::PluginView * MalletsInstrument::instantiateView( QWidget * _parent )
{
	return( new gui::MalletsInstrumentView( this, _parent ) );
}


namespace gui
{


MalletsInstrumentView::MalletsInstrumentView( MalletsInstrument * _instrument,
							QWidget * _parent ) :
        InstrumentViewFixedSize( _instrument, _parent )
{
	m_modalBarWidget = setupModalBarControls( this );
	setWidgetBackground( m_modalBarWidget, "artwork" );
	m_modalBarWidget->move( 0,0 );
	
	m_tubeBellWidget = setupTubeBellControls( this );
	setWidgetBackground( m_tubeBellWidget, "artwork" );
	m_tubeBellWidget->move( 0,0 );
	
	m_bandedWGWidget = setupBandedWGControls( this );
	setWidgetBackground( m_bandedWGWidget, "artwork" );
	m_bandedWGWidget->move( 0,0 );

	changePreset(); // Show widget

	m_presetsCombo = new ComboBox( this, tr( "Instrument" ) );
	m_presetsCombo->setGeometry( 140, 50, 99, ComboBox::DEFAULT_HEIGHT );
	
	connect( &_instrument->m_presetsModel, SIGNAL( dataChanged() ),
		 this, SLOT( changePreset() ) );
	
	m_spreadKnob = new Knob(KnobType::Vintage32, tr("Spread"), SMALL_FONT_SIZE, this);
	m_spreadKnob->move( 190, 140 );
	m_spreadKnob->setHintText( tr( "Spread:" ), "" );

	m_randomKnob = new Knob(KnobType::Vintage32, tr("Random"), SMALL_FONT_SIZE, this);
	m_randomKnob->move(190, 190);
	m_randomKnob->setHintText(tr("Random:"), "");

	// try to inform user about missing Stk-installation
	if( _instrument->m_filesMissing && getGUI() != nullptr )
	{
		QMessageBox::information( 0, tr( "Missing files" ),
				tr( "Your Stk-installation seems to be "
					"incomplete. Please make sure "
					"the full Stk-package is installed!" ),
				QMessageBox::Ok );
	}
}




void MalletsInstrumentView::setWidgetBackground( QWidget * _widget, const QString & _pic )
{
	_widget->setAutoFillBackground( true );
	QPalette pal;
	pal.setBrush( _widget->backgroundRole(),
		PLUGIN_NAME::getIconPixmap( _pic.toLatin1().constData() ) );
	_widget->setPalette( pal );
}




QWidget * MalletsInstrumentView::setupModalBarControls( QWidget * _parent )
{
	auto widget = new QWidget(_parent);
	widget->setFixedSize( 250, 250 );
		
	m_hardnessKnob = new Knob(KnobType::Vintage32, tr("Hardness"), SMALL_FONT_SIZE, widget);
	m_hardnessKnob->move( 30, 90 );
	m_hardnessKnob->setHintText( tr( "Hardness:" ), "" );

	m_positionKnob = new Knob(KnobType::Vintage32, tr("Position"), SMALL_FONT_SIZE, widget);
	m_positionKnob->move( 110, 90 );
	m_positionKnob->setHintText( tr( "Position:" ), "" );

	m_vibratoGainKnob = new Knob(KnobType::Vintage32, tr("Vibrato gain"), SMALL_FONT_SIZE, widget);
	m_vibratoGainKnob->move( 30, 140 );
	m_vibratoGainKnob->setHintText( tr( "Vibrato gain:" ), "" );

	m_vibratoFreqKnob = new Knob(KnobType::Vintage32, tr("Vibrato frequency"), SMALL_FONT_SIZE, widget);
	m_vibratoFreqKnob->move( 110, 140 );
	m_vibratoFreqKnob->setHintText( tr( "Vibrato frequency:" ), "" );

	m_stickKnob = new Knob(KnobType::Vintage32, tr("Stick mix"), SMALL_FONT_SIZE, widget);
	m_stickKnob->move( 190, 90 );
	m_stickKnob->setHintText( tr( "Stick mix:" ), "" );

	return( widget );
}




QWidget * MalletsInstrumentView::setupTubeBellControls( QWidget * _parent )
{
	auto widget = new QWidget(_parent);
	widget->setFixedSize( 250, 250 );
	
	m_modulatorKnob = new Knob(KnobType::Vintage32, tr("Modulator"), SMALL_FONT_SIZE, widget);
	m_modulatorKnob->move( 30, 90 );
	m_modulatorKnob->setHintText( tr( "Modulator:" ), "" );

	m_crossfadeKnob = new Knob(KnobType::Vintage32, tr("Crossfade"), SMALL_FONT_SIZE, widget);
	m_crossfadeKnob->move( 110, 90 );
	m_crossfadeKnob->setHintText( tr( "Crossfade:" ), "" );
	
	m_lfoSpeedKnob = new Knob(KnobType::Vintage32, tr("LFO speed"), SMALL_FONT_SIZE, widget);
	m_lfoSpeedKnob->move( 30, 140 );
	m_lfoSpeedKnob->setHintText( tr( "LFO speed:" ), "" );
	
	m_lfoDepthKnob = new Knob(KnobType::Vintage32, tr("LFO depth"), SMALL_FONT_SIZE, widget);
	m_lfoDepthKnob->move( 110, 140 );
	m_lfoDepthKnob->setHintText( tr( "LFO depth:" ), "" );
	
	m_adsrKnob = new Knob(KnobType::Vintage32, tr("ADSR"), SMALL_FONT_SIZE, widget);
	m_adsrKnob->move( 190, 90 );
	m_adsrKnob->setHintText( tr( "ADSR:" ), "" );

	return( widget );
}




QWidget * MalletsInstrumentView::setupBandedWGControls( QWidget * _parent )
{
	// BandedWG
	auto widget = new QWidget(_parent);
	widget->setFixedSize( 250, 250 );
	
/*	m_strikeLED = new LedCheckBox( tr( "Bowed" ), widget );
	m_strikeLED->move( 138, 25 );*/

	m_pressureKnob = new Knob(KnobType::Vintage32, tr("Pressure"), SMALL_FONT_SIZE, widget);
	m_pressureKnob->move( 30, 90 );
	m_pressureKnob->setHintText( tr( "Pressure:" ), "" );

/*	m_motionKnob = new Knob(KnobType::Vintage32, tr("Motion"), SMALL_FONT_SIZE, widget);
	m_motionKnob->move( 110, 90 );
	m_motionKnob->setHintText( tr( "Motion:" ), "" );*/

	m_velocityKnob = new Knob(KnobType::Vintage32, tr("Speed"), SMALL_FONT_SIZE, widget);
	m_velocityKnob->move( 30, 140 );
	m_velocityKnob->setHintText( tr( "Speed:" ), "" );
	
/*	m_vibratoKnob = new Knob(KnobType::Vintage32, tr("Vibrato"), SMALL_FONT_SIZE, widget);
	m_vibratoKnob->move( 110, 140 );
	m_vibratoKnob->setHintText( tr( "Vibrato:" ), "" );*/
	
	return( widget );
}




void MalletsInstrumentView::modelChanged()
{
	auto inst = castModel<MalletsInstrument>();
	m_hardnessKnob->setModel( &inst->m_hardnessModel );
	m_positionKnob->setModel( &inst->m_positionModel );
	m_vibratoGainKnob->setModel( &inst->m_vibratoGainModel );
	m_vibratoFreqKnob->setModel( &inst->m_vibratoFreqModel );
	m_stickKnob->setModel( &inst->m_stickModel );
	m_modulatorKnob->setModel( &inst->m_modulatorModel );
	m_crossfadeKnob->setModel( &inst->m_crossfadeModel );
	m_lfoSpeedKnob->setModel( &inst->m_lfoSpeedModel );
	m_lfoDepthKnob->setModel( &inst->m_lfoDepthModel );
	m_adsrKnob->setModel( &inst->m_adsrModel );
	m_pressureKnob->setModel( &inst->m_pressureModel );
//	m_motionKnob->setModel( &inst->m_motionModel );
//	m_vibratoKnob->setModel( &inst->m_vibratoModel );
	m_velocityKnob->setModel( &inst->m_velocityModel );
//	m_strikeLED->setModel( &inst->m_strikeModel );
	m_presetsCombo->setModel( &inst->m_presetsModel );
	m_spreadKnob->setModel( &inst->m_spreadModel );
	m_randomKnob->setModel(&inst->m_randomModel);
}




void MalletsInstrumentView::changePreset()
{
	auto inst = castModel<MalletsInstrument>();
	int _preset = inst->m_presetsModel.value();

	if( _preset < 9 )
	{
		m_tubeBellWidget->hide();
		m_bandedWGWidget->hide();
		m_modalBarWidget->show();
	}
	else if( _preset == 9 )
	{
		m_modalBarWidget->hide();
		m_bandedWGWidget->hide();
		m_tubeBellWidget->show();
	}
	else
	{
		m_modalBarWidget->hide();
		m_tubeBellWidget->hide();
		m_bandedWGWidget->show();
	}
}


} // namespace gui


// Preconstruct the STK voice and its delay buffer, off the audio thread.
bool MalletsSynth::init( Family _family, const sample_rate_t _sample_rate )
{
	try
	{
		Stk::setSampleRate( _sample_rate );
		Stk::setRawwavePath( QDir( ConfigManager::inst()->stkDir() ).absolutePath()
						.toLocal8Bit().constData() );
#ifndef LMMS_DEBUG
		Stk::showWarnings( false );
#endif

		switch( _family )
		{
			case Family::ModalBar: m_voice = new ModalBar(); break;
			case Family::TubeBell: m_voice = new TubeBell(); break;
			case Family::BandedWG: m_voice = new BandedWG(); break;
		}
	}
	catch( ... )
	{
		m_voice = nullptr;
	}

	m_delay = new StkFloat[256];
	resetDelay( 0 );
	return( m_voice != nullptr );
}




void MalletsSynth::resetDelay( const uint8_t _delay )
{
	m_delayRead = 0;
	m_delayWrite = _delay;
	for( int i = 0; i < 256; i++ )
	{
		m_delay[i] = 0.0;
	}
}




// ModalBar
void MalletsSynth::armModalBar( const StkFloat _pitch,
				const StkFloat _velocity,
				const StkFloat _control1,
				const StkFloat _control2,
				const StkFloat _control4,
				const StkFloat _control8,
				const StkFloat _control11,
				const int _control16,
				const uint8_t _delay )
{
	if( m_voice == nullptr )
	{
		resetDelay( _delay );
		return;
	}

	m_voice->clear();
	m_voice->controlChange( 16, _control16 );
	m_voice->controlChange( 1, _control1 );
	m_voice->controlChange( 2, _control2 );
	m_voice->controlChange( 4, _control4 );
	m_voice->controlChange( 8, _control8 );
	m_voice->controlChange( 11, _control11 );
	m_voice->controlChange( 128, 128.0f );
	m_voice->noteOn( _pitch, _velocity );
	resetDelay( _delay );
}




// TubeBell
void MalletsSynth::armTubeBell( const StkFloat _pitch,
				const StkFloat _velocity,
				const int _preset,
				const StkFloat _control1,
				const StkFloat _control2,
				const StkFloat _control4,
				const StkFloat _control11,
				const StkFloat _control128,
				const uint8_t _delay )
{
	if( m_voice == nullptr )
	{
		resetDelay( _delay );
		return;
	}

	m_voice->clear();
	m_voice->controlChange( 1, _control1 );
	m_voice->controlChange( 2, _control2 );
	m_voice->controlChange( 4, _control4 );
	m_voice->controlChange( 11, _control11 );
	m_voice->controlChange( 128, _control128 );
	m_voice->noteOn( _pitch, _velocity );
	resetDelay( _delay );
}




// BandedWG
void MalletsSynth::armBandedWG( const StkFloat _pitch,
				const StkFloat _velocity,
				const StkFloat _control2,
				const StkFloat _control4,
				const StkFloat _control11,
				const int _control16,
				const StkFloat _control64,
				const StkFloat _control128,
				const uint8_t _delay )
{
	if( m_voice == nullptr )
	{
		resetDelay( _delay );
		return;
	}

	m_voice->clear();
	m_voice->controlChange( 1, 128.0 );
	m_voice->controlChange( 2, _control2 );
	m_voice->controlChange( 4, _control4 );
	m_voice->controlChange( 11, _control11 );
	m_voice->controlChange( 16, _control16 );
	m_voice->controlChange( 64, _control64 );
	m_voice->controlChange( 128, _control128 );
	m_voice->noteOn( _pitch, _velocity );
	resetDelay( _delay );
}




extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model * m, void * )
{
	return new MalletsInstrument( static_cast<InstrumentTrack *>( m ) );
}


}


} // namespace lmms
