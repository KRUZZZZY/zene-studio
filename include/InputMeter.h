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

namespace lmms
{

class SampleFrame;

/*! 0.5.0 L08 (MX-134): how loud the audio input is - the engine's stereo input bus, measured once a
 *  period on the audio thread (two relaxed atomic maxima, no allocation, no lock) and read - and reset -
 *  by meter.input, the record panel's level. */
namespace inputmeter
{

//! Audio thread: raise the held peaks with @a frames frames of @a buffer.
LMMS_EXPORT void measure(const SampleFrame* buffer, f_cnt_t frames);
//! The peaks held since the last take, linear; they start again from zero.
LMMS_EXPORT void take(float* left, float* right);

} // namespace inputmeter

} // namespace lmms

#endif // LMMS_INPUT_METER_H
