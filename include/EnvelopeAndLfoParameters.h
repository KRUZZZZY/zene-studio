/*
 * EnvelopeAndLfoParameters.h - class EnvelopeAndLfoParameters
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

#ifndef LMMS_ENVELOPE_AND_LFO_PARAMETERS_H
#define LMMS_ENVELOPE_AND_LFO_PARAMETERS_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "JournallingObject.h"
#include "AutomatableModel.h"
#include "SampleBuffer.h"
#include "TempoSyncKnobModel.h"
#include "LmmsTypes.h"

namespace lmms
{

namespace gui
{

class EnvelopeAndLfoView;

}

class LMMS_EXPORT EnvelopeAndLfoParameters : public Model, public JournallingObject
{
	Q_OBJECT
private:
	/*! The parameter snapshot the audio path reads.
	 *
	 *  A snapshot is immutable once it is published. The writer (the parameter
	 *  side: the GUI, the command handlers, project loading) builds one under
	 *  m_paramMutex and hands it to the audio thread through an atomic pointer;
	 *  the audio thread holds it for the length of one fillLevel() call and
	 *  never takes a lock, never waits and never allocates. A published
	 *  snapshot is retired, not freed, while a reader may still hold it. */
	struct AudioState
	{
		f_cnt_t pahdFrames = 0;
		f_cnt_t rFrames = 0;
		float sustainLevel = 0.f;
		std::vector<sample_t> pahdEnv;
		std::vector<sample_t> rEnv;

		f_cnt_t lfoPredelayFrames = 0;
		f_cnt_t lfoAttackFrames = 0;
		f_cnt_t lfoOscillationFrames = 1;
		float lfoAmount = 0.f;
		bool lfoAmountIsZero = true;

		//! The user-defined LFO wave. It is part of the snapshot, so the audio
		//! path samples the wave that was published with the parameters and the
		//! wave it samples cannot be freed underneath it.
		std::shared_ptr<const SampleBuffer> userWave = SampleBuffer::emptyBuffer();
	};

public:
	class LfoInstances
	{
	public:
		LfoInstances() = default;

		~LfoInstances() = default;

		inline bool isEmpty() const
		{
			return m_lfos.isEmpty();
		}

		void trigger();
		void reset();

		void add( EnvelopeAndLfoParameters * lfo );
		void remove( EnvelopeAndLfoParameters * lfo );

	private:
		QMutex m_lfoListMutex;
		using LfoList = QList<EnvelopeAndLfoParameters*>;
		LfoList m_lfos;

	};

	enum class LfoShape
	{
		SineWave,
		TriangleWave,
		SawWave,
		SquareWave,
		UserDefinedWave,
		RandomWave,
		Count
	};

	EnvelopeAndLfoParameters( float _value_for_zero_amount,
							Model * _parent );
	~EnvelopeAndLfoParameters() override;

	static inline float expKnobVal( float _val )
	{
		return ( ( _val < 0 ) ? -_val : _val ) * _val;
	}

	static LfoInstances * instances()
	{
		return s_lfoInstances;
	}

	void fillLevel( float * _buf, f_cnt_t _frame,
				const f_cnt_t _release_begin,
				const f_cnt_t _frames );

	inline bool isUsed() const
	{
		return m_used.load(std::memory_order_relaxed);
	}


	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;
	QString nodeName() const override
	{
		return "el";
	}

	f_cnt_t PAHD_Frames() const;

	f_cnt_t releaseFrames() const;

	// Envelope
	const FloatModel& getPredelayModel() const { return m_predelayModel; }
	const FloatModel& getAttackModel() const { return m_attackModel; }
	const FloatModel& getHoldModel() const { return m_holdModel; }
	const FloatModel& getDecayModel() const { return m_decayModel; }
	const FloatModel& getSustainModel() const { return m_sustainModel; }
	const FloatModel& getReleaseModel() const { return m_releaseModel; }
	const FloatModel& getAmountModel() const { return m_amountModel; }
	FloatModel& getAmountModel() { return m_amountModel; }


	// LFO
	f_cnt_t getLfoPredelayFrames() const;
	f_cnt_t getLfoAttackFrames() const;
	f_cnt_t getLfoOscillationFrames() const;

	const FloatModel& getLfoAmountModel() const { return m_lfoAmountModel; }
	FloatModel& getLfoAmountModel() { return m_lfoAmountModel; }
	const TempoSyncKnobModel& getLfoSpeedModel() const { return m_lfoSpeedModel; }
	const BoolModel& getX100Model() const { return m_x100Model; }
	const IntModel& getLfoWaveModel() const { return m_lfoWaveModel; }
	std::shared_ptr<const SampleBuffer> getLfoUserWave() const;

	/*! TEST SEAM. holdAudioState() takes the published parameter snapshot exactly
	 *  as the audio thread takes it while it fills a level, and
	 *  releaseHeldAudioState() gives it back. A test uses it to stand in for a
	 *  running audio path: while a snapshot is held, the parameters it carries
	 *  must not be freed underneath the holder. A production path never calls
	 *  them. */
	bool holdAudioState();
	void releaseHeldAudioState();

