/*
 * LfoController.cpp - implementation of class controller which handles
 *                      remote-control of AutomatableModels
 *
 * Copyright (c) 2008 Paul Giblock <drfaygo/at/gmail.com>
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

#include "LfoController.h"

#include <QDomElement>
#include <QFileInfo>
#include <QTimer>

#include <utility>

#include "AudioEngine.h"
#include "Oscillator.h"
#include "PathUtil.h"
#include "Song.h"

namespace lmms
{

static_assert(std::atomic<const SampleBuffer*>::is_always_lock_free);
static_assert(std::atomic<unsigned int>::is_always_lock_free);
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<sample_t (*)(const float)>::is_always_lock_free);

namespace
{

//! The sample function of a wave shape. The render path derives it from the
//! shape it read, so the shape and the function can never disagree.
sample_t (*sampleFunctionFor( Oscillator::WaveShape shape ))( const float )
{
	switch( shape )
	{
		case Oscillator::WaveShape::Triangle: return &Oscillator::triangleSample;
		case Oscillator::WaveShape::Saw: return &Oscillator::sawSample;
		case Oscillator::WaveShape::Square: return &Oscillator::squareSample;
		case Oscillator::WaveShape::MoogSaw: return &Oscillator::moogSawSample;
		case Oscillator::WaveShape::Exponential: return &Oscillator::expSample;
		case Oscillator::WaveShape::WhiteNoise: return &Oscillator::noiseSample;
		case Oscillator::WaveShape::UserDefined: return nullptr;
		case Oscillator::WaveShape::Sine:
		default: return &Oscillator::sinSample;
	}
}

} // namespace


LfoController::LfoController( Model * _parent ) :
	Controller( ControllerType::Lfo, _parent, tr( "LFO Controller" ) ),
	m_baseModel(0.5f, 0.f, 1.f, 0.001f, this, tr("Base value")),
	m_speedModel(2.f, 0.01f, 20.f, 0.0001f, 20000.f, this, tr("Oscillator speed")),
	m_amountModel(1.f, -1.f, 1.f, 0.005f, this, tr("Oscillator amount")),
	m_phaseModel( 0.0, 0.0, 360.0, 4.0, this, tr( "Oscillator phase" ) ),
	m_waveModel( static_cast<int>(Oscillator::WaveShape::Sine), 0, Oscillator::NumWaveShapes,
			this, tr( "Oscillator waveform" ) ),
	m_multiplierModel( 0, 0, 2, this, tr( "Frequency Multiplier" ) ),
	m_duration( 1000 ),
	m_phaseOffset( 0 ),
	m_currentPhase( 0 ),
	m_sampleFunction( &Oscillator::sinSample ),
	m_renderedWaveShape( static_cast<int>(Oscillator::WaveShape::Sine) ),
	m_heldSample( 0.0f ),
	m_userDefSampleBuffer(std::make_shared<SampleBuffer>()),
	m_userDefSampleBufferForRender(m_userDefSampleBuffer.get())
{
	setSampleExact( true );
	connect( &m_speedModel, SIGNAL(dataChanged()),
			this, SLOT(updateDuration()), Qt::DirectConnection );
	connect( &m_multiplierModel, SIGNAL(dataChanged()),
			this, SLOT(updateDuration()), Qt::DirectConnection );
	connect( Engine::audioEngine(), SIGNAL(sampleRateChanged()),
			this, SLOT(updateDuration()));

	connect( Engine::getSong(), SIGNAL(playbackStateChanged()),
			this, SLOT(updatePhase()));
	connect(Engine::getSong(), &Song::playbackPositionJumped,
			this, &LfoController::updatePhase);

	updateDuration();
}




LfoController::~LfoController()
{
	m_baseModel.disconnect( this );
	m_speedModel.disconnect( this );
	m_amountModel.disconnect( this );
	m_phaseModel.disconnect( this );
	m_waveModel.disconnect( this );
	m_multiplierModel.disconnect( this );
}


void LfoController::updateValueBuffer()
{
	// ---- apply the state the GUI published, at this safe render boundary ----
	// The sample function and the white-noise held sample belong to the render
	// thread: they are derived here from the shape this period renders, never
	// written by a GUI slot.
	const auto waveShape = static_cast<Oscillator::WaveShape>(
				static_cast<int>( m_waveModel.value() ) );
	if( static_cast<int>( waveShape ) != m_renderedWaveShape )
	{
		m_renderedWaveShape = static_cast<int>( waveShape );
		m_sampleFunction.store( sampleFunctionFor( waveShape ) );
		if( waveShape == Oscillator::WaveShape::WhiteNoise )
		{
			m_heldSample.store( Oscillator::noiseSample( 0.0f ) );
		}
	}

	const float phaseOffset = m_phaseModel.value() / 360.0f;
	m_phaseOffset.store( phaseOffset );
	float phase = m_currentPhase.load() + phaseOffset;
	float phasePrev = 0.0f;
	// One snapshot per period: the UI thread may publish new values meanwhile.
	const float duration = m_duration.load(std::memory_order_acquire);
	const SampleFunction sampleFunction = m_sampleFunction.load(std::memory_order_acquire);

	// roll phase up until we're in sync with period counter
	m_bufferLastUpdated++;
	if( m_bufferLastUpdated < s_periods )
	{
		f_cnt_t diff = s_periods - m_bufferLastUpdated;
		phase += static_cast<float>( Engine::audioEngine()->framesPerPeriod() * diff ) / duration;
		m_bufferLastUpdated += diff;
	}

	float amount = m_amountModel.value();
	// The amount is an AutomatableModel in the same render-owned registry. It
	// must be prepared before its buffer is read by this one controller update.
	m_amountModel.prepareValueBufferForRender();
	ValueBuffer *amountBuffer = m_amountModel.valueBuffer();
	int amountInc = amountBuffer ? 1 : 0;
	float *amountPtr = amountBuffer ? &(amountBuffer->values()[ 0 ] ) : &amount;
	const bool usesUserWave = waveShape == Oscillator::WaveShape::UserDefined;
	const auto* userWave = usesUserWave ? acquireUserDefSampleBufferForRender() : nullptr;

	for( float& f : m_valueBuffer )
	{
		float currentSample = 0;
		switch (waveShape)
		{
		case Oscillator::WaveShape::WhiteNoise:
		{
			if (sampleFunction != nullptr && absFraction(phase) < absFraction(phasePrev))
			{
				// Resample when phase period has completed
				m_heldSample.store(sampleFunction(phase), std::memory_order_relaxed);
			}
			currentSample = m_heldSample.load(std::memory_order_relaxed);
			break;
		}
		case Oscillator::WaveShape::UserDefined:
		{
			currentSample = Oscillator::userWaveSample(userWave, phase);
			break;
		}
		default:
		{
			if (sampleFunction != nullptr)
			{
				currentSample = sampleFunction(phase);
			}
		}
	}

		f = std::clamp(m_baseModel.value() + (*amountPtr * currentSample / 2.0f), 0.0f, 1.0f);

		phasePrev = phase;
		phase += 1.0 / duration;
		amountPtr += amountInc;
	}
	if (usesUserWave) { releaseUserDefSampleBufferForRender(); }

	m_currentPhase.store(absFraction(phase - phaseOffset));
	m_bufferLastUpdated = s_periods;
}


void LfoController::setUserDefSampleBuffer(std::shared_ptr<const SampleBuffer> buffer)
{
	if (!buffer) { buffer = SampleBuffer::emptyBuffer(); }
	m_retiredUserDefSampleBuffers.push_back(std::move(m_userDefSampleBuffer));
	m_userDefSampleBuffer = std::move(buffer);
	m_userDefSampleBufferForRender.store(m_userDefSampleBuffer.get(), std::memory_order_seq_cst);
	reclaimRetiredUserDefSampleBuffers();
	if (!m_retiredUserDefSampleBuffers.empty()) { scheduleUserDefSampleBufferReclaim(); }
}


const SampleBuffer* LfoController::acquireUserDefSampleBufferForRender() noexcept
{
	m_userWaveRenderReaders.fetch_add(1, std::memory_order_seq_cst);
	return m_userDefSampleBufferForRender.load(std::memory_order_seq_cst);
}


void LfoController::releaseUserDefSampleBufferForRender() noexcept
{
	m_userWaveRenderReaders.fetch_sub(1, std::memory_order_seq_cst);
}


void LfoController::reclaimRetiredUserDefSampleBuffers() noexcept
{
	if (m_userWaveRenderReaders.load(std::memory_order_seq_cst) != 0) { return; }

	m_retiredUserDefSampleBuffers.clear();
	if (m_userWaveReclaimTimer != nullptr) { m_userWaveReclaimTimer->stop(); }
}


void LfoController::scheduleUserDefSampleBufferReclaim()
{
	if (m_userWaveReclaimTimer == nullptr)
	{
		m_userWaveReclaimTimer = new QTimer(this);
		m_userWaveReclaimTimer->setObjectName(QStringLiteral("lfoRetiredUserWaveReclaimTimer"));
		m_userWaveReclaimTimer->setTimerType(Qt::VeryCoarseTimer);
		m_userWaveReclaimTimer->setInterval(1000);
		QObject::connect(m_userWaveReclaimTimer, &QTimer::timeout, this, [this] {
			reclaimRetiredUserDefSampleBuffers();
		});
	}
	m_userWaveReclaimTimer->start();
}

void LfoController::updatePhase()
{
	Engine::audioEngine()->requestChangeInModel();
	m_currentPhase.store(Engine::getSong()->getFrames() / m_duration.load(std::memory_order_acquire));
	m_bufferLastUpdated = s_periods - 1;
	Engine::audioEngine()->doneChangeInModel();
}


void LfoController::updateDuration()
{
	float newDurationF = Engine::audioEngine()->outputSampleRate() * m_speedModel.value();

	switch(m_multiplierModel.value() )
	{
		case 1:
			newDurationF /= 100.0;
			break;

		case 2:
			newDurationF *= 100.0;
			break;

		default:
			break;
	}

	m_duration.store(newDurationF, std::memory_order_release);
}

void LfoController::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	Controller::saveSettings( _doc, _this );

	m_baseModel.saveSettings( _doc, _this, "base" );
	m_speedModel.saveSettings( _doc, _this, "speed" );
	m_amountModel.saveSettings( _doc, _this, "amount" );
	m_phaseModel.saveSettings( _doc, _this, "phase" );
	m_waveModel.saveSettings( _doc, _this, "wave" );
	m_multiplierModel.saveSettings( _doc, _this, "multiplier" );
	_this.setAttribute("userwavefile", m_userDefSampleBuffer->audioFile());
}



void LfoController::loadSettings( const QDomElement & _this )
{
	Controller::loadSettings( _this );

	m_baseModel.loadSettings( _this, "base" );
	m_speedModel.loadSettings( _this, "speed" );
	m_amountModel.loadSettings( _this, "amount" );
	m_phaseModel.loadSettings( _this, "phase" );
	m_waveModel.loadSettings( _this, "wave" );
	m_multiplierModel.loadSettings( _this, "multiplier" );

	if (const auto userWaveFile = _this.attribute("userwavefile"); !userWaveFile.isEmpty())
	{
		if (QFileInfo(PathUtil::toAbsolute(userWaveFile)).exists())
		{
			setUserDefSampleBuffer(SampleBuffer::fromFile(_this.attribute("userwavefile")));
		}
		else { Engine::getSong()->collectError(QString("%1: %2").arg(tr("Sample not found"), userWaveFile)); }
	}

}



QString LfoController::nodeName() const
{
	return( "lfocontroller" );
}



gui::ControllerDialog * LfoController::createDialog( QWidget * _parent )
{
	return new gui::LfoControllerDialog( this, _parent );
}


} // namespace lmms
