/*
 * MixerSendPan.h - a send with its own pan, mixed into the receiving channel
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

#ifndef LMMS_MIXER_SEND_PAN_H
#define LMMS_MIXER_SEND_PAN_H

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class MixerChannel;
class MixerRoute;
class SampleFrame;

/*! 0.5.0 L09 (MX-048): add @a src (the sender's block, already delayed for PDC) into @a dst as the
 *  route sends it - the send amount, the sender's fader unless the send is pre-fader, sample-exact
 *  where those models are - balanced by the route's own pan (the balance law the channel pan uses).
 *  Only called for a route whose pan is off centre: a centred send keeps the inherited arithmetic.
 *  Audio thread (a mixer worker): no allocation, no lock. */
LMMS_EXPORT void addPannedSend(SampleFrame* dst, const SampleFrame* src, MixerRoute& route, MixerChannel& sender, f_cnt_t frames);

//! The two balance gains (left, right) for a pan in -1..1.
LMMS_EXPORT void balanceGains(float pan, float* left, float* right);

} // namespace lmms

#endif // LMMS_MIXER_SEND_PAN_H
