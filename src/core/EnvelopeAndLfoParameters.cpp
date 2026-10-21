/*
 * EnvelopeAndLfoParameters.cpp - class EnvelopeAndLfoParameters
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "EnvelopeAndLfoParameters.h"

#include <QDomElement>
#include <QFileInfo>

#include "AudioEngine.h"
#include "Engine.h"
#include "Oscillator.h"
#include "PathUtil.h"
#include "Song.h"

namespace lmms
{

// how long should be each envelope-segment maximal (e.g. attack)?
extern const float SECS_PER_ENV_SEGMENT = 5.0f;
// how long should be one LFO-oscillation maximal?
extern const float SECS_PER_LFO_OSCILLATION = 20.0f;
// minimum number of frames for ENV/LFO stages that mustn't be '0'
const f_cnt_t minimumFrames = 1;


EnvelopeAndLfoParameters::LfoInstances * EnvelopeAndLfoParameters::s_lfoInstances = nullptr;

namespace
{
//! The test seams: the publisher calls them while it publishes a snapshot.
std::function<void()> g_publicationHook;
std::function<void()> g_publicationStartHook;
} // namespace


void EnvelopeAndLfoParameters::LfoInstances::trigger()
{
	QMutexLocker m( &m_lfoListMutex );
	for (const auto& lfo : m_lfos)
	{
		// the LFO phase moved, so the shape cache of that LFO is stale
		lfo->m_lfoFrame += Engine::audioEngine()->framesPerPeriod();
	}
}




void EnvelopeAndLfoParameters::LfoInstances::reset()
{
	QMutexLocker m( &m_lfoListMutex );
	for (const auto& lfo : m_lfos)
	{
		lfo->m_lfoFrame = 0;
	}
}




void EnvelopeAndLfoParameters::LfoInstances::add( EnvelopeAndLfoParameters * lfo )
{
	QMutexLocker m( &m_lfoListMutex );
	m_lfos.append( lfo );
}




void EnvelopeAndLfoParameters::LfoInstances::remove( EnvelopeAndLfoParameters * lfo )
{
	QMutexLocker m( &m_lfoListMutex );
	m_lfos.removeAll( lfo );
}




EnvelopeAndLfoParameters::EnvelopeAndLfoParameters(
					float _value_for_zero_amount,
							Model * _parent ) :
	Model( _parent ),
	m_used( false ),
	m_predelayModel(0.f, 0.f, 2.f, 0.001f, this, tr("Env pre-delay")),
	m_attackModel(0.f, 0.f, 2.f, 0.001f, this, tr("Env attack")),
	m_holdModel(0.5f, 0.f, 2.f, 0.001f, this, tr("Env hold")),
	m_decayModel(0.5f, 0.f, 2.f, 0.001f, this, tr("Env decay")),
	m_sustainModel(0.5f, 0.f, 1.f, 0.001f, this, tr("Env sustain")),
	m_releaseModel(0.1f, 0.f, 2.f, 0.001f, this, tr("Env release")),
	m_amountModel(0.f, -1.f, 1.f, 0.005f, this, tr("Env mod amount")),
	m_valueForZeroAmount( _value_for_zero_amount ),
	m_lfoPredelayModel(0.f, 0.f, 1.f, 0.001f, this, tr("LFO pre-delay")),
	m_lfoAttackModel(0.f, 0.f, 1.f, 0.001f, this, tr("LFO attack")),
	m_lfoSpeedModel(0.1f, 0.001f, 1.f, 0.0001f,
				SECS_PER_LFO_OSCILLATION * 1000.f, this,
							tr("LFO frequency")),
	m_lfoAmountModel(0.f, -1.f, 1.f, 0.005f, this, tr("LFO mod amount")),
	m_lfoWaveModel( static_cast<int>(LfoShape::SineWave), 0, NumLfoShapes, this, tr( "LFO wave shape" ) ),
	m_x100Model( false, this, tr( "LFO frequency x 100" ) ),
	m_controlEnvAmountModel( false, this, tr( "Modulate env amount" ) ),
	m_lfoFrame( 0 ),
	m_lfoShapeData(nullptr),
	m_lfoShapeDataVersion( 0 ),
	m_lfoShapeDataFrame( 0 ),
	m_audioState( new AudioState() ),
	m_publicationCounter( 0 ),
	m_audioStateReaders( 0 ),
	m_heldAudioState( nullptr )
{
	m_amountModel.setCenterValue( 0 );
	m_lfoAmountModel.setCenterValue( 0 );

	if( s_lfoInstances == nullptr )
	{
		s_lfoInstances = new LfoInstances();
	}

	instances()->add( this );

	connect( &m_predelayModel, SIGNAL(dataChanged()),
			this, SLOT(updateSampleVars()), Qt::DirectConnection );
	connect( &m_attackModel, SIGNAL(dataChanged()),
			this, SLOT(updateSampleVars()), Qt::DirectConnection );
	connect( &m_holdModel, SIGNAL(dataChanged()),
			this, SLOT(updateSampleVars()), Qt::DirectConnection );
	connect( &m_decayModel, SIGNAL(dataChanged()),
			this, SLOT(updateSampleVars()), Qt::DirectConnection );
	connect( &m_sustainModel, SIGNAL(dataChanged()),
			this, SLOT(updateSampleVars()), Qt::DirectConnection );
	connect( &m_releaseModel, SIGNAL(dataChanged()),
			this, SLOT(updateSampleVars()), Qt::DirectConnection );
	connect( &m_amountModel, SIGNAL(dataChanged()),
			this, SLOT(updateSampleVars()), Qt::DirectConnection );

	connect( &m_lfoPredelayModel, SIGNAL(dataChanged()),
			this, SLOT(updateSampleVars()), Qt::DirectConnection );
	connect( &m_lfoAttackModel, SIGNAL(dataChanged()),
			this, SLOT(updateSampleVars()), Qt::DirectConnection );
	connect( &m_lfoSpeedModel, SIGNAL(dataChanged()),
			this, SLOT(updateSampleVars()), Qt::DirectConnection );
	connect( &m_lfoAmountModel, SIGNAL(dataChanged()),
			this, SLOT(updateSampleVars()), Qt::DirectConnection );
	connect( &m_lfoWaveModel, SIGNAL(dataChanged()),
			this, SLOT(updateSampleVars()), Qt::DirectConnection );
	connect( &m_x100Model, SIGNAL(dataChanged()),
			this, SLOT(updateSampleVars()), Qt::DirectConnection );

	connect( Engine::audioEngine(), SIGNAL(sampleRateChanged()),
				this, SLOT(updateSampleVars()));


	m_lfoShapeData =
		new sample_t[Engine::audioEngine()->framesPerPeriod()];

	updateSampleVars();
}




EnvelopeAndLfoParameters::~EnvelopeAndLfoParameters()
{
	m_predelayModel.disconnect( this );
	m_attackModel.disconnect( this );
	m_holdModel.disconnect( this );
	m_decayModel.disconnect( this );
	m_sustainModel.disconnect( this );
	m_releaseModel.disconnect( this );
	m_amountModel.disconnect( this );
	m_lfoPredelayModel.disconnect( this );
	m_lfoAttackModel.disconnect( this );
	m_lfoSpeedModel.disconnect( this );
	m_lfoAmountModel.disconnect( this );
	m_lfoWaveModel.disconnect( this );
	m_x100Model.disconnect( this );

	delete[] m_lfoShapeData;

	// The published snapshots: the current one and the retired ones a reader
	// may still hold. The object is going away, so no reader may hold them.
	delete m_audioState.exchange(nullptr);
	for (AudioState* state : m_retiredAudioStates)
	{
		delete state;
	}
	m_retiredAudioStates.clear();

	instances()->remove( this );

	if( instances()->isEmpty() )
	{
		delete instances();
		s_lfoInstances = nullptr;
	}
}




inline sample_t EnvelopeAndLfoParameters::lfoShapeSample( f_cnt_t _frame_offset,
							const AudioState & _state,
							f_cnt_t _lfoFrame )
{
	const f_cnt_t oscillationFrames = _state.lfoOscillationFrames;
	f_cnt_t frame = ( _lfoFrame + _frame_offset ) % oscillationFrames;
	const float phase = frame / static_cast<float>( oscillationFrames );
	sample_t shape_sample;
	switch( static_cast<LfoShape>(m_lfoWaveModel.value())  )
	{
		case LfoShape::TriangleWave:
			shape_sample = Oscillator::triangleSample( phase );
			break;
		case LfoShape::SquareWave:
			shape_sample = Oscillator::squareSample( phase );
			break;
		case LfoShape::SawWave:
			shape_sample = Oscillator::sawSample( phase );
			break;
		case LfoShape::UserDefinedWave:
			// the wave of the published snapshot: the one the writer published,
			// and the one that cannot be freed while this snapshot is held.
			shape_sample = Oscillator::userWaveSample(_state.userWave.get(), phase);
			break;
		case LfoShape::RandomWave:
			if( frame == 0 || !m_randomInitialized )
			{
				m_random = Oscillator::noiseSample( 0.0f );
				m_randomInitialized = true;
			}
			shape_sample = m_random;
			break;
		case LfoShape::SineWave:
		default:
			shape_sample = Oscillator::sinSample( phase );
			break;
	}
	return shape_sample * _state.lfoAmount;
}




void EnvelopeAndLfoParameters::updateLfoShapeData( const AudioState & _state )
{
	const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
	const f_cnt_t lfoFrame = m_lfoFrame.load(std::memory_order_relaxed);
	for( f_cnt_t offset = 0; offset < frames; ++offset )
	{
		m_lfoShapeData[offset] = lfoShapeSample( offset, _state, lfoFrame );
	}
	// The cache belongs to this snapshot and to this phase. Publishing the key
	// after the data cannot make a cache that was built from another snapshot
	// look current.
	m_lfoShapeDataVersion.store( _state.version, std::memory_order_release );
	m_lfoShapeDataFrame.store( lfoFrame, std::memory_order_release );
}




inline void EnvelopeAndLfoParameters::fillLfoLevel( float * _buf,
							f_cnt_t _frame,
							const f_cnt_t _frames,
							const AudioState & _state )
{
	if( _state.lfoAmountIsZero )
	{
		for( f_cnt_t offset = 0; offset < _frames; ++offset )
		{
			*_buf++ = 0.0f;
		}
		return;
	}
	f_cnt_t offset = 0;
	for( ; offset < _frames && _frame < _state.lfoPredelayFrames;
								++offset, ++_frame )
	{
		*_buf++ = 0.0f;
	}
	if( offset == _frames )
	{
		return;
	}
	_frame -= _state.lfoPredelayFrames;

	// The cached shape data is used only for the snapshot it was built from
	// and for the phase it was built at: a snapshot that was published while
	// this one was held cannot be answered with this cache.
	if( m_lfoShapeDataVersion.load(std::memory_order_acquire) != _state.version ||
		m_lfoShapeDataFrame.load(std::memory_order_acquire) !=
			m_lfoFrame.load(std::memory_order_relaxed) )
	{
		updateLfoShapeData( _state );
	}

	const float lafI = 1.0f / std::max(minimumFrames, _state.lfoAttackFrames);
	for( ; offset < _frames && _frame < _state.lfoAttackFrames; ++offset,
								++_frame )
	{
		*_buf++ = m_lfoShapeData[offset] * _frame * lafI;
	}
	for( ; offset < _frames; ++offset )
	{
		*_buf++ = m_lfoShapeData[offset];
	}
}




void EnvelopeAndLfoParameters::fillLevel( float * _buf, f_cnt_t _frame,
						const f_cnt_t _release_begin,
						const f_cnt_t _frames )
{
	// The audio path takes no lock and waits for nothing: it reads the
	// published parameter snapshot, which the writer hands over atomically.
	const AudioState * const state = acquireAudioState();

	fillLfoLevel( _buf, _frame, _frames, *state );

	const sample_t * const pahdEnv = state->pahdEnv.data();
	const sample_t * const rEnv = state->rEnv.data();

	for( f_cnt_t offset = 0; offset < _frames; ++offset, ++_buf, ++_frame )
	{
		float env_level;
		if( _frame < _release_begin )
		{
			if( _frame < state->pahdFrames )
			{
				env_level = pahdEnv[_frame];
			}
			else
			{
				env_level = state->sustainLevel;
			}
		}
		else if( ( _frame - _release_begin ) < state->rFrames )
		{
			env_level = rEnv[_frame - _release_begin] *
				( ( _release_begin < state->pahdFrames ) ?
				pahdEnv[_release_begin] : state->sustainLevel );
		}
		else
		{
			env_level = 0.0f;
		}

		// at this point, *_buf is LFO level
		*_buf = m_controlEnvAmountModel.value() ?
			env_level * ( 0.5f + *_buf ) :
			env_level + *_buf;
	}

	releaseAudioState();
}




void EnvelopeAndLfoParameters::saveSettings( QDomDocument & _doc,
							QDomElement & _parent )
{
	m_predelayModel.saveSettings( _doc, _parent, "pdel" );
	m_attackModel.saveSettings( _doc, _parent, "att" );
	m_holdModel.saveSettings( _doc, _parent, "hold" );
	m_decayModel.saveSettings( _doc, _parent, "dec" );
	m_sustainModel.saveSettings( _doc, _parent, "sustain" );
	m_releaseModel.saveSettings( _doc, _parent, "rel" );
	m_amountModel.saveSettings( _doc, _parent, "amt" );
	m_lfoWaveModel.saveSettings( _doc, _parent, "lshp" );
	m_lfoPredelayModel.saveSettings( _doc, _parent, "lpdel" );
	m_lfoAttackModel.saveSettings( _doc, _parent, "latt" );
	m_lfoSpeedModel.saveSettings( _doc, _parent, "lspd" );
	m_lfoAmountModel.saveSettings( _doc, _parent, "lamt" );
	m_x100Model.saveSettings( _doc, _parent, "x100" );
	m_controlEnvAmountModel.saveSettings( _doc, _parent, "ctlenvamt" );
	_parent.setAttribute("userwavefile", m_userWave->audioFile());
}




void EnvelopeAndLfoParameters::loadSettings( const QDomElement & _this )
{
	m_predelayModel.loadSettings( _this, "pdel" );
	m_attackModel.loadSettings( _this, "att" );
	m_holdModel.loadSettings( _this, "hold" );
	m_decayModel.loadSettings( _this, "dec" );
	m_sustainModel.loadSettings( _this, "sustain" );
	m_releaseModel.loadSettings( _this, "rel" );
	m_amountModel.loadSettings( _this, "amt" );
	m_lfoWaveModel.loadSettings( _this, "lshp" );
	m_lfoPredelayModel.loadSettings( _this, "lpdel" );
	m_lfoAttackModel.loadSettings( _this, "latt" );
	m_lfoSpeedModel.loadSettings( _this, "lspd" );
	m_lfoAmountModel.loadSettings( _this, "lamt" );
	m_x100Model.loadSettings( _this, "x100" );
	m_controlEnvAmountModel.loadSettings( _this, "ctlenvamt" );

/*	 ### TODO:
	Old reversed sustain kept for backward compatibility
	with 4.15 file format*/
	if( _this.hasAttribute( "sus" ) )
	{
		m_sustainModel.loadSettings( _this, "sus" );
		m_sustainModel.setValue( 1.0 - m_sustainModel.value() );
	}

	// The wave file is read outside the lock; the swap of the wave itself is
	// under it, and the snapshot that carries it is published afterwards.
	std::shared_ptr<const SampleBuffer> userWave = SampleBuffer::emptyBuffer();
	if (const auto userWaveFile = _this.attribute("userwavefile"); !userWaveFile.isEmpty())
	{
		if (QFileInfo(PathUtil::toAbsolute(userWaveFile)).exists())
		{
			userWave = SampleBuffer::fromFile(_this.attribute("userwavefile"));
		}
		else { Engine::getSong()->collectError(QString("%1: %2").arg(tr("Sample not found"), userWaveFile)); }
	}

	{
		QMutexLocker m(&m_paramMutex);
		m_userWave = userWave;
		rebuildAudioState();
	}
	emit dataChanged();
}




