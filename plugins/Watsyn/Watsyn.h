/*
 * Watsyn.h - a 4-oscillator modulating wavetable synth
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


#ifndef WATSYN_H
#define WATSYN_H

#include "AudioResampler.h"
#include "Instrument.h"
#include "InstrumentView.h"
#include "Graph.h"
#include "AutomatableModel.h"
#include "TempoSyncKnob.h"
#include "WatsynParamHandoff.h"
#include <samplerate.h>

#include <atomic>
#include <span>
#include <vector>

namespace lmms
{

#define A1ROW 26
#define A2ROW 49
#define B1ROW 72
#define B2ROW 95


const int GRAPHLEN = 220; // don't change - must be same as the size of the widget

const int WAVERATIO = 32; // oversampling ratio

const int WAVELEN = GRAPHLEN * WAVERATIO;
const int PMOD_AMT = WAVELEN / 2;

const int	MOD_MIX = 0;
const int	MOD_AM = 1;
const int	MOD_RM = 2;
const int	MOD_PM = 3;
const int  NUM_MODS = 4;

const int	A1_OSC = 0;
const int	A2_OSC = 1;
const int	B1_OSC = 2;
const int	B2_OSC = 3;
const int	NUM_OSCS = 4;

class WatsynInstrument;

namespace gui
{
class AutomatableButtonGroup;
class PixmapButton;
class WatsynView;
}

/*!
 * An immutable snapshot of the four wavetables.
 *
 * The GUI thread publishes a new snapshot whenever a graph changes and every
 * note pins the snapshot that was current when it started. A sounding note
 * therefore keeps the wave it started with - the copy the upstream code made
 * per note - but notes that start while the same snapshot is current share
 * one buffer instead of one buffer each.
 */
class WatsynWave
{
public:
	//! Holders: the instrument's own reference plus one per note that pinned it.
	std::atomic<int> pins{1};

	float data[4][WAVELEN];
};

/*!
 * The per-note state of Watsyn.
 *
 * It lives in the note handle's inline storage - the same place Monstro's
 * synth lives - so a note start neither allocates nor takes a slot from a
 * pool of preallocated voices. How many Watsyn notes may sound at the same
 * time is the note manager's limit, not Watsyn's: there is no voice cap that
 * could silence or delay an admissible note.
 */
class WatsynVoice
{
public:
	WatsynVoice( WatsynInstrument * _parent, NotePlayHandle * _nph, WatsynWave * _wave );
	~WatsynVoice();

	//! Renders the mixed oscillator output straight into the note's buffer.
	void renderOutput( std::span<SampleFrame> out );

	inline sample_rate_t samplerate() const
	{
		return m_samplerate;
	}

private:
	//! Gives the wavetable snapshot back to the instrument.
	void releaseWave() noexcept;

	int m_amod;
	int m_bmod;

	sample_rate_t m_samplerate;
	NotePlayHandle * m_nph;

	WatsynInstrument * m_parent;
	WatsynWave * m_wave;

	float m_lphase [NUM_OSCS];
	float m_rphase [NUM_OSCS];
};

class WatsynInstrument : public Instrument
{
	Q_OBJECT
public:
	WatsynInstrument( InstrumentTrack * _instrument_track );
	~WatsynInstrument() override;

	void deleteNotePluginData( NotePlayHandle * _n ) override;

	void saveSettings( QDomDocument & _doc,
							QDomElement & _this ) override;
	void loadSettings( const QDomElement & _this ) override;

	QString nodeName() const override;

	float desiredReleaseTimeMs() const override
	{
		return 1.5f;
	}

	gui::PluginView* instantiateView( QWidget * _parent ) override;

public slots:
	void updateVolumes();
	void updateFreqA1();
	void updateFreqA2();
	void updateFreqB1();
	void updateFreqB2();
	void updateWaveA1();
	void updateWaveA2();
	void updateWaveB1();
	void updateWaveB2();

protected:
	//! The published oscillator parameters. The thread that owns the
	//! instrument publishes them, the audio thread takes the published set
	//! once per rendered period. The plain float arrays the upstream code
	//! kept here were written by one thread and read by another with
	//! nothing between them, which is a data race.
	WatsynParamHandoff<NUM_OSCS> m_paramHandoff;

private:
	void playNoteImpl(NotePlayHandle* _n, std::span<SampleFrame> out) override;

