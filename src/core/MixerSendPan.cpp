/*
 * MixerSendPan.cpp - a send with its own pan, mixed into the receiving channel
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

#include "MixerSendPan.h"

#include <algorithm>

#include "Mixer.h"
#include "SampleFrame.h"
#include "ValueBuffer.h"

namespace lmms
{

void balanceGains(float pan, float* left, float* right)
{
	const float p = std::clamp(pan, -1.f, 1.f);
	*left = p > 0.f ? 1.f - p : 1.f;
	*right = p < 0.f ? 1.f + p : 1.f;
}

void addPannedSend(SampleFrame* dst, const SampleFrame* src, MixerRoute& route, MixerChannel& sender, f_cnt_t frames)
{
	float left = 1.f;
	float right = 1.f;
	balanceGains(route.pan()->value(), &left, &right);
	const ValueBuffer* sendBuf = route.amount()->valueBuffer();
	const ValueBuffer* volBuf = route.preFader() ? nullptr : sender.m_volumeModel.valueBuffer();
	const float send = route.amount()->value();
	const float volume = route.preFader() ? 1.f : sender.m_volumeModel.value();
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		const float gain = (sendBuf != nullptr ? sendBuf->values()[f] : send) * (volBuf != nullptr ? volBuf->values()[f] : volume);
		dst[f][0] += src[f][0] * gain * left;
		dst[f][1] += src[f][1] * gain * right;
	}
}

} // namespace lmms
