/*
 * Modulation.h - a native chorus, phaser and tremolo
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
#ifndef LMMS_MODULATION_H
#define LMMS_MODULATION_H

#include <array>
#include <vector>

#include "AudioPlugin.h"
#include "ModulationControls.h"

namespace lmms
{

/*! 0.5.0 L10 (MX-153): the modulation effects the tree lacked natively (the flanger is its own
 *  device), in one device with three modes, each driven by a sine LFO whose right side runs
 *  'spread' degrees ahead of the left:
 *  - chorus: the signal with a voice delayed 15 ms +- 'depth' x 10 ms, fed back by 'feedback';
 *  - phaser: the signal with itself through six first-order all-passes swept between 200 Hz and
 *    200 Hz + 'depth' x 4 kHz (fed back by 'feedback');
 *  - tremolo: the level swung down by 'depth'.
 *  The device's own wet/dry knob mixes it. The delay line is sized at construction for the longest
 *  delay at 192 kHz, so processing never allocates. */
class ModulationEffect : public DefaultEffect
{
public:
	ModulationEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~ModulationEffect() override = default;

	EffectControls* controls() override { return &m_controls; }

private:
	ProcessStatus processImpl(InterleavedBufferView<float, 2> inOut) override;
	float chorus(int channel, float in, float lfo, float rate);
	float phaser(int channel, float in, float lfo, float rate);

	static constexpr int Stages = 6;

	ModulationControls m_controls;
	std::array<std::vector<float>, 2> m_delay;
	std::size_t m_write = 0;
	std::array<std::array<float, Stages>, 2> m_allpass{};
	std::array<float, 2> m_feedback{};
	double m_phase = 0.0;

	friend class ModulationControls;
};

} // namespace lmms

#endif // LMMS_MODULATION_H
