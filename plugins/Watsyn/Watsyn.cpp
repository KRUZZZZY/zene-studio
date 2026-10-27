/*
 * Watsyn.cpp - a 4-oscillator modulating wavetable synth
 *
 * Copyright (c) 2014 Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>
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

#include <QDomElement>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>

#include "Watsyn.h"
#include "base64.h"
#include "WaveformData.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "PixmapButton.h"
#include "Song.h"
#include "lmms_math.h"

#include "embed.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT watsyn_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"Watsyn",
	QT_TRANSLATE_NOOP( "PluginBrowser",
				"4-oscillator modulatable wavetable synth" ),
	"Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader( "logo" ),
	nullptr,
	nullptr,
} ;

}




WatsynVoice::WatsynVoice( WatsynInstrument * _parent, NotePlayHandle * _nph, WatsynWave * _wave ) :
			m_amod( _parent->m_amod.value() ),
			m_bmod( _parent->m_bmod.value() ),
			m_samplerate( Engine::audioEngine()->outputSampleRate() ),
			m_nph( _nph ),
			m_parent( _parent ),
			m_wave( _wave )
{
	// The note takes its parameters from the published set, never from a
	// separate reading of the models: the published set is one complete
	// publication, and a reading of the models could combine values from
	// different points of a multi-parameter update.
	std::fill_n(m_lphase, NUM_OSCS, 0.0f);
	std::fill_n(m_rphase, NUM_OSCS, 0.0f);
}


WatsynVoice::~WatsynVoice()
{
	releaseWave();
}


void WatsynVoice::releaseWave() noexcept
{
	// The snapshot goes back to the instrument; the GUI thread frees it once
	// no note holds it any more. The audio thread never frees it.
	if (m_wave != nullptr)
	{
		m_wave->pins.fetch_sub(1, std::memory_order_release);
		m_wave = nullptr;
	}
}


void WatsynVoice::renderOutput( std::span<SampleFrame> out )
{
	const f_cnt_t frames = static_cast<f_cnt_t>(out.size());

	// The oscillator parameters are taken as one published set. The set is
	// immutable once it is published, so a render never mixes two
	// publications and the read never fails.
	const auto params = m_parent->m_paramHandoff.read();

	// envelope parameters
	const float envAmt = m_parent->m_envAmt.value();
	const float envAtt = ( m_parent->m_envAtt.value() * samplerate() ) / 1000.0f;
	const float envHold = ( m_parent->m_envHold.value() * samplerate() ) / 1000.0f;
	const float envDec = ( m_parent->m_envDec.value() * samplerate() ) / 1000.0f;
	const float envLen = envAtt + envDec + envHold;
	const float tfp_ = static_cast<float>(m_nph->totalFramesPlayed());
	const float mixvalue_ = m_parent->m_abmix.value();
	// if the mix envelope is active and we haven't gone past its end, the mix
	// is calculated per frame, otherwise once per period
	const bool useMixEnvelope = envAmt != 0.0f && tfp_ < envLen;
	const float fixedBmix = ( ( mixvalue_ + 100.0 ) / 200.0 );
	const float fixedAmix = 1.0 - fixedBmix;

	for( f_cnt_t frame = 0; frame < frames; frame++ )
	{
		// put phases of 1-series oscs into variables because phase modulation might happen
		float A1_lphase = m_lphase[A1_OSC];
		float A1_rphase = m_rphase[A1_OSC];
		float B1_lphase = m_lphase[B1_OSC];
		float B1_rphase = m_rphase[B1_OSC];

		/////////////   A-series   /////////////////

		// A2
		sample_t A2_L = params.lvol[A2_OSC] * std::lerp(
			m_wave->data[1][static_cast<int>(m_lphase[A2_OSC])],
			m_wave->data[1][static_cast<int>(m_lphase[A2_OSC] + 1) % WAVELEN],
			fraction(m_lphase[A2_OSC])
		);
		sample_t A2_R = params.rvol[A2_OSC] * std::lerp(
			m_wave->data[1][static_cast<int>(m_rphase[A2_OSC])],
			m_wave->data[1][static_cast<int>(m_rphase[A2_OSC] + 1) % WAVELEN],
			fraction(m_rphase[A2_OSC])
		);

		// if phase mod, add to phases
		if( m_amod == MOD_PM )
		{
			A1_lphase = std::fmod(A1_lphase + A2_L * PMOD_AMT, WAVELEN);
			if( A1_lphase < 0 ) A1_lphase += WAVELEN;
			A1_rphase = std::fmod(A1_rphase + A2_R * PMOD_AMT, WAVELEN);
			if( A1_rphase < 0 ) A1_rphase += WAVELEN;
		}
		// A1
		sample_t A1_L = params.lvol[A1_OSC] * std::lerp(
			m_wave->data[0][static_cast<int>(A1_lphase)],
			m_wave->data[0][static_cast<int>(A1_lphase + 1) % WAVELEN],
			fraction(A1_lphase)
		);
		sample_t A1_R = params.rvol[A1_OSC] * std::lerp(
			m_wave->data[0][static_cast<int>(A1_rphase)],
			m_wave->data[0][static_cast<int>(A1_rphase + 1) % WAVELEN],
			fraction(A1_rphase)
		);

		/////////////   B-series   /////////////////

		// B2
		sample_t B2_L = params.lvol[B2_OSC] * std::lerp(
			m_wave->data[3][static_cast<int>(m_lphase[B2_OSC])],
			m_wave->data[3][static_cast<int>(m_lphase[B2_OSC] + 1) % WAVELEN],
			fraction(m_lphase[B2_OSC])
		);
		sample_t B2_R = params.rvol[B2_OSC] * std::lerp(
			m_wave->data[3][static_cast<int>(m_rphase[B2_OSC])],
			m_wave->data[3][static_cast<int>(m_rphase[B2_OSC] + 1) % WAVELEN],
			fraction(m_rphase[B2_OSC])
		);

		// if crosstalk active, add a1
		const float xt = m_parent->m_xtalk.value();
		if( xt > 0.0 )
		{
			B2_L += ( A1_L * xt ) * 0.01f;
			B2_R += ( A1_R * xt ) * 0.01f;
		}

		// if phase mod, add to phases
		if( m_bmod == MOD_PM )
		{
			B1_lphase = std::fmod(B1_lphase + B2_L * PMOD_AMT, WAVELEN);
			if( B1_lphase < 0 ) B1_lphase += WAVELEN;
			B1_rphase = std::fmod(B1_rphase + B2_R * PMOD_AMT, WAVELEN);
			if( B1_rphase < 0 ) B1_rphase += WAVELEN;
		}
		// B1
		sample_t B1_L = params.lvol[B1_OSC] * std::lerp(
			m_wave->data[2][static_cast<int>(B1_lphase) % WAVELEN],
			m_wave->data[2][static_cast<int>(B1_lphase + 1) % WAVELEN],
			fraction(B1_lphase)
		);
		sample_t B1_R = params.rvol[B1_OSC] * std::lerp(
			m_wave->data[2][static_cast<int>(B1_rphase) % WAVELEN],
			m_wave->data[2][static_cast<int>(B1_rphase + 1) % WAVELEN],
			fraction(B1_rphase)
		);


		// A-series modulation)
		switch( m_amod )
		{
			case MOD_MIX:
				A1_L = ( A1_L + A2_L ) / 2.0;
				A1_R = ( A1_R + A2_R ) / 2.0;
				break;
			case MOD_AM:
				A1_L *= qMax( 0.0f, A2_L + 1.0f );
				A1_R *= qMax( 0.0f, A2_R + 1.0f );
				break;
			case MOD_RM:
				A1_L *= A2_L;
				A1_R *= A2_R;
				break;
		}
		// B-series modulation (other than phase mod)
		switch( m_bmod )
		{
			case MOD_MIX:
				B1_L = ( B1_L + B2_L ) / 2.0;
				B1_R = ( B1_R + B2_R ) / 2.0;
				break;
			case MOD_AM:
				B1_L *= qMax( 0.0f, B2_L + 1.0f );
				B1_R *= qMax( 0.0f, B2_R + 1.0f );
				break;
			case MOD_RM:
				B1_L *= B2_L;
				B1_R *= B2_R;
				break;
		}

		// mix the a/b streams according to the mixing knob
		float amix = fixedAmix;
		float bmix = fixedBmix;
		if (useMixEnvelope)
		{
			float mixvalue = mixvalue_;
			const float tfp = tfp_ + static_cast<float>(frame);
			// handle mixing envelope
			if( tfp < envAtt )
			{
				mixvalue = qBound( -100.0f, mixvalue + ( tfp / envAtt * envAmt ), 100.0f );
			}
			else if ( tfp >= envAtt && tfp < envAtt + envHold )
			{
				mixvalue = qBound( -100.0f, mixvalue + envAmt, 100.0f );
			}
			else
			{
				mixvalue = qBound( -100.0f, mixvalue + envAmt - ( ( tfp - ( envAtt + envHold ) ) / envDec * envAmt ), 100.0f );
			}

			// get knob values
			bmix = ( ( mixvalue + 100.0 ) / 200.0 );
			amix = 1.0 - bmix;
		}

		out[frame][0] = ( A1_L * amix ) + ( B1_L * bmix );
		out[frame][1] = ( A1_R * amix ) + ( B1_R * bmix );

		// update phases
		for( int i = 0; i < NUM_OSCS; i++ )
		{
			m_lphase[i] += ( static_cast<float>( WAVELEN ) / ( m_samplerate / ( m_nph->frequency() * params.lfreq[i] ) ) );
			m_lphase[i] = std::fmod(m_lphase[i], WAVELEN);
			m_rphase[i] += ( static_cast<float>( WAVELEN ) / ( m_samplerate / ( m_nph->frequency() * params.rfreq[i] ) ) );
			m_rphase[i] = std::fmod(m_rphase[i], WAVELEN);
		}
	}

}


WatsynInstrument::~WatsynInstrument()
{
	// The notes are gone (the track silences them before it deletes the
	// instrument), so every snapshot can be freed here.
	for (auto* wave : m_freeWaves) { delete wave; }
	for (auto* wave : m_retiredWaves) { delete wave; }
	delete m_wave.exchange(nullptr);
}


void WatsynInstrument::publishWave()
{
	recycleWaves();

	// The GUI thread fills a snapshot and publishes it. A note that starts
	// later pins it; a note that is already sounding keeps the snapshot it
	// started with.
	WatsynWave* fresh = nullptr;
	if (!m_freeWaves.empty())
	{
		fresh = m_freeWaves.back();
		m_freeWaves.pop_back();
	}
	else
	{
		fresh = new WatsynWave();
	}
	fresh->pins.store(1, std::memory_order_relaxed);
	std::memcpy( fresh->data[0], A1_wave, sizeof( A1_wave ) );
	std::memcpy( fresh->data[1], A2_wave, sizeof( A2_wave ) );
	std::memcpy( fresh->data[2], B1_wave, sizeof( B1_wave ) );
	std::memcpy( fresh->data[3], B2_wave, sizeof( B2_wave ) );

	WatsynWave* old = m_wave.exchange(fresh, std::memory_order_seq_cst);
	if (old != nullptr)
	{
		// The instrument gives up its own reference. The snapshot stays
		// alive until the notes that pinned it are done with it.
		old->pins.fetch_sub(1, std::memory_order_acq_rel);
		m_retiredWaves.push_back(old);
	}

	recycleWaves();
}


WatsynWave* WatsynInstrument::claimWave() noexcept
{
	// The claim announces itself, so that the GUI thread can tell whether a
	// note is about to pin the snapshot it is about to retire: the count is
	// zero exactly when no note is inside this window.
	m_waveClaimers.fetch_add(1, std::memory_order_seq_cst);
	WatsynWave* wave = m_wave.load(std::memory_order_seq_cst);
	wave->pins.fetch_add(1, std::memory_order_relaxed);
	m_waveClaimers.fetch_sub(1, std::memory_order_seq_cst);
	return wave;
}


void WatsynInstrument::recycleWaves()
{
	// A retired snapshot may be filled again once no note holds it. The
	// claimer count has to be zero as well: a note that was about to pin the
	// snapshot when it was retired would still raise its pin count.
	if (m_waveClaimers.load(std::memory_order_seq_cst) != 0) { return; }

	for (auto it = m_retiredWaves.begin(); it != m_retiredWaves.end(); )
	{
		if ((*it)->pins.load(std::memory_order_acquire) == 0)
		{
			m_freeWaves.push_back(*it);
			it = m_retiredWaves.erase(it);
		}
		else
		{
			++it;
		}
	}
}



WatsynInstrument::WatsynInstrument( InstrumentTrack * _instrument_track ) :
		Instrument(_instrument_track, &watsyn_plugin_descriptor),

		a1_vol( 100.0f, 0.0f, 200.0f, 0.1f, this, tr( "Volume A1" ) ),
		a2_vol( 100.0f, 0.0f, 200.0f, 0.1f, this, tr( "Volume A2" ) ),
		b1_vol( 100.0f, 0.0f, 200.0f, 0.1f, this, tr( "Volume B1" ) ),
		b2_vol( 100.0f, 0.0f, 200.0f, 0.1f, this, tr( "Volume B2" ) ),

		a1_pan( 0.0f, -100.0f, 100.0f, 0.1f, this, tr( "Panning A1" ) ),
		a2_pan( 0.0f, -100.0f, 100.0f, 0.1f, this, tr( "Panning A2" ) ),
		b1_pan( 0.0f, -100.0f, 100.0f, 0.1f, this, tr( "Panning B1" ) ),
		b2_pan( 0.0f, -100.0f, 100.0f, 0.1f, this, tr( "Panning B2" ) ),

		a1_mult( 8.0f, 1.0, 24.0, 1.0, this, tr( "Freq. multiplier A1" ) ),
		a2_mult( 8.0f, 1.0, 24.0, 1.0, this, tr( "Freq. multiplier A2" ) ),
		b1_mult( 8.0f, 1.0, 24.0, 1.0, this, tr( "Freq. multiplier B1" ) ),
		b2_mult( 8.0f, 1.0, 24.0, 1.0, this, tr( "Freq. multiplier B2" ) ),

		a1_ltune( 0.0f, -600.0f, 600.0f, 1.0f, this, tr( "Left detune A1" ) ),
		a2_ltune( 0.0f, -600.0f, 600.0f, 1.0f, this, tr( "Left detune A2" ) ),
		b1_ltune( 0.0f, -600.0f, 600.0f, 1.0f, this, tr( "Left detune B1" ) ),
		b2_ltune( 0.0f, -600.0f, 600.0f, 1.0f, this, tr( "Left detune B2" ) ),

		a1_rtune( 0.0f, -600.0f, 600.0f, 1.0f, this, tr( "Right detune A1" ) ),
		a2_rtune( 0.0f, -600.0f, 600.0f, 1.0f, this, tr( "Right detune A2" ) ),
		b1_rtune( 0.0f, -600.0f, 600.0f, 1.0f, this, tr( "Right detune B1" ) ),
		b2_rtune( 0.0f, -600.0f, 600.0f, 1.0f, this, tr( "Right detune B2" ) ),

		a1_graph( -1.0f, 1.0f, GRAPHLEN, this ),
		a2_graph( -1.0f, 1.0f, GRAPHLEN, this ),
		b1_graph( -1.0f, 1.0f, GRAPHLEN, this ),
		b2_graph( -1.0f, 1.0f, GRAPHLEN, this ),

		m_abmix( 0.0f, -100.0f, 100.0f, 0.1f, this, tr( "A-B Mix" ) ),
		m_envAmt( 0.0f, -200.0f, 200.0f, 1.0f, this, tr( "A-B Mix envelope amount" ) ),

		m_envAtt( 0.0f, 0.0f, 2000.0f, 1.0f, 2000.0f, this, tr( "A-B Mix envelope attack" ) ),
		m_envHold( 0.0f, 0.0f, 2000.0f, 1.0f, 2000.0f, this, tr( "A-B Mix envelope hold" ) ),
		m_envDec( 0.0f, 0.0f, 2000.0f, 1.0f, 2000.0f, this, tr( "A-B Mix envelope decay" ) ),

		m_xtalk( 0.0f, 0.0f, 100.0f, 0.1f, this, tr( "A1-B2 Crosstalk" ) ),

		m_amod( 0, 0, 3, this, tr( "A2-A1 modulation" ) ),
		m_bmod( 0, 0, 3, this, tr( "B2-B1 modulation" ) ),

		m_selectedGraph( 0, 0, 3, this, tr( "Selected graph" ) )
{
	connect( &a1_vol, SIGNAL( dataChanged() ), this, SLOT( updateVolumes() ) );
	connect( &a2_vol, SIGNAL( dataChanged() ), this, SLOT( updateVolumes() ) );
	connect( &b1_vol, SIGNAL( dataChanged() ), this, SLOT( updateVolumes() ) );
	connect( &b2_vol, SIGNAL( dataChanged() ), this, SLOT( updateVolumes() ) );

	connect( &a1_pan, SIGNAL( dataChanged() ), this, SLOT( updateVolumes() ) );
	connect( &a2_pan, SIGNAL( dataChanged() ), this, SLOT( updateVolumes() ) );
	connect( &b1_pan, SIGNAL( dataChanged() ), this, SLOT( updateVolumes() ) );
	connect( &b2_pan, SIGNAL( dataChanged() ), this, SLOT( updateVolumes() ) );

	connect( &a1_mult, SIGNAL( dataChanged() ), this, SLOT( updateFreqA1() ) );
	connect( &a2_mult, SIGNAL( dataChanged() ), this, SLOT( updateFreqA2() ) );
	connect( &b1_mult, SIGNAL( dataChanged() ), this, SLOT( updateFreqB1() ) );
	connect( &b2_mult, SIGNAL( dataChanged() ), this, SLOT( updateFreqB2() ) );

	connect( &a1_ltune, SIGNAL( dataChanged() ), this, SLOT( updateFreqA1() ) );
	connect( &a2_ltune, SIGNAL( dataChanged() ), this, SLOT( updateFreqA2() ) );
	connect( &b1_ltune, SIGNAL( dataChanged() ), this, SLOT( updateFreqB1() ) );
	connect( &b2_ltune, SIGNAL( dataChanged() ), this, SLOT( updateFreqB2() ) );

	connect( &a1_rtune, SIGNAL( dataChanged() ), this, SLOT( updateFreqA1() ) );
	connect( &a2_rtune, SIGNAL( dataChanged() ), this, SLOT( updateFreqA2() ) );
	connect( &b1_rtune, SIGNAL( dataChanged() ), this, SLOT( updateFreqB1() ) );
	connect( &b2_rtune, SIGNAL( dataChanged() ), this, SLOT( updateFreqB2() ) );
	
	connect( &a1_graph, SIGNAL( samplesChanged( int, int ) ), this, SLOT( updateWaveA1() ) );
	connect( &a2_graph, SIGNAL( samplesChanged( int, int ) ), this, SLOT( updateWaveA2() ) );
	connect( &b1_graph, SIGNAL( samplesChanged( int, int ) ), this, SLOT( updateWaveB1() ) );
	connect( &b2_graph, SIGNAL( samplesChanged( int, int ) ), this, SLOT( updateWaveB2() ) );

	a1_graph.setWaveToSine();
	a2_graph.setWaveToSine();
	b1_graph.setWaveToSine();
	b2_graph.setWaveToSine();

	updateVolumes();
	updateFreqA1();
	updateFreqA2();
	updateFreqB1();
	updateFreqB2();
	updateWaveA1();
	updateWaveA2();
	updateWaveB1();
	updateWaveB2();
}


void WatsynInstrument::playNoteImpl(NotePlayHandle* _n, std::span<SampleFrame> out)
{
	const f_cnt_t frames = _n->framesLeftForCurrentPeriod();
	const f_cnt_t offset = _n->noteOffset();

	auto voice = static_cast<WatsynVoice*>(_n->m_pluginData);
	if (!voice)
	{
		// The per-note state lives in the note handle's own storage, so a
		// note start neither allocates nor takes a slot from a pool: every
		// note the note manager admits gets a voice.
		static_assert(sizeof(WatsynVoice) <= NotePlayHandle::PluginDataStorageSize);
		static_assert(alignof(WatsynVoice) <= alignof(std::max_align_t));
		voice = std::construct_at(static_cast<WatsynVoice*>(_n->pluginDataStorage()),
			this, _n, claimWave());
		_n->m_pluginData = voice;
	}

	// The oscillators are mixed into the note's own buffer, so notes that
	// sound at the same time never share a render buffer.
	voice->renderOutput(std::span<SampleFrame>{out.data() + offset, static_cast<std::size_t>(frames)});

	applyRelease(out.data(), _n);
}


void WatsynInstrument::deleteNotePluginData( NotePlayHandle * _n )
{
	if (auto voice = static_cast<WatsynVoice*>(_n->m_pluginData))
	{
		// The voice gives the wavetable snapshot back and leaves the handle's
		// storage; clearing the pointer keeps a second cleanup from handing
		// the same snapshot to two notes.
		std::destroy_at(voice);
		_n->m_pluginData = nullptr;
	}
}



void WatsynInstrument::saveSettings( QDomDocument & _doc,
							QDomElement & _this )
{
	a1_vol.saveSettings( _doc, _this, "a1_vol" );
	a2_vol.saveSettings( _doc, _this, "a2_vol" );
	b1_vol.saveSettings( _doc, _this, "b1_vol" );
	b2_vol.saveSettings( _doc, _this, "b2_vol" );

	a1_pan.saveSettings( _doc, _this, "a1_pan" );
	a2_pan.saveSettings( _doc, _this, "a2_pan" );
	b1_pan.saveSettings( _doc, _this, "b1_pan" );
	b2_pan.saveSettings( _doc, _this, "b2_pan" );

	a1_mult.saveSettings( _doc, _this, "a1_mult" );
	a2_mult.saveSettings( _doc, _this, "a2_mult" );
	b1_mult.saveSettings( _doc, _this, "b1_mult" );
	b2_mult.saveSettings( _doc, _this, "b2_mult" );

	a1_ltune.saveSettings( _doc, _this, "a1_ltune" );
	a2_ltune.saveSettings( _doc, _this, "a2_ltune" );
	b1_ltune.saveSettings( _doc, _this, "b1_ltune" );
	b2_ltune.saveSettings( _doc, _this, "b2_ltune" );

	a1_rtune.saveSettings( _doc, _this, "a1_rtune" );
	a2_rtune.saveSettings( _doc, _this, "a2_rtune" );
	b1_rtune.saveSettings( _doc, _this, "b1_rtune" );
	b2_rtune.saveSettings( _doc, _this, "b2_rtune" );

	// save graphs
	QString sampleString;

	base64::encode( (const char *)a1_graph.samples(), a1_graph.length() * sizeof(float), sampleString );
	_this.setAttribute( "a1_wave", sampleString );
	base64::encode( (const char *)a2_graph.samples(), a2_graph.length() * sizeof(float), sampleString );
	_this.setAttribute( "a2_wave", sampleString );
	base64::encode( (const char *)b1_graph.samples(), b1_graph.length() * sizeof(float), sampleString );
	_this.setAttribute( "b1_wave", sampleString );
	base64::encode( (const char *)b2_graph.samples(), b2_graph.length() * sizeof(float), sampleString );
	_this.setAttribute( "b2_wave", sampleString );

	m_abmix.saveSettings( _doc, _this, "abmix" );
	m_envAmt.saveSettings( _doc, _this, "envAmt" );
	m_envAtt.saveSettings( _doc, _this, "envAtt" );
	m_envHold.saveSettings( _doc, _this, "envHold" );
	m_envDec.saveSettings( _doc, _this, "envDec" );

	m_xtalk.saveSettings( _doc, _this, "xtalk" );

	m_amod.saveSettings( _doc, _this, "amod" );
	m_bmod.saveSettings( _doc, _this, "bmod" );
	m_selectedGraph.saveSettings( _doc, _this, "selgraph" );
}


void WatsynInstrument::loadSettings( const QDomElement & _this )
{
	a1_vol.loadSettings( _this, "a1_vol" );
	a2_vol.loadSettings( _this, "a2_vol" );
	b1_vol.loadSettings( _this, "b1_vol" );
	b2_vol.loadSettings( _this, "b2_vol" );

	a1_pan.loadSettings( _this, "a1_pan" );
	a2_pan.loadSettings( _this, "a2_pan" );
	b1_pan.loadSettings( _this, "b1_pan" );
	b2_pan.loadSettings( _this, "b2_pan" );

	a1_mult.loadSettings( _this, "a1_mult" );
	a2_mult.loadSettings( _this, "a2_mult" );
	b1_mult.loadSettings( _this, "b1_mult" );
	b2_mult.loadSettings( _this, "b2_mult" );

	a1_ltune.loadSettings( _this, "a1_ltune" );
	a2_ltune.loadSettings( _this, "a2_ltune" );
	b1_ltune.loadSettings( _this, "b1_ltune" );
	b2_ltune.loadSettings( _this, "b2_ltune" );

	a1_rtune.loadSettings( _this, "a1_rtune" );
	a2_rtune.loadSettings( _this, "a2_rtune" );
	b1_rtune.loadSettings( _this, "b1_rtune" );
	b2_rtune.loadSettings( _this, "b2_rtune" );

	// load graphs: each from exactly GRAPHLEN finite saved values, else it keeps its wave (Q050-486)
	waveform::loadGraph(_this, QStringLiteral("a1_wave"), a1_graph, QStringLiteral("Watsyn"));
	waveform::loadGraph(_this, QStringLiteral("a2_wave"), a2_graph, QStringLiteral("Watsyn"));
	waveform::loadGraph(_this, QStringLiteral("b1_wave"), b1_graph, QStringLiteral("Watsyn"));
	waveform::loadGraph(_this, QStringLiteral("b2_wave"), b2_graph, QStringLiteral("Watsyn"));

	m_abmix.loadSettings( _this, "abmix" );

	m_envAmt.loadSettings( _this, "envAmt" );
	m_envAtt.loadSettings( _this, "envAtt" );
	m_envHold.loadSettings( _this, "envHold" );
	m_envDec.loadSettings( _this, "envDec" );

	m_xtalk.loadSettings( _this, "xtalk" );

	m_amod.loadSettings( _this, "amod" );
	m_bmod.loadSettings( _this, "bmod" );
	m_selectedGraph.loadSettings( _this, "selgraph" );
}


QString WatsynInstrument::nodeName() const
{
	return( watsyn_plugin_descriptor.name );
}


gui::PluginView* WatsynInstrument::instantiateView( QWidget * _parent )
{
	return( new gui::WatsynView( this, _parent ) );
}


WatsynParamHandoff<NUM_OSCS>::Values WatsynInstrument::computeParams() const
{
	// The models are the state of the instrument. Every model value is read
	// once, so the left and the right gain of an oscillator come from the
	// same volume and panning, and the set is one coherent computation.
	WatsynParamHandoff<NUM_OSCS>::Values values{};

	const float a1Vol = a1_vol.value();
	const float a1Pan = a1_pan.value();
	values.lvol[A1_OSC] = leftCh( a1Vol, a1Pan );
	values.rvol[A1_OSC] = rightCh( a1Vol, a1Pan );

	const float a2Vol = a2_vol.value();
	const float a2Pan = a2_pan.value();
	values.lvol[A2_OSC] = leftCh( a2Vol, a2Pan );
	values.rvol[A2_OSC] = rightCh( a2Vol, a2Pan );

	const float b1Vol = b1_vol.value();
	const float b1Pan = b1_pan.value();
	values.lvol[B1_OSC] = leftCh( b1Vol, b1Pan );
	values.rvol[B1_OSC] = rightCh( b1Vol, b1Pan );

	const float b2Vol = b2_vol.value();
	const float b2Pan = b2_pan.value();
	values.lvol[B2_OSC] = leftCh( b2Vol, b2Pan );
	values.rvol[B2_OSC] = rightCh( b2Vol, b2Pan );

	const float a1Mult = a1_mult.value();
	const float a1LTune = a1_ltune.value();
	const float a1RTune = a1_rtune.value();
	values.lfreq[A1_OSC] = (a1Mult / 8) * std::exp2(a1LTune / 1200);
	values.rfreq[A1_OSC] = (a1Mult / 8) * std::exp2(a1RTune / 1200);

	const float a2Mult = a2_mult.value();
	const float a2LTune = a2_ltune.value();
	const float a2RTune = a2_rtune.value();
	values.lfreq[A2_OSC] = (a2Mult / 8) * std::exp2(a2LTune / 1200);
	values.rfreq[A2_OSC] = (a2Mult / 8) * std::exp2(a2RTune / 1200);

	const float b1Mult = b1_mult.value();
	const float b1LTune = b1_ltune.value();
	const float b1RTune = b1_rtune.value();
	values.lfreq[B1_OSC] = (b1Mult / 8) * std::exp2(b1LTune / 1200);
	values.rfreq[B1_OSC] = (b1Mult / 8) * std::exp2(b1RTune / 1200);

	const float b2Mult = b2_mult.value();
	const float b2LTune = b2_ltune.value();
	const float b2RTune = b2_rtune.value();
	values.lfreq[B2_OSC] = (b2Mult / 8) * std::exp2(b2LTune / 1200);
	values.rfreq[B2_OSC] = (b2Mult / 8) * std::exp2(b2RTune / 1200);

	return values;
}


void WatsynInstrument::publishParams()
{
	// The models are the state of the instrument; the published set is
	// computed from them, so the set the audio thread takes is always one
	// coherent computation and never a mixture of two.
	m_paramHandoff.publish( computeParams() );
}


void WatsynInstrument::updateVolumes()
{
	publishParams();
}


void WatsynInstrument::updateFreqA1()
{
	publishParams();
}


void WatsynInstrument::updateFreqA2()
{
	publishParams();
}


void WatsynInstrument::updateFreqB1()
{
	publishParams();
}


void WatsynInstrument::updateFreqB2()
{
	publishParams();
}


void WatsynInstrument::updateWaveA1()
{
	// do sinc+oversampling on the wavetables to improve quality
	srccpy( &A1_wave[0], const_cast<float*>( a1_graph.samples() ) );
	// publish the new wavetable: notes that start now use it, notes that are
	// already sounding keep the snapshot they started with
	publishWave();
}


void WatsynInstrument::updateWaveA2()
{
	// do sinc+oversampling on the wavetables to improve quality
	srccpy( &A2_wave[0], const_cast<float*>( a2_graph.samples() ) );
	publishWave();
}


void WatsynInstrument::updateWaveB1()
{
	// do sinc+oversampling on the wavetables to improve quality
	srccpy( &B1_wave[0], const_cast<float*>( b1_graph.samples() ) );
	publishWave();
}


void WatsynInstrument::updateWaveB2()
{
	// do sinc+oversampling on the wavetables to improve quality
	srccpy( &B2_wave[0], const_cast<float*>( b2_graph.samples() ) );
	publishWave();
}


namespace gui
{


WatsynView::WatsynView( Instrument * _instrument,
					QWidget * _parent ) :
	InstrumentViewFixedSize( _instrument, _parent )
{
	setAutoFillBackground( true );
	QPalette pal;

	pal.setBrush( backgroundRole(),	PLUGIN_NAME::getIconPixmap(	"artwork" ) );
	setPalette( pal );

// knobs... lots of em

	a1_volKnob = makeKnob<VolumeKnob>(130, A1ROW, tr("Volume"), "%", "aKnob");
	a2_volKnob = makeKnob<VolumeKnob>(130, A2ROW, tr("Volume"), "%", "aKnob");
	b1_volKnob = makeKnob<VolumeKnob>(130, B1ROW, tr("Volume"), "%", "bKnob");
	b2_volKnob = makeKnob<VolumeKnob>(130, B2ROW, tr("Volume"), "%", "bKnob");

	a1_panKnob = makeKnob(154, A1ROW, tr("Panning"), "", "aKnob");
	a2_panKnob = makeKnob(154, A2ROW, tr("Panning"), "", "aKnob");
	b1_panKnob = makeKnob(154, B1ROW, tr("Panning"), "", "bKnob");
	b2_panKnob = makeKnob(154, B2ROW, tr("Panning"), "", "bKnob");

	a1_multKnob = makeKnob(178, A1ROW, tr("Freq. multiplier"), "/8", "aKnob");
	a2_multKnob = makeKnob(178, A2ROW, tr("Freq. multiplier"), "/8", "aKnob");
	b1_multKnob = makeKnob(178, B1ROW, tr("Freq. multiplier"), "/8", "bKnob");
	b2_multKnob = makeKnob(178, B2ROW, tr("Freq. multiplier"), "/8", "bKnob");

	a1_ltuneKnob = makeKnob(202, A1ROW, tr("Left detune"), tr(" cents"), "aKnob");
	a2_ltuneKnob = makeKnob(202, A2ROW, tr("Left detune"), tr(" cents"), "aKnob");
	b1_ltuneKnob = makeKnob(202, B1ROW, tr("Left detune"), tr(" cents"), "bKnob");
	b2_ltuneKnob = makeKnob(202, B2ROW, tr("Left detune"), tr(" cents"), "bKnob");

	a1_rtuneKnob = makeKnob(226, A1ROW, tr("Right detune"), tr(" cents"), "aKnob");
	a2_rtuneKnob = makeKnob(226, A2ROW, tr("Right detune"), tr(" cents"), "aKnob");
	b1_rtuneKnob = makeKnob(226, B1ROW, tr("Right detune"), tr(" cents"), "bKnob");
	b2_rtuneKnob = makeKnob(226, B2ROW, tr("Right detune"), tr(" cents"), "bKnob");

	m_abmixKnob = makeKnob(4, 3, tr("A-B Mix"), "", "mixKnob");

	m_envAmtKnob = makeKnob(88, 3, tr("Mix envelope amount"), "", "mixenvKnob");

	m_envAttKnob = makeKnob<TempoSyncKnob>(88, A1ROW, tr("Mix envelope attack"), " ms", "mixenvKnob");
	m_envHoldKnob = makeKnob<TempoSyncKnob>(88, A2ROW, tr("Mix envelope hold"), " ms", "mixenvKnob");
	m_envDecKnob = makeKnob<TempoSyncKnob>(88, B1ROW, tr("Mix envelope decay"), " ms", "mixenvKnob");

	m_xtalkKnob = makeKnob(88, B2ROW, tr("Crosstalk"), "", "xtalkKnob");

	m_abmixKnob -> setFixedSize( 31, 31 );


// button groups next.
// graph select buttons
	auto a1_selectButton = new PixmapButton(this, nullptr);
	a1_selectButton -> move( 4, 121 );
	a1_selectButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "a1_active" ) );
	a1_selectButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "a1_inactive" ) );
	a1_selectButton->setToolTip(tr("Select oscillator A1"));

	auto a2_selectButton = new PixmapButton(this, nullptr);
	a2_selectButton -> move( 44, 121 );
	a2_selectButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "a2_active" ) );
	a2_selectButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "a2_inactive" ) );
	a2_selectButton->setToolTip(tr("Select oscillator A2"));

	auto b1_selectButton = new PixmapButton(this, nullptr);
	b1_selectButton -> move( 84, 121 );
	b1_selectButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "b1_active" ) );
	b1_selectButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "b1_inactive" ) );
	b1_selectButton->setToolTip(tr("Select oscillator B1"));

	auto b2_selectButton = new PixmapButton(this, nullptr);
	b2_selectButton -> move( 124, 121 );
	b2_selectButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "b2_active" ) );
	b2_selectButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "b2_inactive" ) );
	b2_selectButton->setToolTip(tr("Select oscillator B2"));

	m_selectedGraphGroup = new AutomatableButtonGroup( this );
	m_selectedGraphGroup -> addButton( a1_selectButton );
	m_selectedGraphGroup -> addButton( a2_selectButton );
	m_selectedGraphGroup -> addButton( b1_selectButton );
	m_selectedGraphGroup -> addButton( b2_selectButton );
	auto w = castModel<WatsynInstrument>();
	m_selectedGraphGroup -> setModel( &w -> m_selectedGraph);

// A-modulation button group
	auto amod_mixButton = new PixmapButton(this, nullptr);
	amod_mixButton -> move( 4, 50 );
	amod_mixButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "amix_active" ) );
	amod_mixButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "amix_inactive" ) );
	amod_mixButton->setToolTip(tr("Mix output of A2 to A1"));

	auto amod_amButton = new PixmapButton(this, nullptr);
	amod_amButton -> move( 4, 66 );
	amod_amButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "aam_active" ) );
	amod_amButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "aam_inactive" ) );
	amod_amButton->setToolTip(tr("Modulate amplitude of A1 by output of A2"));

	auto amod_rmButton = new PixmapButton(this, nullptr);
	amod_rmButton -> move( 4, 82 );
	amod_rmButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "arm_active" ) );
	amod_rmButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "arm_inactive" ) );
	amod_rmButton->setToolTip(tr("Ring modulate A1 and A2"));

	auto amod_pmButton = new PixmapButton(this, nullptr);
	amod_pmButton -> move( 4, 98 );
	amod_pmButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "apm_active" ) );
	amod_pmButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "apm_inactive" ) );
	amod_pmButton->setToolTip(tr("Modulate phase of A1 by output of A2"));

	m_aModGroup = new AutomatableButtonGroup( this );
	m_aModGroup -> addButton( amod_mixButton );
	m_aModGroup -> addButton( amod_amButton );
	m_aModGroup -> addButton( amod_rmButton );
	m_aModGroup -> addButton( amod_pmButton );

// B-modulation button group
	auto bmod_mixButton = new PixmapButton(this, nullptr);
	bmod_mixButton -> move( 44, 50 );
	bmod_mixButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "bmix_active" ) );
	bmod_mixButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "bmix_inactive" ) );
	bmod_mixButton->setToolTip(tr("Mix output of B2 to B1"));

	auto bmod_amButton = new PixmapButton(this, nullptr);
	bmod_amButton -> move( 44, 66 );
	bmod_amButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "bam_active" ) );
	bmod_amButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "bam_inactive" ) );
	bmod_amButton->setToolTip(tr("Modulate amplitude of B1 by output of B2"));

	auto bmod_rmButton = new PixmapButton(this, nullptr);
	bmod_rmButton -> move( 44, 82 );
	bmod_rmButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "brm_active" ) );
	bmod_rmButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "brm_inactive" ) );
	bmod_rmButton->setToolTip(tr("Ring modulate B1 and B2"));

	auto bmod_pmButton = new PixmapButton(this, nullptr);
	bmod_pmButton -> move( 44, 98 );
	bmod_pmButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "bpm_active" ) );
	bmod_pmButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "bpm_inactive" ) );
	bmod_pmButton->setToolTip(tr("Modulate phase of B1 by output of B2"));

	m_bModGroup = new AutomatableButtonGroup( this );
	m_bModGroup -> addButton( bmod_mixButton );
	m_bModGroup -> addButton( bmod_amButton );
	m_bModGroup -> addButton( bmod_rmButton );
	m_bModGroup -> addButton( bmod_pmButton );


// graph widgets
	pal = QPalette();
	pal.setBrush( backgroundRole(), PLUGIN_NAME::getIconPixmap("wavegraph") );
// a1 graph
	a1_graph = new Graph( this, Graph::Style::Linear, 224, 105 );
	a1_graph->move( 4, 141 );
	a1_graph->setAutoFillBackground( true );
	a1_graph->setGraphColor( QColor( 0x43, 0xb2, 0xff ) );
	a1_graph->setToolTip(tr("Draw your own waveform here by dragging your mouse on this graph."));
	a1_graph->setPalette( pal );

// a2 graph
	a2_graph = new Graph( this, Graph::Style::Linear, 224, 105 );
	a2_graph->move( 4, 141 );
	a2_graph->setAutoFillBackground( true );
	a2_graph->setGraphColor( QColor( 0x43, 0xb2, 0xff ) );
	a2_graph->setToolTip(tr("Draw your own waveform here by dragging your mouse on this graph."));
	a2_graph->setPalette( pal );

// b1 graph
	b1_graph = new Graph( this, Graph::Style::Linear, 224, 105 );
	b1_graph->move( 4, 141 );
	b1_graph->setAutoFillBackground( true );
	b1_graph->setGraphColor( QColor( 0xfc, 0x54, 0x31 ) );
	b1_graph->setToolTip(tr("Draw your own waveform here by dragging your mouse on this graph."));
	b1_graph->setPalette( pal );

// b2 graph
	b2_graph = new Graph( this, Graph::Style::Linear, 224, 105 );
	b2_graph->move( 4, 141 );
	b2_graph->setAutoFillBackground( true );
	b2_graph->setGraphColor( QColor( 0xfc, 0x54, 0x31 ) );
	b2_graph->setToolTip(tr("Draw your own waveform here by dragging your mouse on this graph."));
	b2_graph->setPalette( pal );


// misc pushbuttons
// waveform modifications

	m_loadButton = new PixmapButton( this, tr( "Load waveform" ) );
	m_loadButton -> move ( 173, 121 );
	m_loadButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "load_active" ) );
	m_loadButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "load_inactive" ) );
	m_loadButton->setToolTip(tr("Load a waveform from a sample file"));

	m_phaseLeftButton = new PixmapButton( this, tr( "Phase left" ) );
	m_phaseLeftButton -> move ( 193, 121 );
	m_phaseLeftButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "phl_active" ) );
	m_phaseLeftButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "phl_inactive" ) );
	m_phaseLeftButton->setToolTip(tr("Shift phase by -15 degrees"));

	m_phaseRightButton = new PixmapButton( this, tr( "Phase right" ) );
	m_phaseRightButton -> move ( 210, 121 );
	m_phaseRightButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "phr_active" ) );
	m_phaseRightButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "phr_inactive" ) );
	m_phaseRightButton->setToolTip(tr("Shift phase by +15 degrees"));

	m_normalizeButton = new PixmapButton( this, tr( "Normalize" ) );
	m_normalizeButton -> move ( 230, 121 );
	m_normalizeButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "norm_active" ) );
	m_normalizeButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "norm_inactive" ) );
	m_normalizeButton->setToolTip(tr("Normalize"));


	m_invertButton = new PixmapButton( this, tr( "Invert" ) );
	m_invertButton -> move ( 230, 138 );
	m_invertButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "inv_active" ) );
	m_invertButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "inv_inactive" ) );
	m_invertButton->setToolTip(tr("Invert"));

	m_smoothButton = new PixmapButton( this, tr( "Smooth" ) );
	m_smoothButton -> move ( 230, 155 );
	m_smoothButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "smooth_active" ) );
	m_smoothButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "smooth_inactive" ) );
	m_smoothButton->setToolTip(tr("Smooth"));

// waveforms

	m_sinWaveButton = new PixmapButton( this, tr( "Sine wave" ) );
	m_sinWaveButton -> move ( 230, 176 );
	m_sinWaveButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "sin_active" ) );
	m_sinWaveButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "sin_inactive" ) );
	m_sinWaveButton->setToolTip(tr("Sine wave"));

	m_triWaveButton = new PixmapButton( this, tr( "Triangle wave" ) );
	m_triWaveButton -> move ( 230, 194 );
	m_triWaveButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "tri_active" ) );
	m_triWaveButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "tri_inactive" ) );
	m_triWaveButton->setToolTip(tr("Triangle wave"));

	m_sawWaveButton = new PixmapButton( this, tr( "Triangle wave" ) );
	m_sawWaveButton -> move ( 230, 212 );
	m_sawWaveButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "saw_active" ) );
	m_sawWaveButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "saw_inactive" ) );
	m_sawWaveButton->setToolTip(tr("Saw wave"));

	m_sqrWaveButton = new PixmapButton( this, tr( "Square wave" ) );
	m_sqrWaveButton -> move ( 230, 230 );
	m_sqrWaveButton -> setActiveGraphic( PLUGIN_NAME::getIconPixmap( "sqr_active" ) );
	m_sqrWaveButton -> setInactiveGraphic( PLUGIN_NAME::getIconPixmap( "sqr_inactive" ) );
	m_sqrWaveButton->setToolTip(tr("Square wave"));



	connect( m_sinWaveButton, SIGNAL( clicked() ), this, SLOT( sinWaveClicked() ) );
	connect( m_triWaveButton, SIGNAL( clicked() ), this, SLOT( triWaveClicked() ) );
	connect( m_sawWaveButton, SIGNAL( clicked() ), this, SLOT( sawWaveClicked() ) );
	connect( m_sqrWaveButton, SIGNAL( clicked() ), this, SLOT( sqrWaveClicked() ) );
	connect( m_normalizeButton, SIGNAL( clicked() ), this, SLOT( normalizeClicked() ) );
	connect( m_invertButton, SIGNAL( clicked() ), this, SLOT( invertClicked() ) );
	connect( m_smoothButton, SIGNAL( clicked() ), this, SLOT( smoothClicked() ) );
	connect( m_phaseLeftButton, SIGNAL( clicked() ), this, SLOT( phaseLeftClicked() ) );
	connect( m_phaseRightButton, SIGNAL( clicked() ), this, SLOT( phaseRightClicked() ) );
	connect( m_loadButton, SIGNAL( clicked() ), this, SLOT( loadClicked() ) );

	connect( a1_selectButton, SIGNAL( clicked() ), this, SLOT( updateLayout() ) );
	connect( a2_selectButton, SIGNAL( clicked() ), this, SLOT( updateLayout() ) );
	connect( b1_selectButton, SIGNAL( clicked() ), this, SLOT( updateLayout() ) );
	connect( b2_selectButton, SIGNAL( clicked() ), this, SLOT( updateLayout() ) );

	updateLayout();
}


void WatsynView::updateLayout()
{
	switch( m_selectedGraphGroup->model()->value() )
	{
		case A1_OSC:
			a1_graph->show();
			a2_graph->hide();
			b1_graph->hide();
			b2_graph->hide();
			break;
		case A2_OSC:
			a1_graph->hide();
			a2_graph->show();
			b1_graph->hide();
			b2_graph->hide();
			break;
		case B1_OSC:
			a1_graph->hide();
			a2_graph->hide();
			b1_graph->show();
			b2_graph->hide();
			break;
		case B2_OSC:
			a1_graph->hide();
			a2_graph->hide();
			b1_graph->hide();
			b2_graph->show();
			break;
	}
}



void WatsynView::sinWaveClicked()
{
	switch( m_selectedGraphGroup->model()->value() )
	{
		case A1_OSC:
			a1_graph->model()->setWaveToSine();
			Engine::getSong()->setModified();
			break;
		case A2_OSC:
			a2_graph->model()->setWaveToSine();
			Engine::getSong()->setModified();
			break;
		case B1_OSC:
			b1_graph->model()->setWaveToSine();
			Engine::getSong()->setModified();
			break;
		case B2_OSC:
			b2_graph->model()->setWaveToSine();
			Engine::getSong()->setModified();
			break;
	}
}


void WatsynView::triWaveClicked()
{
	switch( m_selectedGraphGroup->model()->value() )
	{
		case A1_OSC:
			a1_graph->model()->setWaveToTriangle();
			Engine::getSong()->setModified();
			break;
		case A2_OSC:
			a2_graph->model()->setWaveToTriangle();
			Engine::getSong()->setModified();
			break;
		case B1_OSC:
			b1_graph->model()->setWaveToTriangle();
			Engine::getSong()->setModified();
			break;
		case B2_OSC:
			b2_graph->model()->setWaveToTriangle();
			Engine::getSong()->setModified();
			break;
	}
}


void WatsynView::sawWaveClicked()
{
	switch( m_selectedGraphGroup->model()->value() )
	{
		case A1_OSC:
			a1_graph->model()->setWaveToSaw();
			Engine::getSong()->setModified();
			break;
		case A2_OSC:
			a2_graph->model()->setWaveToSaw();
			Engine::getSong()->setModified();
			break;
		case B1_OSC:
			b1_graph->model()->setWaveToSaw();
			Engine::getSong()->setModified();
			break;
		case B2_OSC:
			b2_graph->model()->setWaveToSaw();
			Engine::getSong()->setModified();
			break;
	}
}


void WatsynView::sqrWaveClicked()
{
	switch( m_selectedGraphGroup->model()->value() )
	{
		case A1_OSC:
			a1_graph->model()->setWaveToSquare();
			Engine::getSong()->setModified();
			break;
		case A2_OSC:
			a2_graph->model()->setWaveToSquare();
			Engine::getSong()->setModified();
			break;
		case B1_OSC:
			b1_graph->model()->setWaveToSquare();
			Engine::getSong()->setModified();
			break;
		case B2_OSC:
			b2_graph->model()->setWaveToSquare();
			Engine::getSong()->setModified();
			break;
	}
}


void WatsynView::normalizeClicked()
{
	switch( m_selectedGraphGroup->model()->value() )
	{
		case A1_OSC:
			a1_graph->model()->normalize();
			Engine::getSong()->setModified();
			break;
		case A2_OSC:
			a2_graph->model()->normalize();
			Engine::getSong()->setModified();
			break;
		case B1_OSC:
			b1_graph->model()->normalize();
			Engine::getSong()->setModified();
			break;
		case B2_OSC:
			b2_graph->model()->normalize();
			Engine::getSong()->setModified();
			break;
	}
}


void WatsynView::invertClicked()
{
	switch( m_selectedGraphGroup->model()->value() )
	{
		case A1_OSC:
			a1_graph->model()->invert();
			Engine::getSong()->setModified();
			break;
		case A2_OSC:
			a2_graph->model()->invert();
			Engine::getSong()->setModified();
			break;
		case B1_OSC:
			b1_graph->model()->invert();
			Engine::getSong()->setModified();
			break;
		case B2_OSC:
			b2_graph->model()->invert();
			Engine::getSong()->setModified();
			break;
	}
}


void WatsynView::smoothClicked()
{
	switch( m_selectedGraphGroup->model()->value() )
	{
		case A1_OSC:
			a1_graph->model()->smooth();
			Engine::getSong()->setModified();
			break;
		case A2_OSC:
			a2_graph->model()->smooth();
			Engine::getSong()->setModified();
			break;
		case B1_OSC:
			b1_graph->model()->smooth();
			Engine::getSong()->setModified();
			break;
		case B2_OSC:
			b2_graph->model()->smooth();
			Engine::getSong()->setModified();
			break;
	}
}


void WatsynView::phaseLeftClicked()
{
	switch( m_selectedGraphGroup->model()->value() )
	{
		case A1_OSC:
			a1_graph->model()->shiftPhase( -15 );
			Engine::getSong()->setModified();
			break;
		case A2_OSC:
			a2_graph->model()->shiftPhase( -15 );
			Engine::getSong()->setModified();
			break;
		case B1_OSC:
			b1_graph->model()->shiftPhase( -15 );
			Engine::getSong()->setModified();
			break;
		case B2_OSC:
			b2_graph->model()->shiftPhase( -15 );
			Engine::getSong()->setModified();
			break;
	}
}


void WatsynView::phaseRightClicked()
{
	switch( m_selectedGraphGroup->model()->value() )
	{
		case A1_OSC:
			a1_graph->model()->shiftPhase( 15 );
			Engine::getSong()->setModified();
			break;
		case A2_OSC:
			a2_graph->model()->shiftPhase( 15 );
			Engine::getSong()->setModified();
			break;
		case B1_OSC:
			b1_graph->model()->shiftPhase( 15 );
			Engine::getSong()->setModified();
			break;
		case B2_OSC:
			b2_graph->model()->shiftPhase( 15 );
			Engine::getSong()->setModified();
			break;
	}
}


void WatsynView::loadClicked()
{
	QString fileName;
	switch( m_selectedGraphGroup->model()->value() )
	{
		case A1_OSC:
			a1_graph->model()->setWaveToUser();
			Engine::getSong()->setModified();
			break;
		case A2_OSC:
			a2_graph->model()->setWaveToUser();
			Engine::getSong()->setModified();
			break;
		case B1_OSC:
			b1_graph->model()->setWaveToUser();
			Engine::getSong()->setModified();
			break;
		case B2_OSC:
			b2_graph->model()->setWaveToUser();
			Engine::getSong()->setModified();
			break;
	}
}


void WatsynView::modelChanged()
{
	auto w = castModel<WatsynInstrument>();

	a1_volKnob -> setModel( &w -> a1_vol );
	a2_volKnob -> setModel( &w -> a2_vol );
	b1_volKnob -> setModel( &w -> b1_vol );
	b2_volKnob -> setModel( &w -> b2_vol );

	a1_panKnob -> setModel( &w -> a1_pan );
	a2_panKnob -> setModel( &w -> a2_pan );
	b1_panKnob -> setModel( &w -> b1_pan );
	b2_panKnob -> setModel( &w -> b2_pan );

	a1_multKnob -> setModel( &w -> a1_mult );
	a2_multKnob -> setModel( &w -> a2_mult );
	b1_multKnob -> setModel( &w -> b1_mult );
	b2_multKnob -> setModel( &w -> b2_mult );

	a1_ltuneKnob -> setModel( &w -> a1_ltune );
	a2_ltuneKnob -> setModel( &w -> a2_ltune );
	b1_ltuneKnob -> setModel( &w -> b1_ltune );
	b2_ltuneKnob -> setModel( &w -> b2_ltune );

	a1_rtuneKnob -> setModel( &w -> a1_rtune );
	a2_rtuneKnob -> setModel( &w -> a2_rtune );
	b1_rtuneKnob -> setModel( &w -> b1_rtune );
	b2_rtuneKnob -> setModel( &w -> b2_rtune );

	m_abmixKnob -> setModel( &w -> m_abmix );

	m_selectedGraphGroup -> setModel( &w -> m_selectedGraph );

	m_aModGroup -> setModel( &w -> m_amod );
	m_bModGroup -> setModel( &w -> m_bmod );

	a1_graph -> setModel( &w -> a1_graph );
	a2_graph -> setModel( &w -> a2_graph );
	b1_graph -> setModel( &w -> b1_graph );
	b2_graph -> setModel( &w -> b2_graph );

	m_envAmtKnob -> setModel( &w -> m_envAmt );
	m_envAttKnob -> setModel( &w -> m_envAtt );
	m_envHoldKnob -> setModel( &w -> m_envHold );
	m_envDecKnob -> setModel( &w -> m_envDec );

	m_xtalkKnob -> setModel( &w -> m_xtalk );
}


} // namespace gui



extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model *m, void * )
{
	return( new WatsynInstrument( static_cast<InstrumentTrack *>( m ) ) );
}


}


} // namespace lmms
