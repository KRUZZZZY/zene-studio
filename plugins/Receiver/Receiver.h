/*
 * Receiver.h - a device that mixes another channel's audio in at its slot (0.5.0 MX-063)
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
#ifndef LMMS_RECEIVER_H
#define LMMS_RECEIVER_H

#include "AudioPlugin.h"
#include "ReceiverControls.h"

namespace lmms
{

/*! 0.5.0 W3 (MX-063): receive audio inside a chain. The mixer delivers another channel's audio to this
 *  channel's sidechain input (mixer.sidechain_to - its own cycle refusal, PDC alignment to the channel's
 *  input and dependency ordering); this device mixes that input into the signal at its slot in the chain,
 *  at 'level', keeping the chain's own signal or ('replace') standing in for it. chain.receive places
 *  both in one step. The input is every sidechain send into the channel, summed (the channel's one
 *  sidechain bus). Per block: two model reads, then a multiply-add per sample - no allocation, no lock. */
class ReceiverEffect : public DefaultEffect
{
public:
	ReceiverEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~ReceiverEffect() override = default;

	EffectControls* controls() override { return &m_controls; }

	//! One sample: the chain's own @a own (dropped when @a replace) plus @a received at @a level.
	static float mix(float own, float received, float level, bool replace) { return (replace ? 0.f : own) + received * level; }

private:
	ProcessStatus processImpl(InterleavedBufferView<float, 2> inOut) override;

	ReceiverControls m_controls;

	friend class ReceiverControls;
};

} // namespace lmms

#endif // LMMS_RECEIVER_H
