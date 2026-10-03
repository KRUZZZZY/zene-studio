/*
 * MixerStrip.h - a mixer channel's own stages: trim, polarity, mono, pan modes, and what its meters read
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

#ifndef LMMS_MIXER_STRIP_H
#define LMMS_MIXER_STRIP_H

#include <atomic>

#include "LmmsTypes.h"
#include "lmms_export.h"

class QDomElement;

namespace lmms
{

class MixerChannel;
class SampleFrame;

/*! One meter reading the mixer worker writes and the interface reads - and may reset - each period:
 *  a relaxed atomic, so the two threads share it without a lock and without a data race (review
 *  L05-L09-L08-1 #9). raise() is the worker's running maximum; a reset racing it loses at most one
 *  period's peak, which the next period puts back. */
struct MeterValue
{
	std::atomic<float> value{0.f};
	float get() const { return value.load(std::memory_order_relaxed); }
	void set(float v) { value.store(v, std::memory_order_relaxed); }
	void raise(float v) { if (v > get()) { set(v); } }
};

/*! 0.5.0 L08: the strip stages a console channel has beyond the inherited mixer's fader and balance
 *  pan, run by MixerChannel::doProcessing on the mixer worker (no allocation, no lock), each skipped
 *  while at its default so an older project renders bit for bit as before:
 *  - trim (MX-032 row 111): an input gain before the inserts, in dB;
 *  - polarity per side and mono (MX-032), after the inserts, before the pan;
 *  - pan modes (MX-024): balance (the inherited pan), dual pan (each side placed on its own), width;
 *  - the meters (MX-126 / MX-127 / MX-129 / MX-130): pre-fader peak, RMS, a latched clip flag and the
 *    deepest gain reduction any insert reported this period. */
namespace mixerstrip
{

enum class PanMode
{
	Balance = 0,
	Dual = 1,
	Width = 2
};

//! Trim, before the inserts.
LMMS_EXPORT void applyInput(MixerChannel& channel, f_cnt_t frames);
//! Polarity and mono, after the inserts.
LMMS_EXPORT void applyOutput(MixerChannel& channel, f_cnt_t frames);
//! The pan in its mode; false for balance (the inherited stage does it).
LMMS_EXPORT bool applyPanMode(MixerChannel& channel, f_cnt_t frames);
//! The meters, from the pre-fader block @a buffer and the fader @a volume.
LMMS_EXPORT void updateMeters(MixerChannel& channel, const SampleFrame* buffer, float volume, f_cnt_t frames);

//! The strip's settings as attributes of the channel's element - only those off their defaults.
LMMS_EXPORT void save(const MixerChannel& channel, QDomElement& element);
//! Read back; an absent attribute is the default (so a journal restore takes a setting off again).
LMMS_EXPORT void load(MixerChannel& channel, const QDomElement& element);

//! MX-029: solo mode - exclusive (the inherited behaviour, the default) or additive. A preference
//! kept in the config file (mixer/additivesolo), UI thread.
LMMS_EXPORT bool additiveSolo();
LMMS_EXPORT void setAdditiveSolo(bool additive);

} // namespace mixerstrip

} // namespace lmms

#endif // LMMS_MIXER_STRIP_H