	//! Computes the oscillator parameters from the models. The models are
	//! the state of the instrument, and every model value is read once, so
	//! the set is one coherent computation.
	WatsynParamHandoff<NUM_OSCS>::Values computeParams() const;
	//! Computes the oscillator parameters and publishes them.
	void publishParams();

	inline float leftCh( float _vol, float _pan ) const
	{
		return ( _pan <= 0 ? 1.0 : 1.0 - ( _pan / 100.0 ) ) * _vol / 100.0;
	}

	inline float rightCh( float _vol, float _pan ) const
	{
		return ( _pan >= 0 ? 1.0 : 1.0 + ( _pan / 100.0 ) ) * _vol / 100.0;
	}

	// memcpy utilizing libsamplerate (src) for sinc interpolation
	inline void srccpy(float* _dst, float* _src)
	{
		auto srcIndex = f_cnt_t{0};
		auto dstIndex = f_cnt_t{0};

		m_resampler.reset();
		m_resampler.setRatio(WAVERATIO);

		while (dstIndex < WAVELEN)
		{
			const auto input = InterleavedBufferView<const float, 1>{_src + srcIndex, GRAPHLEN - srcIndex};
			const auto output = InterleavedBufferView<float, 1>{_dst + dstIndex, WAVELEN - dstIndex};
			const auto result = m_resampler.process(input, output);

			srcIndex = (srcIndex + result.inputFramesUsed) % GRAPHLEN;
			dstIndex += result.outputFramesGenerated;
		}
	}

	// memcpy utilizing cubic interpolation
/*	inline void cipcpy( float * _dst, float * _src )
	{
		// calculate cyclic tangents
		float tang[GRAPHLEN];
		tang[0] = ( _src[1] - _src[ GRAPHLEN - 1] ) / 2;
		tang[ GRAPHLEN - 1 ] = ( _src[0] - _src[ GRAPHLEN - 2 ] ) / 2;
		for( int i = 1; i < GRAPHLEN-1; i++ )
		{
			tang[i] = ( _src[i+1] - _src[i-1] ) / 2;
		}
		
		// calculate cspline
		for( int i=0; i < WAVELEN; i++ )
		{
			const float s1 = _src[ i / WAVERATIO ];
			const float s2 = _src[ ( i / WAVERATIO + 1 ) % GRAPHLEN ];
			const float m1 = tang[ i / WAVERATIO ];
			const float m2 = tang[ ( i / WAVERATIO + 1 ) % GRAPHLEN ];
			
			const float x = static_cast<float>( i % WAVERATIO ) / WAVERATIO;
			const float x2 = x * x;
			const float x3 = x * x * x;
			
			_dst[i] = ( ( x3 * 2.0 - x2 * 3.0 + 1.0 ) * s1 ) +
				( ( x3 * -2.0 + x2 * 3.0 ) * s2 ) +
				( ( x3 - x2 * 2 + x ) * m1 ) +
				( ( x3 - x2 ) * m2 );		
		}
	}*/

	AudioResampler m_resampler = AudioResampler{AudioResampler::Mode::SincFastest, 1};

	FloatModel a1_vol;
	FloatModel a2_vol;
	FloatModel b1_vol;
	FloatModel b2_vol;

	FloatModel a1_pan;
	FloatModel a2_pan;
	FloatModel b1_pan;
	FloatModel b2_pan;

	FloatModel a1_mult;
	FloatModel a2_mult;
	FloatModel b1_mult;
	FloatModel b2_mult;

	FloatModel a1_ltune;
	FloatModel a2_ltune;
	FloatModel b1_ltune;
	FloatModel b2_ltune;

	FloatModel a1_rtune;
	FloatModel a2_rtune;
	FloatModel b1_rtune;
	FloatModel b2_rtune;

	graphModel a1_graph;
	graphModel a2_graph;
	graphModel b1_graph;
	graphModel b2_graph;

	FloatModel m_abmix;

	FloatModel m_envAmt;

