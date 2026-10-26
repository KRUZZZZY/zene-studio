/*
 * InputMeter.h - the level of the engine's audio input
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
#ifndef LMMS_INPUT_METER_H
#define LMMS_INPUT_METER_H

#include "LmmsTypes.h"
#include "lmms_export.h"

#ifdef LMMS_TESTING
#	include <atomic>
#endif

namespace lmms
{

class SampleFrame;

/*! 0.5.0 L08 (MX-134): how loud the audio input is - the engine's stereo input bus, measured once a
 *  period on the audio thread (two pairs of relaxed atomic maxima, no allocation, no lock)
 *  and read - and reset - by meter.input, the record panel's level; and read live, without the reset, by
 *  the input meters Large Meters draws (InputLevelMeter.h). */
namespace inputmeter
{

//! Audio thread: raise the held peaks with @a frames frames of @a buffer.
LMMS_EXPORT void measure(const SampleFrame* buffer, f_cnt_t frames);
//! The peaks held since the last take, linear; they start again from zero.
LMMS_EXPORT void take(float* left, float* right);
//! The live meter's own read (InputLevelMeter.h, polled at 25 Hz): the peaks since ITS last read, linear -
//! every period's, so a transient between two polls is not missed - from a second pair of maxima, so it
//! never empties what meter.input takes, nor the other way round. One live reader at a time.
LMMS_EXPORT void live(float* left, float* right);
//! The peak watcher's own read (0.5.0 Q050-100): the peaks since ITS last read, linear. A THIRD pair of
//! maxima, because a threshold crossing must neither consume the live meter's reading nor meter.input's.
//! One watcher at a time; the caller owns the crossing latch.
LMMS_EXPORT void watch(float* left, float* right);

#ifdef LMMS_TESTING
namespace detail
{

//! Test seam: the point where the audio thread has observed the held peak and is about to commit the
//!  raise. The hook is called with the cell it observed and the value it saw; a test may clear the
//!  cell there, exactly as a reader that reads in that window does. Production never installs a hook.
using PeakObservationHook = void (*)(std::atomic<float>& held, float observed);

//! Install the test seam's hook (nullptr removes it).
LMMS_EXPORT void setPeakObservationHook(PeakObservationHook hook);

} // namespace detail
#endif

} // namespace inputmeter

} // namespace lmms

#endif // LMMS_INPUT_METER_H
