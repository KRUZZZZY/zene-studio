/*
 * Gate.h - a native gate, expander and ducker
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
#ifndef LMMS_GATE_H
#define LMMS_GATE_H

#include <atomic>

#include "AudioPlugin.h"
#include "GateControls.h"

namespace lmms
{

/*! 0.5.0 L10 (MX-145): one dynamics device in three modes, keyed by the channel's own signal or -
 *  when the mixer routes one here (mixer.sidechain_to) - by a sidechain:
 *  - gate: below the threshold the signal drops to the floor ('range'), held open for 'hold';
 *  - expander: below the threshold the level falls 'ratio' times faster, no lower than the floor (also
 *    held open for 'hold');
 *  - duck: above the threshold the signal drops by 'range' - another channel's sidechain ducking
 *    this one is the classic use.
 *  The envelope follows the key's peak; the gain moves at 'attack' when it opens and 'release' when
 *  it closes. Every coefficient is computed per block from the models: nothing allocates or locks. */
class GateEffect : public DefaultEffect
{
public:
	GateEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~GateEffect() override = default;

	EffectControls* controls() override { return &m_controls; }
	//! 0 while bypassed: the last block's reading would otherwise stay on the strip's meter.
	float gainReductionDb() const override { return isEnabled() ? m_reductionDb.load(std::memory_order_relaxed) : 0.f; }

	//! The gain (linear) the mode asks for at key level @a levelDb - exposed for the unit test.
	static float targetGain(int mode, float levelDb, float thresholdDb, float rangeDb, float ratio);

private:
	ProcessStatus processImpl(InterleavedBufferView<float, 2> inOut) override;
	//! @a target with the hold applied: open for the hold time after the key last opened it.
	float held(float target, int mode, f_cnt_t hold);

	GateControls m_controls;
	float m_envelope = 0.f;
	float m_gain = 1.f;
	f_cnt_t m_holdLeft = 0;
	std::atomic<float> m_reductionDb{0.f};  //!< written by the audio thread, read by the strip

	friend class GateControls;
};

} // namespace lmms

#endif // LMMS_GATE_H
