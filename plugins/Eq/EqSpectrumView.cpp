/* eqspectrumview.cpp - implementation of EqSpectrumView class.
*
* Copyright (c) 2014-2017, David French <dave/dot/french3/at/googlemail/dot/com>
*
* This file is part of LMMS - https://lmms.io
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

#include "EqSpectrumView.h"

#include <cmath>
#include <cstring>
#include <numbers>
#include <QPainter>
#include <QPen>

#include "AudioEngine.h"
#include "Engine.h"
#include "EqCurve.h"
#include "GuiApplication.h"
#include "MainWindow.h"

namespace lmms
{


EqAnalyser::EqAnalyser() :
	m_pendingFrames ( 0 ),
	m_energy ( 0.f ),
	m_sampleRate ( 1 ),
	m_active ( true ),
	m_inProgress ( false ),
	m_publishedEnergy ( 0.f ),
	m_publishSequence ( 0 ),
	m_publishHook ( nullptr ),
	m_publishHookContext ( nullptr )
{
	using namespace std::numbers;
	m_specBuf = ( fftwf_complex * ) fftwf_malloc( ( FFT_BUFFER_SIZE + 1 ) * sizeof( fftwf_complex ) );
	m_fftPlan = fftwf_plan_dft_r2c_1d( FFT_BUFFER_SIZE*2, m_buffer, m_specBuf, FFTW_ESTIMATE );

	//initialize Blackman-Harris window, constants taken from
	//https://en.wikipedia.org/wiki/Window_function#A_list_of_window_functions
	const float a0 = 0.35875f;
	const float a1 = 0.48829f;
	const float a2 = 0.14128f;
	const float a3 = 0.01168f;

	for (auto i = std::size_t{0}; i < FFT_BUFFER_SIZE; i++)
	{
		m_fftWindow[i] = a0 - a1 * std::cos(2 * pi_v<float> * i / static_cast<float>(FFT_BUFFER_SIZE - 1.0))
			+ a2 * std::cos(4 * pi_v<float> * i / static_cast<float>(FFT_BUFFER_SIZE - 1.0))
			- a3 * std::cos(6 * pi_v<float> * i / static_cast<float>(FFT_BUFFER_SIZE - 1.0));
	}
	clear();
}




EqAnalyser::~EqAnalyser()
{
	fftwf_destroy_plan( m_fftPlan );
	fftwf_free( m_specBuf );
}




void EqAnalyser::analyze(InterleavedBufferView<const float, 2> buf)
{
	//only analyse if the view is visible
	if ( m_active.load(std::memory_order_relaxed) )
	{
		m_inProgress.store(true, std::memory_order_relaxed);
		const int FFT_BUFFER_SIZE = 2048;
		f_cnt_t f = 0;
		int staged = m_pendingFrames;
		if (buf.frames() > FFT_BUFFER_SIZE)
		{
			// A block longer than the window: only its last window is analysed.
			staged = 0;
			m_pendingFrames = 0;
			f = buf.frames() - FFT_BUFFER_SIZE;
		}
		// merge channels into the window; the frames that do not fit into it are kept
		// for the next window instead of being dropped
		for (; f < buf.frames() && staged < FFT_BUFFER_SIZE; ++f)
		{
			m_pending[staged++] = (buf[f][0] + buf[f][1]) * 0.5f;
		}

		if( staged < FFT_BUFFER_SIZE )
		{
			m_pendingFrames = staged;
			m_inProgress.store(false, std::memory_order_relaxed);
			return;
		}

		// the window is full: it goes to the FFT, the rest of the block stays pending
		for( int i = 0; i < FFT_BUFFER_SIZE; i++ )
		{
			m_buffer[i] = m_pending[i];
		}
		m_pendingFrames = 0;
		for (; f < buf.frames(); ++f)
		{
			m_pending[m_pendingFrames++] = (buf[f][0] + buf[f][1]) * 0.5f;
		}

		m_sampleRate.store(Engine::audioEngine()->outputSampleRate(), std::memory_order_relaxed);
		const int LOWEST_FREQ = 0;
		const int HIGHEST_FREQ = m_sampleRate.load(std::memory_order_relaxed) / 2;

		//apply FFT window
		for( int i = 0; i < FFT_BUFFER_SIZE; i++ )
		{
			m_buffer[i] = m_buffer[i] * m_fftWindow[i];
		}

		fftwf_execute( m_fftPlan );
		absspec( m_specBuf, m_absSpecBuf, FFT_BUFFER_SIZE+1 );

		compressbands( m_absSpecBuf, m_bands, FFT_BUFFER_SIZE+1,
					   MAX_BANDS,
					   ( int )( LOWEST_FREQ * ( FFT_BUFFER_SIZE + 1 ) / ( float )( m_sampleRate.load(std::memory_order_relaxed) / 2 ) ),
					   ( int )( HIGHEST_FREQ * ( FFT_BUFFER_SIZE +  1) / ( float )( m_sampleRate.load(std::memory_order_relaxed) / 2 ) ) );
		const float mergedBufferMaximum = maximum( m_buffer, FFT_BUFFER_SIZE );
		m_energy.store(mergedBufferMaximum == 0.0f ? 0.0f
			: maximum( m_bands, MAX_BANDS ) / mergedBufferMaximum, std::memory_order_relaxed);
		publish();

		m_inProgress.store(false, std::memory_order_relaxed);
		m_active.store(false, std::memory_order_relaxed);
	}
}



void EqAnalyser::publish()
{
	// The GUI reads the published spectrum. The odd sequence number tells a reader that
	// the copy it is taking is not a whole spectrum.
	m_publishSequence.fetch_add(1, std::memory_order_acq_rel);
	for (int i = 0; i < MAX_BANDS; ++i)
	{
		m_publishedBands[i].store(m_bands[i], std::memory_order_relaxed);
		if (i == MAX_BANDS / 2 && m_publishHook != nullptr)
		{
			m_publishHook(m_publishHookContext, i + 1);
		}
	}
	m_publishedEnergy.store(m_energy.load(std::memory_order_relaxed), std::memory_order_relaxed);
	m_publishSequence.fetch_add(1, std::memory_order_release);
}



bool EqAnalyser::copySpectrum(float* bands, float& energy) const
{
	const unsigned int sequence = m_publishSequence.load(std::memory_order_acquire);
	if ((sequence & 1u) == 0)
	{
		for (int i = 0; i < MAX_BANDS; ++i)
		{
			bands[i] = m_publishedBands[i].load(std::memory_order_relaxed);
		}
		const float publishedEnergy = m_publishedEnergy.load(std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_acquire);
		if (m_publishSequence.load(std::memory_order_relaxed) == sequence)
		{
			// remember the spectrum, so a reader that arrives during the next
			// publication still gets a whole one
			std::memcpy(m_lastSpectrum, bands, sizeof(m_lastSpectrum));
			m_lastEnergy = publishedEnergy;
			energy = publishedEnergy;
			return true;
		}
	}
	// A publication is in flight: hand out the last whole spectrum instead of a half
	// written one.
	std::memcpy(bands, m_lastSpectrum, sizeof(m_lastSpectrum));
	energy = m_lastEnergy;
	return false;
}



void EqAnalyser::setPublishHook(PublishHook hook, void* context)
{
	m_publishHook = hook;
	m_publishHookContext = context;
}




float EqAnalyser::getEnergy() const
{
	return m_energy.load(std::memory_order_relaxed);
}




int EqAnalyser::getSampleRate() const
{
	return m_sampleRate.load(std::memory_order_relaxed);
}




bool EqAnalyser::getActive() const
{
	return m_active.load(std::memory_order_relaxed);
}




void EqAnalyser::setActive(bool active)
{
	m_active.store(active, std::memory_order_relaxed);
}




bool EqAnalyser::getInProgress() const
{
	return m_inProgress.load(std::memory_order_relaxed);
}




void EqAnalyser::clear()
{
	m_pendingFrames = 0;
	m_energy.store(0.f, std::memory_order_relaxed);
	memset( m_buffer, 0, sizeof( m_buffer ) );
	memset( m_bands, 0, sizeof( m_bands ) );
	// the GUI must not keep painting a spectrum that was cleared
	publish();
}



namespace gui
{

EqSpectrumView::EqSpectrumView(EqAnalyser *b, QWidget *_parent) :
	QWidget( _parent ),
	m_analyser( b ),
	m_peakSum(0.),
	m_periodicalUpdate( false ),
	m_spectrumBands( MAX_BANDS, 0.f )
{
	setFixedSize( 450, 200 );
	connect( getGUI()->mainWindow(), SIGNAL( periodicUpdate() ), this, SLOT( periodicalUpdate() ) );
	setAttribute( Qt::WA_TranslucentBackground, true );
	m_skipBands = MAX_BANDS * 0.5;
	const float totalLength = std::log10(20000);
	m_pixelsPerUnitWidth = width() / totalLength ;
	m_scale = 1.5;
	m_color = QColor( 255, 255, 255, 255 );
	for ( int i = 0 ; i < MAX_BANDS ; i++ )
	{
		m_bandHeight.append( 0 );
	}
}




void EqSpectrumView::paintEvent(QPaintEvent *event)
{
	// The analyser publishes its spectrum from the audio thread: the GUI reads a whole
	// snapshot of it, never the array the analyser is writing.
	float energy = 0.f;
	m_analyser->copySpectrum( m_spectrumBands.data(), energy );
	if (energy <= 0. && m_peakSum <= 0) { return; }

	const int fh = height();
	const int LOWER_Y = -36;	// dB
	QPainter painter( this );
	painter.setPen( QPen( m_color, 1, Qt::SolidLine, Qt::RoundCap, Qt::BevelJoin ) );
	painter.setRenderHint(QPainter::Antialiasing, true);

	if( m_analyser->getInProgress() || m_periodicalUpdate == false )
	{
		//only paint the cached path
		painter.fillPath( m_path, QBrush( m_color ) );
		return;
	}

	m_periodicalUpdate = false;
	//Now we calculate the path
	m_path = QPainterPath();
	const float *bands = m_spectrumBands.data();
	m_path.moveTo( 0, height() );
	m_peakSum = 0;
	const float fallOff = 1.07f;
	for( int x = 0; x < MAX_BANDS; ++x, ++bands )
	{
		float peak = *bands != 0. ? (fh * 2.0 / 3.0 * (20. * std::log10(*bands / energy) - LOWER_Y) / (-LOWER_Y)) : 0.;

		if( peak < 0 )
		{
			peak = 0;
		}
		else if( peak >= fh )
		{
			continue;
		}

		if ( peak > m_bandHeight[x] )
		{
			m_bandHeight[x] = peak;
		}
		else
		{
			m_bandHeight[x] = m_bandHeight[x] / fallOff;
		}

		if( m_bandHeight[x] < 0 )
		{
			m_bandHeight[x] = 0;
		}

		m_path.lineTo( EqHandle::freqToXPixel( bandToFreq( x ), width() ), fh - m_bandHeight[x] );
		m_peakSum += m_bandHeight[x];
	}

	m_path.lineTo( width(), height() );
	m_path.closeSubpath();
	painter.fillPath( m_path, QBrush( m_color ) );
	painter.drawPath( m_path );
}




QColor EqSpectrumView::getColor() const
{
	return m_color;
}




void EqSpectrumView::setColor( const QColor &value )
{
	m_color = value;
}




float EqSpectrumView::bandToFreq( int index )
{
	return index * m_analyser->getSampleRate() / ( MAX_BANDS * 2 );
}




void EqSpectrumView::periodicalUpdate()
{
	m_periodicalUpdate = true;
	m_analyser->setActive( isVisible() );
	update();
}


} // namespace gui

} // namespace lmms
