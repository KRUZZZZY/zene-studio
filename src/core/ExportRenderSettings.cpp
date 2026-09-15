/*
 * ExportRenderSettings.cpp - the current dither / SRC-quality selection.
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

#include "ExportRenderSettings.h"

#include <cstring>

namespace lmms
{

// The defaults are the pre-existing behaviour, and they are written out rather
// than implied: dither OFF (the reproducibility claim) and Linear (the
// converter every render the engine has produced so far used).
std::atomic<bool> ExportRenderSettings::s_dither{false};
std::atomic<int> ExportRenderSettings::s_srcQuality{
	static_cast<int>(SrcQuality::Linear)};
// OFF by default: the report is opt-in (feature row 24 of
// docs/FEATURE-LIST-0.3.0.md), and a render that never asks for one is
// byte-for-byte the render it always was - the loudness report is measure-only,
// but not constructing a meter is still the honest default.
std::atomic<bool> ExportRenderSettings::s_loudnessReport{false};

bool ExportRenderSettings::dither() noexcept
{
	return s_dither.load(std::memory_order_relaxed);
}

void ExportRenderSettings::setDither(bool enabled) noexcept
{
	s_dither.store(enabled, std::memory_order_relaxed);
}

bool ExportRenderSettings::loudnessReport() noexcept
{
	return s_loudnessReport.load(std::memory_order_relaxed);
}

void ExportRenderSettings::setLoudnessReport(bool enabled) noexcept
{
	s_loudnessReport.store(enabled, std::memory_order_relaxed);
}

SrcQuality ExportRenderSettings::srcQuality() noexcept
{
	return static_cast<SrcQuality>(s_srcQuality.load(std::memory_order_relaxed));
}

void ExportRenderSettings::setSrcQuality(SrcQuality quality) noexcept
{
	s_srcQuality.store(static_cast<int>(quality), std::memory_order_relaxed);
}

void ExportRenderSettings::reset() noexcept
{
	setDither(false);
	setSrcQuality(SrcQuality::Linear);
	setLoudnessReport(false);
}

// ---------------------------------------------------------------------------
// The wire/CLI names of the quality enum. One definition for the whole tree:
// the CLI parser (src/core/main.cpp), the control surface
// (src/core/ControlCommandsExport.cpp) and the tests all read the same table,
// so a name can never mean two things in two places.
// ---------------------------------------------------------------------------

const char* srcQualityName(SrcQuality quality) noexcept
{
	switch (quality)
	{
	case SrcQuality::Linear:      return "linear";
	case SrcQuality::SincFastest: return "sinc_fastest";
	case SrcQuality::SincMedium:  return "sinc_medium";
	case SrcQuality::SincBest:    return "sinc_best";
	}
	return "linear";
}

bool srcQualityFromName(const char* name, SrcQuality* out) noexcept
{
	if (name == nullptr || out == nullptr) { return false; }
	const char* const names[] = {"linear", "sinc_fastest", "sinc_medium", "sinc_best"};
	for (int i = 0; i < 4; ++i)
	{
		if (std::strcmp(name, names[i]) == 0)
		{
			*out = static_cast<SrcQuality>(i);
			return true;
		}
	}
	return false;
}

} // namespace lmms
