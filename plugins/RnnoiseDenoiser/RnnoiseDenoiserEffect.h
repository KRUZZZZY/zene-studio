/*
 * RnnoiseDenoiserEffect.h - RNNoise-based real-time noise suppression effect
 *
 * Copyright (c) 2026 AI-KOS Team
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

#ifndef LMMS_RNNOISE_DENOISER_EFFECT_H
#define LMMS_RNNOISE_DENOISER_EFFECT_H

#include "Effect.h"
#include "RnnoiseDenoiserControls.h"

// Forward declaration of RNNoise's opaque state type (global C typedef)
struct DenoiseState;

namespace lmms
{

class RnnoiseDenoiserEffect : public Effect
{
	Q_OBJECT
public:
	RnnoiseDenoiserEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~RnnoiseDenoiserEffect() override;

	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;

	EffectControls* controls() override
	{
		return &m_controls;
	}

private:
	RnnoiseDenoiserControls m_controls;

	// Opaque RNNoise denoise state pointer
	struct DenoiseState* m_rnnoiseState;

	// RNNoise processes exactly 480-sample frames (10 ms at 48 kHz)
	static constexpr int RNNOISE_FRAME_SIZE = 480;

	// Input accumulation buffer (mono)
	float m_inputBuf[RNNOISE_FRAME_SIZE] = {};
	int m_inputCount;

	// Output buffer for the last processed frame
	float m_outputBuf[RNNOISE_FRAME_SIZE] = {};
	int m_outputPos;
	bool m_hasOutput;

	friend class RnnoiseDenoiserControls;
	friend class gui::RnnoiseDenoiserControlDialog;
};

} // namespace lmms

#endif // LMMS_RNNOISE_DENOISER_EFFECT_H