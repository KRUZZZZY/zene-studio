/*
 * DynamicEq.h - a native four-band dynamic EQ that can work on the mid or the side alone
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

#ifndef LMMS_DYNAMIC_EQ_H
#define LMMS_DYNAMIC_EQ_H

#include <array>
#include <memory>

#include "AudioPlugin.h"
#include "BiquadDesign.h"
#include "ComboBoxModel.h"
#include "ParameterControls.h"

namespace lmms
{

class DynamicEqEffect;

/*! The EQ's parameters: where it works (both sides, the mid, the side), then for each of the four
 *  bands - a low shelf, two bells, a high shelf - its frequency, gain, width (Q), threshold and
 *  dynamic range; and the output. Listed type by type, so the dialog's columns are the bands. */
class DynamicEqControls : public ParameterControls
{
public:
	static constexpr int Bands = 4;

	explicit DynamicEqControls(DynamicEqEffect* effect);

	ComboBoxModel m_channelsModel;
	std::array<std::unique_ptr<FloatModel>, Bands> m_frequency;
	std::array<std::unique_ptr<FloatModel>, Bands> m_gain;
	std::array<std::unique_ptr<FloatModel>, Bands> m_q;
	std::array<std::unique_ptr<FloatModel>, Bands> m_threshold;
	std::array<std::unique_ptr<FloatModel>, Bands> m_dynamic;
	FloatModel m_outputModel;
};

/*! 0.5.0 L10 (MX-143): a dynamic and mid / side EQ. Each band is a cookbook shelf or bell whose
 *  gain is its own 'gain' plus up to 'dynamic' dB more (negative: a cut) as the level in the band -
 *  a band-pass detector at the band's frequency and width - rises from 'threshold' to 12 dB above
 *  it; with 'dynamic' at 0 a band is a plain static EQ band. 'Channels' runs the bands on both
 *  sides, on the mid (L+R) only or on the side (L-R) only. A band's coefficients are recomputed
 *  every 32 frames while its gain moves - plain arithmetic in fixed members, nothing allocated while
 *  processing. Linear phase is not offered (it needs a long FIR and its latency). */
class DynamicEqEffect : public DefaultEffect
{
public:
	enum class Channels
	{
		Stereo = 0,
		Mid = 1,
		Side = 2
	};

	DynamicEqEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~DynamicEqEffect() override = default;

	EffectControls* controls() override { return &m_controls; }

	//! The gain in dB band @a gainDb moves to for a band level @a levelDb.
	static float dynamicGainDb(float gainDb, float dynamicDb, float levelDb, float thresholdDb);

private:
	struct Band
	{
		BiquadSection<2> filter;
		BiquadSection<1> detector;
		float envelope = 0.f;
		float appliedDb = 1e9f;
		float hz = -1.f;
		float q = -1.f;
	};

	ProcessStatus processImpl(InterleavedBufferView<float, 2> inOut) override;
	//! Recompute band @a b's sections when its gain, frequency or width moved.
	void update(int b, float rate);
	//! One frame through every band, on the channels 'Channels' chooses.
	SampleFrame equalise(const SampleFrame& in, Channels channels, float attack, float release);

	DynamicEqControls m_controls;
	std::array<Band, DynamicEqControls::Bands> m_bands{};
	int m_countdown = 0;
};

} // namespace lmms

#endif // LMMS_DYNAMIC_EQ_H
