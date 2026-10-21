/*
 * CompressorControls.h
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

#ifndef COMPRESSOR_CONTROLS_H
#define COMPRESSOR_CONTROLS_H

#include <atomic>

#include "CompressorControlDialog.h"

#include "EffectControls.h"

namespace lmms
{

class CompressorEffect;


class CompressorControls : public EffectControls
{
	Q_OBJECT
public:
	CompressorControls(CompressorEffect* effect);

	void saveSettings(QDomDocument & _doc, QDomElement & _parent) override;
	void loadSettings(const QDomElement & _this) override;
	inline QString nodeName() const override
	{
		return "CompressorControls";
	}

	int controlCount() override
	{
		return 28;
	}

	gui::EffectControlDialog* createView() override
	{
		return new gui::CompressorControlDialog(this);
	}

private:
	CompressorEffect * m_effect;

	FloatModel m_thresholdModel;
	FloatModel m_ratioModel;
	FloatModel m_attackModel;
	FloatModel m_releaseModel;
	FloatModel m_kneeModel;
	FloatModel m_holdModel;
	FloatModel m_rangeModel;
	FloatModel m_rmsModel;
	IntModel m_midsideModel;
	IntModel m_peakmodeModel;
	FloatModel m_lookaheadLengthModel;
	FloatModel m_inBalanceModel;
	FloatModel m_outBalanceModel;
	IntModel m_limiterModel;
	FloatModel m_outGainModel;
	FloatModel m_inGainModel;
	FloatModel m_blendModel;
	FloatModel m_stereoBalanceModel;
	BoolModel m_autoMakeupModel;
	BoolModel m_auditionModel;
	BoolModel m_feedbackModel;
	FloatModel m_autoAttackModel;
	FloatModel m_autoReleaseModel;
	BoolModel m_lookaheadModel;
	FloatModel m_tiltModel;
	FloatModel m_tiltFreqModel;
	//! 0.5.0 L09 (MX-057): the key (detector) filter - a high-pass (20 Hz = off) and a low-pass (20 kHz = off).
	FloatModel m_keyHpModel;
	FloatModel m_keyLpModel;
	IntModel m_stereoLinkModel;
	FloatModel m_mixModel;

	// cluster new-p9-122: peak meters written on the audio thread
	// (CompressorEffect::processImpl) and read+cleared by the shared EqFader
	// on the GUI timer - the sibling of the Eq/Delay handoff defect.
	std::atomic<float> m_inPeakL;
	std::atomic<float> m_inPeakR;
	std::atomic<float> m_outPeakL;
	std::atomic<float> m_outPeakR;

	friend class gui::CompressorControlDialog;
	friend class CompressorEffect;

} ;


} // namespace lmms

// The peak meters cross the audio/GUI thread boundary (cluster new-p9-122);
// a non-lock-free atomic would put a lock or syscall on the render thread.
static_assert(std::atomic<float>::is_always_lock_free,
	"CompressorControls peak meters are written on the audio thread and must be lock-free");

#endif
