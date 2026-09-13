/*
 * SrcQuality.h - the export sample-rate-conversion quality.
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

#ifndef LMMS_SRC_QUALITY_H
#define LMMS_SRC_QUALITY_H

#include "lmms_export.h"

namespace lmms
{

/*! The sample-rate-conversion quality a render asks the resampler for.
 *
 *  The engine converts rates with libsamplerate (`AudioResampler`). This
 *  setting selects the *converter*, not the rate:
 *
 *  - `Linear` is `AudioResampler::Mode::Linear` / libsamplerate `SRC_LINEAR`,
 *    the converter this path has always used. It is the DEFAULT, so a render
 *    that does not ask for anything else is byte for byte the render the engine
 *    produced before this setting existed - which is what keeps the
 *    "7 of 9 bundled projects render byte-identically" claim true
 *    (projects/lmms-fl-research/START-HERE.md §3.0).
 *  - the three `Sinc*` values select libsamplerate's sinc converters in
 *    increasing quality, and increasing cost.
 *
 *  It deliberately has no dependency beyond `lmms_export.h`: `OutputSettings.h`
 *  includes it, and `OutputSettings.h` is included almost everywhere.
 */
enum class SrcQuality
{
	Linear = 0,
	SincFastest,
	SincMedium,
	SincBest,
};

//! Wire/CLI name of \p quality: "linear" | "sinc_fastest" | "sinc_medium" |
//! "sinc_best". Never null.
LMMS_EXPORT const char* srcQualityName(SrcQuality quality) noexcept;

//! Parses a wire/CLI name (the same four spellings); false when the name is not
//! one of them, leaving \p out untouched.
LMMS_EXPORT bool srcQualityFromName(const char* name, SrcQuality* out) noexcept;

} // namespace lmms

#endif // LMMS_SRC_QUALITY_H