void EnvelopeAndLfoParameters::updateSampleVars()
{
	{
		QMutexLocker m(&m_paramMutex);
		rebuildAudioState();
	}
	emit dataChanged();
}




// The writer's side: build a snapshot of the parameters and publish it. The
// audio path never runs this code and never takes m_paramMutex.
void EnvelopeAndLfoParameters::rebuildAudioState()
{
	const float frames_per_env_seg = SECS_PER_ENV_SEGMENT *
				Engine::audioEngine()->outputSampleRate();

	// TODO: Remove the expKnobVals, time should be linear
	const auto predelay_frames = static_cast<f_cnt_t>(frames_per_env_seg * expKnobVal(m_predelayModel.value()));

	const f_cnt_t attack_frames = std::max(minimumFrames,
					static_cast<f_cnt_t>(frames_per_env_seg *
					expKnobVal(m_attackModel.value())));

	const auto hold_frames = static_cast<f_cnt_t>(frames_per_env_seg * expKnobVal(m_holdModel.value()));

	const f_cnt_t decay_frames = std::max(minimumFrames,
					static_cast<f_cnt_t>(frames_per_env_seg *
					expKnobVal(m_decayModel.value() *
					(1 - m_sustainModel.value()))));

	const float sustainLevel = m_sustainModel.value();
	const float amount = m_amountModel.value();
	const float amountAdd = (amount >= 0) ?
				( 1.0f - amount ) * m_valueForZeroAmount :
				m_valueForZeroAmount;

	auto state = std::make_unique<AudioState>();
	state->pahdFrames = predelay_frames + attack_frames + hold_frames +
								decay_frames;
	state->rFrames = std::max(minimumFrames, static_cast<f_cnt_t>( frames_per_env_seg *
					expKnobVal( m_releaseModel.value() ) ));

	if( static_cast<int>( floorf( amount * 1000.0f ) ) == 0 )
	{
		state->rFrames = minimumFrames;
	}

	// The envelope buffers of a retired snapshot are reused, so that a
	// parameter change does not allocate a new buffer every time.
	state->pahdEnv = takeEnvBuffer( state->pahdFrames );
	state->rEnv = takeEnvBuffer( state->rFrames );

	const float aa = amountAdd;
	for( f_cnt_t i = 0; i < predelay_frames; ++i )
	{
		state->pahdEnv[i] = aa;
	}

	f_cnt_t add = predelay_frames;

	const float afI = ( 1.0f / attack_frames ) * amount;
	for( f_cnt_t i = 0; i < attack_frames; ++i )
	{
		state->pahdEnv[add+i] = i * afI + aa;
	}

	add += attack_frames;
	const float amsum = amount + amountAdd;
	for( f_cnt_t i = 0; i < hold_frames; ++i )
	{
		state->pahdEnv[add + i] = amsum;
	}

	add += hold_frames;
	const float dfI = ( 1.0 / decay_frames ) * ( sustainLevel -1 ) * amount;
	for( f_cnt_t i = 0; i < decay_frames; ++i )
	{
		state->pahdEnv[add + i] = amsum + i*dfI;
	}

	const float rfI = ( 1.0f / state->rFrames ) * amount;
	for( f_cnt_t i = 0; i < state->rFrames; ++i )
	{
		state->rEnv[i] = (float)( state->rFrames - i ) * rfI;
	}

	// save this calculation in real-time-part
	state->sustainLevel = sustainLevel * amount + amountAdd;


	const float frames_per_lfo_oscillation = SECS_PER_LFO_OSCILLATION *
				Engine::audioEngine()->outputSampleRate();
	state->lfoPredelayFrames = static_cast<f_cnt_t>( frames_per_lfo_oscillation *
				expKnobVal( m_lfoPredelayModel.value() ) );
	state->lfoAttackFrames = static_cast<f_cnt_t>( frames_per_lfo_oscillation *
				expKnobVal( m_lfoAttackModel.value() ) );
	state->lfoOscillationFrames = static_cast<f_cnt_t>(
						frames_per_lfo_oscillation *
						m_lfoSpeedModel.value() );
	if( m_x100Model.value() )
	{
		state->lfoOscillationFrames /= 100;
	}
	state->lfoAmount = m_lfoAmountModel.value() * 0.5f;
	state->lfoAmountIsZero =
		static_cast<int>( floorf( state->lfoAmount * 1000.0f ) ) == 0;

	m_used.store( true, std::memory_order_relaxed );
	if( state->lfoAmountIsZero &&
		static_cast<int>( floorf( amount * 1000.0f ) ) == 0 )
	{
		m_used.store( false, std::memory_order_relaxed );
	}

	// the user-defined wave travels with the snapshot: the audio path samples
	// the published wave, and a wave that is replaced is not freed while a
	// reader may still be sampling it.
	state->userWave = m_userWave;

	publishAudioState( std::move(state) );
}




