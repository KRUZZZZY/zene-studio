/*
 * RnnoiseDenoiserEffect.cpp - RNNoise-based real-time noise suppression effect
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

#include "RnnoiseDenoiserEffect.h"

#include "embed.h"
#include "plugin_export.h"

// RNNoise C API -- header resides in the rnnoise/ subdirectory
extern "C"
{
#include "rnnoise.h"
}


namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT rnnoisedenoiser_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"RNNoise Denoiser",
	QT_TRANSLATE_NOOP("PluginBrowser",
		"A real-time noise suppression plugin using the RNNoise neural network"),
	"AI-KOS Team <https://github.com/ai-kos>",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
} ;

}


RnnoiseDenoiserEffect::RnnoiseDenoiserEffect(Model* parent,
	const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&rnnoisedenoiser_plugin_descriptor, parent, key),
	m_controls(this),
	m_rnnoiseState(nullptr),
	m_inputCount(0),
	m_outputPos(0),
	m_hasOutput(false)
{
	// Initialise RNNoise with the default model (trained weights embedded in rnnoise_data.c)
	m_rnnoiseState = rnnoise_create(nullptr);
}


RnnoiseDenoiserEffect::~RnnoiseDenoiserEffect()
{
	if (m_rnnoiseState)
	{
		rnnoise_destroy(m_rnnoiseState);
		m_rnnoiseState = nullptr;
	}
}


Effect::ProcessStatus RnnoiseDenoiserEffect::processImpl(
	SampleFrame* buf, const f_cnt_t frames)
{
	const float d = dryLevel();
	const float w = wetLevel();

	for (f_cnt_t i = 0; i < frames; ++i)
	{
		// Downmix to mono for RNNoise input
		const float monoIn = (buf[i][0] + buf[i][1]) * 0.5f;
		m_inputBuf[m_inputCount++] = monoIn;

		// When we have a full 480-sample frame, process through RNNoise
		if (m_inputCount >= RNNOISE_FRAME_SIZE)
		{
			rnnoise_process_frame(m_rnnoiseState, m_outputBuf, m_inputBuf);
			m_inputCount = 0;
			m_outputPos = 0;
			m_hasOutput = true;
		}

		// Read from the output buffer (with latency) or pass dry signal
		if (m_hasOutput)
		{
			const float denoised = m_outputBuf[m_outputPos++];
			buf[i][0] = buf[i][0] * d + denoised * w;
			buf[i][1] = buf[i][1] * d + denoised * w;

			if (m_outputPos >= RNNOISE_FRAME_SIZE)
			{
				m_outputPos = 0;
				m_hasOutput = false;
			}
		}
		// else: before the first frame is processed, input passes through unchanged
		// (dry-only until the first 480-sample frame completes)
	}

	return ProcessStatus::ContinueIfNotQuiet;
}


extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new RnnoiseDenoiserEffect(parent,
		static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms