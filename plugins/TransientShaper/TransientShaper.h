/*
 * TransientShaper.h - a native transient shaper: the attacks and the tails turned up or down
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

#ifndef LMMS_TRANSIENT_SHAPER_H
#define LMMS_TRANSIENT_SHAPER_H

#include "AudioPlugin.h"
#include "ParameterControls.h"

namespace lmms
{

class TransientShaperEffect;

//! The shaper's parameters: how much the attacks and the sustain are turned up (or down), and the output.
class TransientShaperControls : public ParameterControls
{
public:
	explicit TransientShaperControls(TransientShaperEffect* effect);

	FloatModel m_attackModel;
	FloatModel m_sustainModel;
	FloatModel m_outputModel;
};

/*! 0.5.0 L10 (MX-148): a transient shaper. Two envelope followers run on the louder side - a fast
 *  one (0.5 ms up, 20 ms down) and a slow one (20 ms up, 250 ms down). While a sound starts the fast
 *  one is above the slow one, and while it rings out it is below: half their difference in dB (it
 *  is capped at 24) times 'attack' (or 'sustain'), as a fraction of 100 %, is the gain in dB - at
 *  most +-12 dB. No look-ahead (no latency); the state is two floats, nothing is allocated while
 *  processing. */
class TransientShaperEffect : public DefaultEffect
{
public:
	TransientShaperEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~TransientShaperEffect() override = default;

	EffectControls* controls() override { return &m_controls; }

	//! The gain in dB for a fast-minus-slow envelope difference @a differenceDb.
	static float gainDb(float differenceDb, float attackPercent, float sustainPercent);

private:
	ProcessStatus processImpl(InterleavedBufferView<float, 2> inOut) override;

	TransientShaperControls m_controls;
	float m_fast = 0.f;
	float m_slow = 0.f;
};

} // namespace lmms

#endif // LMMS_TRANSIENT_SHAPER_H
