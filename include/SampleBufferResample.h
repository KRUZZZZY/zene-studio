/*
 * SampleBufferResample.h - a whole sample buffer at another rate (offline)
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

#ifndef LMMS_SAMPLE_BUFFER_RESAMPLE_H
#define LMMS_SAMPLE_BUFFER_RESAMPLE_H

#include <memory>

#include "SampleBuffer.h"
#include "lmms_export.h"

namespace lmms
{

/*! @a input at @a rate, through the engine's own sinc resampler at its best quality - an offline
 *  step (it allocates the whole result), for work that needs a fixed rate: stem separation at
 *  the model's 44.1 kHz (engine audit E27; it used to refuse any other rate). The result keeps
 *  the input's length in time (frames scaled by the rate ratio). @a input itself is returned
 *  when it is already at @a rate. */
LMMS_EXPORT auto resampleBuffer(const std::shared_ptr<const SampleBuffer>& input, sample_rate_t rate)
	-> std::shared_ptr<const SampleBuffer>;

} // namespace lmms

#endif // LMMS_SAMPLE_BUFFER_RESAMPLE_H
