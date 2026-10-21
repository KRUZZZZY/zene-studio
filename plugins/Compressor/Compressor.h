/*
 * Compressor.h
 *
 * Copyright (c) 2020 Lost Robot <r94231@gmail.com>
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


#ifndef COMPRESSOR_H
#define COMPRESSOR_H

#include <atomic>

#include "AudioPlugin.h"
#include "CompressorControls.h"
#include "CompressorKeyFilter.h"

namespace lmms
{


constexpr float COMP_LOG = -2.2f;

class CompressorEffect : public DefaultEffect
{
	Q_OBJECT
public:
	CompressorEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~CompressorEffect() override = default;

	EffectControls* controls() override
	{
		return &m_compressorControls;
	}

	//! 0.5.0 L08 (MX-130): the reduction the last block applied, in dB above zero, for the mixer strip.
	float gainReductionDb() const override;

private slots:
	void calcAutoMakeup();
	void calcAttack();
	void calcRelease();
	void calcAutoAttack();
	void calcAutoRelease();
	void calcHold();
	void calcOutGain();
	void calcRatio();
	void calcRange();
	void resizeRMS();
	void calcLookaheadLength();
	void calcThreshold();
	void calcKnee();
	void calcInGain();
	void calcTiltCoeffs();
	void calcKeyFilter();
	void calcMix();
	void changeSampleRate();
	void redrawKnee();

private:
	ProcessStatus processImpl(InterleavedBufferView<float, 2> inOut) override;
	void processBypassedImpl() override;

	float msToCoeff(float ms);

	inline void calcTiltFilter(sample_t inputSample, sample_t &outputSample, int filtNum);

	//! 0.5.0 L09 (MX-057): the detector's key filter. calcKeyFilter() designs it wherever a knob moves
	//! and posts it to the mailbox; the audio thread adopts it before a block (catch-up W3).
	KeyFilterMailbox m_keyMailbox;
	KeyFilter m_keyFilter;

	enum class StereoLinkMode { Unlinked, Maximum, Average, Minimum, Blend };

	CompressorControls m_compressorControls;

	std::array<std::vector<float>, 2> m_inLookBuf;
	std::array<std::vector<float>, 2> m_scLookBuf;
	int m_lookWrite;
	int m_lookBufLength;

	float m_attCoeff;
	float m_relCoeff;
	float m_autoAttVal;
	float m_autoRelVal;

	int m_holdLength = 0;
	int m_holdTimer[2] = {0, 0};

	int m_lookaheadLength;
	float m_thresholdAmpVal;
	float m_autoMakeupVal;
	float m_outGainVal;
	float m_inGainVal;
	float m_rangeVal;
	float m_tiltVal;
	float m_mixVal;

	float m_coeffPrecalc;

	SampleFrame m_maxLookaheadVal;

	int m_maxLookaheadTimer[2] = {1, 1};
	
	float m_rmsTimeConst;
	float m_rmsVal[2] = {0, 0};

	float m_crestPeakVal[2] = {0, 0};
	float m_crestRmsVal[2] = {0, 0};
	float m_crestFactorVal[2] = {0, 0};
	float m_crestTimeConst;

	float m_tiltOut[2] = {0};

	bool m_cleanedBuffers = false;

	float m_sampleRate;

	float m_lgain;
	float m_hgain;
	float m_a0;
	float m_b1;

	float m_prevOut[2] = {0};

	//! The audio thread's own working state. Only the audio thread reads or writes it.
	float m_yL[2];
	float m_gainResult[2];
	//! gainReductionDb(): the lowest gain of the last block, not of its last frame (as Gate and DeEsser).
	std::atomic<float> m_deepestGain{1.f};
	//! The visualizer's holds. The audio thread raises them to the block's peak and gain; the
	//! control dialog samples them and resets them to the published envelope and gain. Both
	//! threads write them, so they are atomic: the dialog's reset is a store, not a race.
	std::atomic<float> m_displayPeak[2];
	std::atomic<float> m_displayGain[2];
	//! What the control dialog resets the holds to, published by the audio thread after every
	//! block: the detector's envelope and the gain the block applied. The dialog reads these
	//! and never the audio thread's working state (m_yL, m_gainResult), which the next block
	//! overwrites while the dialog reads it.
	std::atomic<float> m_publishedEnvelope[2];
	std::atomic<float> m_publishedGain[2];

	float m_kneeVal;
	float m_thresholdVal;
	float m_ratioVal;

	bool m_redrawKnee = true;
	bool m_redrawThreshold = true;

	friend class CompressorControls;
	friend class gui::CompressorControlDialog;
} ;


} // namespace lmms

#endif
