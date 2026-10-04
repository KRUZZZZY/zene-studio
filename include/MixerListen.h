/*
 * MixerListen.h - the solo-to-listen bus: AFL / PFL of mixer channels on the master output
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

#ifndef LMMS_MIXER_LISTEN_H
#define LMMS_MIXER_LISTEN_H

#include <QVector>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class Mixer;
class MixerChannel;
class SampleFrame;

/*! 0.5.0 L09 (MX-037): listening to channels without touching the mix. While any channel listens,
 *  the master output carries the sum of the listening channels - after their faders (AFL) or before
 *  (PFL) - instead of the mix; the mix itself, its mute and solo states and every send are left
 *  alone, so stopping the listen brings the mix back exactly. The flags are relaxed atomics: the UI
 *  sets them, the audio thread reads them in Mixer::masterMix (no allocation, no lock). Interface
 *  state - not saved with the project; deleting a channel clears every flag (indices move). */
namespace mixerlisten
{

constexpr int MaxChannels = 512;

enum class Mode
{
	AfterFader,
	PreFader
};

LMMS_EXPORT void setListening(int channel, bool on);
LMMS_EXPORT bool listening(int channel);
LMMS_EXPORT bool anyListening();
LMMS_EXPORT QVector<int> listeningChannels();
LMMS_EXPORT void clear();
LMMS_EXPORT void setMode(Mode mode);
LMMS_EXPORT Mode mode();

//! Audio thread: add @a channel's signal after (or before) its fader to @a out, times @a gain, balanced by @a pan.
LMMS_EXPORT void addChannelSignal(SampleFrame* out, MixerChannel& channel, bool afterFader, f_cnt_t frames,
	float gain = 1.f, float pan = 0.f);

//! Audio thread, from Mixer::masterMix: when a channel listens, @a out becomes the listened sum.
LMMS_EXPORT void replaceOutput(SampleFrame* out, Mixer& mixer, f_cnt_t frames);

} // namespace mixerlisten

} // namespace lmms

#endif // LMMS_MIXER_LISTEN_H
