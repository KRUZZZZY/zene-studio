/*
 * DeEsser.h - a native de-esser: the high band turned down while it is loud
 *
 * Copyright (c) 2026 Zene Studio contributors
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

#ifndef LMMS_DEESSER_H
#define LMMS_DEESSER_H

#include <atomic>

#include "AudioPlugin.h"
#include "BiquadDesign.h"
#include "ComboBoxModel.h"
#include "ParameterControls.h"

namespace lmms
{

class DeEsserEffect;

//! The de-esser's parameters: the mode, the frequency above which sibilance is heard, the threshold,
//! the most it turns down, and a switch to hear only the band it listens to.
class DeEsserControls : public ParameterControls
{
public:
	explicit DeEsserControls(DeEsserEffect* effect);

	ComboBoxModel m_modeModel;
	FloatModel m_frequencyModel;
	FloatModel m_thresholdModel;
	FloatModel m_rangeModel;
	BoolModel m_listenModel;
};

/*! 0.5.0 L10 (MX-147): a de-esser. A detector follows the level above 'frequency' (a 12 dB / octave
 *  high-pass of the mid signal); while it is over 'threshold' the device turns down by 4:1, at most
 *  'range' dB - only the band above the frequency (split, the default: the input minus its low-pass,
 *  so the two parts add back exactly) or the whole signal (wide).
 *  'listen' plays the detector's band alone, to find the frequency. Its reduction is what the
 *  strip's gain-reduction meter shows. The filters are a few floats in the object: nothing is
 *  allocated while processing, and new coefficients are plain arithmetic when a knob moves. */
class DeEsserEffect : public DefaultEffect
{
public:
	enum class Mode
	{
		Split = 0,
		Wide = 1
	};

	DeEsserEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~DeEsserEffect() override = default;

	EffectControls* controls() override { return &m_controls; }
	float gainReductionDb() const override { return m_reductionDb.load(std::memory_order_relaxed); }

	//! The reduction (dB, >= 0) for a sibilance level @a levelDb: 4:1 above @a thresholdDb, at most @a rangeDb.
	static float reductionDb(float levelDb, float thresholdDb, float rangeDb);

private:
	ProcessStatus processImpl(InterleavedBufferView<float, 2> inOut) override;
	//! Bypassed (or not processing for another reason): it turns nothing down, so it reports 0 dB.
	void processBypassedImpl() override { m_reductionDb.store(0.f, std::memory_order_relaxed); }
	void design(float frequency, float rate);
	//! The frame with this sample's gain applied in the chosen mode.
	SampleFrame shaped(const SampleFrame& in, Mode mode, bool listen);

	DeEsserControls m_controls;
	BiquadSection<1> m_detector;
	BiquadSection<2> m_low;  //!< the split: the band above the frequency is the input minus this
	float m_frequency = -1.f;
	float m_rate = 0.f;
	float m_envelope = 0.f;
	float m_gain = 1.f;
	std::atomic<float> m_reductionDb{0.f};
};

} // namespace lmms

#endif // LMMS_DEESSER_H
