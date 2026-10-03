/*
 * Console.h - a native console channel: soft saturation and a console's tone, for every channel of a mix
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

#ifndef LMMS_CONSOLE_H
#define LMMS_CONSOLE_H

#include <array>

#include "AudioPlugin.h"
#include "BiquadDesign.h"
#include "ComboBoxModel.h"
#include "OversamplingHelpers.h"
#include "ParameterControls.h"

namespace lmms
{

class ConsoleEffect;

//! The console's parameters: its character, how hard it is driven, and the output.
class ConsoleControls : public ParameterControls
{
public:
	explicit ConsoleControls(ConsoleEffect* effect);

	ComboBoxModel m_characterModel;
	FloatModel m_driveModel;
	FloatModel m_outputModel;
	ComboBoxModel m_oversamplingModel;
};

/*! 0.5.0 L10 (MX-150): one channel of an analogue console, meant to sit on every channel of a mix
 *  (mixer.console_apply puts it there and sets them all at once). A soft, slightly asymmetric
 *  saturator - unity gain for quiet signals, rounding the peaks more as 'drive' rises (0..100 %:
 *  -12..+8 dB into the curve) and adding a little even-order colour - then a DC blocker and the
 *  character's tone: clean (none), warm (+1 dB at 120 Hz, -1.5 dB above 9 kHz) or bright
 *  (+1.5 dB above 10 kHz). 'Oversampling' (MX-100: off, 2x, 4x) runs the saturator at a multiple
 *  of the rate, through the tree's polyphase half-band filters (OversamplingHelpers.h), so the
 *  harmonics it adds do not fold back as aliases. The filters are fixed state in the object;
 *  nothing is allocated while processing. */
class ConsoleEffect : public DefaultEffect
{
public:
	enum class Character
	{
		Clean = 0,
		Warm = 1,
		Bright = 2
	};

	ConsoleEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~ConsoleEffect() override = default;

	EffectControls* controls() override { return &m_controls; }

	//! The saturator: unity slope at 0, @a drive the gain into the curve (linear).
	static float saturate(float x, float drive);

private:
	ProcessStatus processImpl(InterleavedBufferView<float, 2> inOut) override;
	void design(Character character, float rate);
	//! One sample of channel @a c through the saturator, at the oversampled rate when it is on.
	float saturated(int c, float x, float drive);
	static constexpr int MaxStages = 2;

	ConsoleControls m_controls;
	BiquadSection<2> m_dcBlock;
	BiquadSection<2> m_low;
	BiquadSection<2> m_high;
	int m_character = -1;
	float m_rate = 0.f;
	std::array<Upsampler<MaxStages>, 2> m_upsampler;
	std::array<Downsampler<MaxStages>, 2> m_downsampler;
	alignas(16) std::array<std::array<float, 1 << MaxStages>, 2> m_over{};
	int m_stages = 0;
};

} // namespace lmms

#endif // LMMS_CONSOLE_H