	TempoSyncKnobModel m_envAtt;
	TempoSyncKnobModel m_envHold;
	TempoSyncKnobModel m_envDec;

	FloatModel m_xtalk;

	IntModel m_amod;
	IntModel m_bmod;

	IntModel m_selectedGraph;
	
	float A1_wave [WAVELEN];
	float A2_wave [WAVELEN];
	float B1_wave [WAVELEN];
	float B2_wave [WAVELEN];

	//! The wavetable snapshot the next note pins.
	std::atomic<WatsynWave*> m_wave{nullptr};
	//! How many threads are inside the window in which a note pins the
	//! current snapshot. The GUI thread reads it to learn that no note is
	//! about to pin the snapshot it just retired.
	std::atomic<int> m_waveClaimers{0};
	//! Snapshots that may be filled again (GUI thread only).
	std::vector<WatsynWave*> m_freeWaves;
	//! Snapshots waiting for their last holder (GUI thread only).
	std::vector<WatsynWave*> m_retiredWaves;

	//! Publishes the current wavetables as the snapshot the next note pins.
	void publishWave();
	//! Pins the current snapshot for a note. Runs on the audio thread.
	WatsynWave* claimWave() noexcept;
	//! Moves the snapshots no note holds any more back into the free list.
	void recycleWaves();

	friend class WatsynVoice;
	friend class gui::WatsynView;
};


namespace gui
{


class WatsynView : public InstrumentViewFixedSize
{
	Q_OBJECT
public:
	WatsynView( Instrument * _instrument,
					QWidget * _parent );
	~WatsynView() override = default;

protected slots:
	void updateLayout();

	void sinWaveClicked();
	void triWaveClicked();
	void sawWaveClicked();
	void sqrWaveClicked();

	void smoothClicked();
	void normalizeClicked();
	void invertClicked();
	void phaseLeftClicked();
	void phaseRightClicked();
	void loadClicked();

private:
	void modelChanged() override;

	template<class T = Knob>
	auto makeKnob(int x, int y, const QString& hint, const QString& unit,
		const QString& objName) -> T*
	{
		T* knob = new T(KnobType::Styled, this);
		knob->move(x, y);
		knob->setHintText(hint, unit);
		knob->setObjectName(objName);
		knob->setFixedSize(19, 19);
		return knob;
	};

// knobs
	Knob * a1_volKnob;
	Knob * a2_volKnob;
	Knob * b1_volKnob;
	Knob * b2_volKnob;

	Knob * a1_panKnob;
	Knob * a2_panKnob;
	Knob * b1_panKnob;
	Knob * b2_panKnob;

	Knob * a1_multKnob;
	Knob * a2_multKnob;
	Knob * b1_multKnob;
	Knob * b2_multKnob;

	Knob * a1_ltuneKnob;
	Knob * a2_ltuneKnob;
	Knob * b1_ltuneKnob;
	Knob * b2_ltuneKnob;

	Knob * a1_rtuneKnob;
	Knob * a2_rtuneKnob;
	Knob * b1_rtuneKnob;
	Knob * b2_rtuneKnob;

	Knob * m_abmixKnob;

	Knob * m_envAmtKnob;

	TempoSyncKnob * m_envAttKnob;
	TempoSyncKnob * m_envHoldKnob;
	TempoSyncKnob * m_envDecKnob;

	Knob * m_xtalkKnob;

	AutomatableButtonGroup * m_selectedGraphGroup;
	AutomatableButtonGroup * m_aModGroup;
	AutomatableButtonGroup * m_bModGroup;

	Graph * a1_graph;
	Graph * a2_graph;
	Graph * b1_graph;
	Graph * b2_graph;

	PixmapButton * m_sinWaveButton;
	PixmapButton * m_triWaveButton;
	PixmapButton * m_sawWaveButton;
	PixmapButton * m_sqrWaveButton;
	PixmapButton * m_normalizeButton;
	PixmapButton * m_invertButton;
	PixmapButton * m_smoothButton;
	PixmapButton * m_phaseLeftButton;
	PixmapButton * m_phaseRightButton;
	PixmapButton * m_loadButton;

};


} // namespace gui

} // namespace lmms

#endif