// Hand the new snapshot to the audio thread and retire the previous one. The
// sequence is odd while the switch happens, so a reader that catches it
// retries instead of reading a half-published snapshot.
void EnvelopeAndLfoParameters::setPublicationHook( PublicationHook _hook )
{
	g_publicationHook = std::move( _hook );
}




void EnvelopeAndLfoParameters::setPublicationStartHook( PublicationHook _hook )
{
	g_publicationStartHook = std::move( _hook );
}




void EnvelopeAndLfoParameters::publishAudioState( std::unique_ptr<AudioState> _state )
{
	_state->version = m_publicationCounter.fetch_add( 1, std::memory_order_relaxed ) + 1;
	// the start of the publication: the test seam holds the publisher here.
	if( g_publicationStartHook ) { g_publicationStartHook(); }
	// the publication point: the test seam holds the publisher here.
	if( g_publicationHook ) { g_publicationHook(); }
	AudioState * const old = m_audioState.exchange( _state.release(),
								std::memory_order_seq_cst );

	if( old != nullptr )
	{
		m_retiredAudioStates.push_back( old );
	}
	reclaimRetiredAudioStates();
}




// A retired snapshot is freed only when no reader may still hold it.
void EnvelopeAndLfoParameters::reclaimRetiredAudioStates()
{
	// A retired snapshot is freed only when no reader holds one. The check
	// follows the publication in the total order of the atomics, so a reader
	// that claims a snapshot either holds the new one or is counted here.
	if( m_audioStateReaders.load(std::memory_order_seq_cst) != 0 )
	{
		return;
	}
	for( AudioState * state : m_retiredAudioStates )
	{
		// the buffers of a retired snapshot are kept for the next publication
		if( m_envBufferPool.size() < 8 )
		{
			m_envBufferPool.push_back( std::move(state->pahdEnv) );
			m_envBufferPool.push_back( std::move(state->rEnv) );
		}
		delete state;
	}
	m_retiredAudioStates.clear();
}