public slots:
	void updateSampleVars();


protected:
	void fillLfoLevel( float * _buf, f_cnt_t _frame, const f_cnt_t _frames,
						const AudioState & _state );


private:
	static LfoInstances * s_lfoInstances;
	std::atomic<bool> m_used;

	//! The writer's lock. It serialises the parameter side; the audio path
	//! never takes it.
	QMutex m_paramMutex;

	FloatModel m_predelayModel;
	FloatModel m_attackModel;
	FloatModel m_holdModel;
	FloatModel m_decayModel;
	FloatModel m_sustainModel;
	FloatModel m_releaseModel;
	FloatModel m_amountModel;

	float  m_valueForZeroAmount;


	FloatModel m_lfoPredelayModel;
	FloatModel m_lfoAttackModel;
	TempoSyncKnobModel m_lfoSpeedModel;
	FloatModel m_lfoAmountModel;
	IntModel m_lfoWaveModel;

	BoolModel m_x100Model;
	BoolModel m_controlEnvAmountModel;


	std::atomic<f_cnt_t> m_lfoFrame;
	sample_t * m_lfoShapeData;
	sample_t m_random {};
	bool m_randomInitialized { false };
	std::atomic<bool> m_bad_lfoShapeData;
	std::shared_ptr<const SampleBuffer> m_userWave = SampleBuffer::emptyBuffer();

	//! The published snapshot: the audio path's view of the parameters.
	std::atomic<AudioState*> m_audioState;
	//! The publication counter. It is odd while a snapshot is being published,
	//! so a reader that catches the switch retries instead of reading a
	//! half-published snapshot.
	std::atomic<std::uint64_t> m_audioStateSeq;
	//! How many readers hold a published snapshot (the audio thread, and the
	//! test seam above). A snapshot is freed only when this is zero.
	mutable std::atomic<int> m_audioStateReaders;
	//! The published snapshots that may still be held by a reader.
	std::vector<AudioState*> m_retiredAudioStates;
	//! The envelope buffers of retired snapshots, kept for the next
	//! publication so that a parameter change does not allocate every time.
	std::vector<std::vector<sample_t>> m_envBufferPool;
	//! The snapshot the test seam holds.
	const AudioState* m_heldAudioState;

	constexpr static auto NumLfoShapes = static_cast<std::size_t>(LfoShape::Count);

	sample_t lfoShapeSample( f_cnt_t _frame_offset, const AudioState & _state );
	void updateLfoShapeData( const AudioState & _state );

	//! The audio path's snapshot: no lock, no wait, no allocation.
	const AudioState* acquireAudioState() const noexcept;
	void releaseAudioState() const noexcept;

	//! The writer's side: build a snapshot from the models and publish it.
	void rebuildAudioState();
	void publishAudioState( std::unique_ptr<AudioState> _state );
	void reclaimRetiredAudioStates();
	std::vector<sample_t> takeEnvBuffer( std::size_t _samples );


	friend class gui::EnvelopeAndLfoView;

} ;

} // namespace lmms

#endif // LMMS_ENVELOPE_AND_LFO_PARAMETERS_H
