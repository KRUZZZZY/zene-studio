/*
 * Vocoder.h - a native channel vocoder: a voice's spectrum imposed on the channel's sound
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

#ifndef LMMS_VOCODER_H
#define LMMS_VOCODER_H

#include <array>

#include "AudioPlugin.h"
#include "BiquadDesign.h"
#include "ComboBoxModel.h"
#include "ParameterControls.h"

namespace lmms
{

class VocoderEffect;

//! The vocoder's parameters: where the voice comes from, the band count and span, the bands'
//! width, how fast a band follows the voice, and the output.
class VocoderControls : public ParameterControls
{
public:
	explicit VocoderControls(VocoderEffect* effect);

	ComboBoxModel m_sourceModel;
	FloatModel m_bandsModel;
	FloatModel m_lowModel;
	FloatModel m_highModel;
	FloatModel m_widthModel;
	FloatModel m_releaseModel;
	FloatModel m_outputModel;
};

/*! 0.5.0 L10 (MX-154): a channel vocoder. The channel's own sound is the carrier (a synth); the
 *  voice - the modulator - is the channel's sidechain input (route one with mixer.sidechain_to), or
 *  the left side of the input with the carrier on the right. Both run through 'bands' band-passes
 *  spread in octaves from 'low' to 'high' Hz; each carrier band is turned up and down by the voice's
 *  level in the same band. With the sidechain chosen and none routed, the input passes unchanged.
 *  Every filter and envelope is a fixed array in the object (at most 32 bands), and the coefficients
 *  are recomputed - plain arithmetic - only when a knob moves: nothing is allocated while processing. */
class VocoderEffect : public DefaultEffect
{
public:
	static constexpr int MaxBands = 32;

	enum class Source
	{
		Sidechain = 0,
		LeftChannel = 1
	};

	VocoderEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~VocoderEffect() override = default;

	EffectControls* controls() override { return &m_controls; }

private:
	ProcessStatus processImpl(InterleavedBufferView<float, 2> inOut) override;
	void configure(int bands, float low, float high, float width, float rate);
	//! One frame: the carrier @a left / @a right shaped by the voice sample @a voice.
	SampleFrame vocode(float voice, float left, float right, float release);

	VocoderControls m_controls;
	std::array<BiquadSection<2>, MaxBands> m_carrier{};
	std::array<BiquadSection<1>, MaxBands> m_voice{};
	std::array<float, MaxBands> m_envelope{};
	int m_bands = 0;
	float m_low = 0.f;
	float m_high = 0.f;
	float m_width = 0.f;
	float m_rate = 0.f;
	float m_makeup = 1.f;
};

} // namespace lmms

#endif // LMMS_VOCODER_H