std::vector<sample_t> EnvelopeAndLfoParameters::takeEnvBuffer( std::size_t _samples )
{
	for( auto it = m_envBufferPool.begin(); it != m_envBufferPool.end(); ++it )
	{
		if( it->capacity() >= _samples )
		{
			std::vector<sample_t> buffer = std::move( *it );
			m_envBufferPool.erase( it );
			buffer.resize( _samples );
			return buffer;
		}
	}
	return std::vector<sample_t>( _samples );
}




// The audio path's snapshot. No lock, no wait, no allocation: the snapshot is
// immutable and the writer hands it over atomically. The reader count keeps
// the writer from freeing a snapshot that is still in use.
const EnvelopeAndLfoParameters::AudioState* EnvelopeAndLfoParameters::acquireAudioState() const noexcept
{
	// The claim of the published snapshot is a fixed number of atomic
	// operations: it never waits for the publisher, it never retries and it
	// never allocates. The reader count is raised before the snapshot is read,
	// so the publisher cannot free a snapshot that this reader is about to
	// use: it either sees the count, or this reader reads the new snapshot.
	m_audioStateReaders.fetch_add( 1, std::memory_order_seq_cst );
	return m_audioState.load( std::memory_order_seq_cst );
}




void EnvelopeAndLfoParameters::releaseAudioState() const noexcept
{
	m_audioStateReaders.fetch_sub( 1, std::memory_order_seq_cst );
}




