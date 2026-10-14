/*
 * OssSampleConversion.h - conversion of floating-point audio to OSS PCM
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
 */

#pragma once

#include <cmath>
#include <cstdint>

#include "AudioEngine.h"

namespace lmms::audio::oss
{

inline std::int16_t toPcmSample(sample_t sample) noexcept
{
	if (std::isnan(sample)) { sample = 0.0f; }
	return static_cast<std::int16_t>(AudioEngine::clip(sample) * OUTPUT_SAMPLE_MULTIPLIER);
}

} // namespace lmms::audio::oss