bool EnvelopeAndLfoParameters::holdAudioState()
{
	const AudioState * const state = acquireAudioState();
	if( state == nullptr ) { return false; }
	m_heldAudioState = state;
	return true;
}




void EnvelopeAndLfoParameters::releaseHeldAudioState()
{
	if( m_heldAudioState != nullptr )
	{
		releaseAudioState();
		m_heldAudioState = nullptr;
	}
}




f_cnt_t EnvelopeAndLfoParameters::PAHD_Frames() const
{
	const AudioState * const state = acquireAudioState();
	if( state == nullptr ) { return 0; }
	const f_cnt_t frames = state->pahdFrames;
	releaseAudioState();
	return frames;
}




f_cnt_t EnvelopeAndLfoParameters::releaseFrames() const
{
	const AudioState * const state = acquireAudioState();
	if( state == nullptr ) { return 0; }
	const f_cnt_t frames = state->rFrames;
	releaseAudioState();
	return frames;
}




f_cnt_t EnvelopeAndLfoParameters::getLfoPredelayFrames() const
{
	const AudioState * const state = acquireAudioState();
	if( state == nullptr ) { return 0; }
	const f_cnt_t frames = state->lfoPredelayFrames;
	releaseAudioState();
	return frames;
}




f_cnt_t EnvelopeAndLfoParameters::getLfoAttackFrames() const
{
	const AudioState * const state = acquireAudioState();
	if( state == nullptr ) { return 0; }
	const f_cnt_t frames = state->lfoAttackFrames;
	releaseAudioState();
	return frames;
}




f_cnt_t EnvelopeAndLfoParameters::getLfoOscillationFrames() const
{
	const AudioState * const state = acquireAudioState();
	if( state == nullptr ) { return 1; }
	const f_cnt_t frames = state->lfoOscillationFrames;
	releaseAudioState();
	return frames;
}




std::shared_ptr<const SampleBuffer> EnvelopeAndLfoParameters::getLfoUserWave() const
{
	const AudioState * const state = acquireAudioState();
	if( state == nullptr ) { return m_userWave; }
	const std::shared_ptr<const SampleBuffer> wave = state->userWave;
	releaseAudioState();
	return wave;
}







} // namespace lmms
