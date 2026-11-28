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

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace lmms
{

// The defaults are the pre-existing behaviour, and they are written out rather
// than implied: dither OFF (the reproducibility claim) and Linear (the
// converter every render the engine has produced so far used).
std::atomic<int> ExportRenderSettings::s_ditherMode{static_cast<int>(DitherMode::Off)};
std::atomic<int> ExportRenderSettings::s_srcQuality{
	static_cast<int>(SrcQuality::Linear)};
std::atomic<int> ExportRenderSettings::s_proposedSrcQuality{
	static_cast<int>(SrcQuality::Linear)};
// OFF by default: the report is opt-in (feature row 24 of
// docs/FEATURE-LIST-0.3.0.md), and a render that never asks for one is
// byte-for-byte the render it always was - the loudness report is measure-only,
// but not constructing a meter is still the honest default.
std::atomic<bool> ExportRenderSettings::s_loudnessReport{false};
std::atomic<ExportRenderSettings::SrcQualityListener> ExportRenderSettings::s_srcQualityListener{nullptr};
std::atomic<std::uint64_t> ExportRenderSettings::s_srcQualityPropagationFailures{0};
#ifdef LMMS_TESTING
std::atomic<ExportRenderSettings::SrcQualityPublicationProbe>
	ExportRenderSettings::s_srcQualityPublicationProbe{nullptr};
std::atomic<void*> ExportRenderSettings::s_srcQualityPublicationProbeUserData{nullptr};
#endif

// ---------------------------------------------------------------------------
// The live publications (see publishRenderSelection in the header).
//
// The registry exists because "save my previous value, put it back when I am
// destroyed" is only correct when renderers die in the reverse order of their
// construction. Two renderers can be alive at once - the class permits it, and a
// nested export builds the second while the first still exists - and then the
// older one's destruction used to put the pre-render selection back UNDER the
// newer one while it still had to render (cluster 5239). Here the selection in
// force is always the newest LIVE publication's, whatever order they retire in.
//
// Control threads only: ProjectRenderer's constructor publishes, its destructor
// retires. Nothing on the audio or render path touches this - those read
// srcQuality()/ditherMode() and the atomics above, and never take this lock.
// ---------------------------------------------------------------------------

namespace
{

//! One live render's publication.
struct LiveSelection
{
	std::uint64_t token;
	DitherMode mode;
	SrcQuality quality;
};

std::mutex s_selectionMutex;
std::vector<LiveSelection> s_liveSelections;
std::uint64_t s_nextSelectionToken = 0;
//! The selection found before the first live publication; put back by the last
//! retirement (or dropped by reset()).
DitherMode s_preRenderDither = DitherMode::Off;
SrcQuality s_preRenderQuality = SrcQuality::Linear;
bool s_preRenderValid = false;

} // namespace


DitherMode ExportRenderSettings::ditherMode() noexcept
{
	return static_cast<DitherMode>(s_ditherMode.load(std::memory_order_relaxed));
}

void ExportRenderSettings::setDitherMode(DitherMode mode) noexcept
{
	s_ditherMode.store(static_cast<int>(mode), std::memory_order_relaxed);
}

bool ExportRenderSettings::dither() noexcept
{
	return ditherMode() != DitherMode::Off;
}

void ExportRenderSettings::setDither(bool enabled) noexcept
{
	setDitherMode(enabled ? DitherMode::Tpdf : DitherMode::Off);
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

auto ExportRenderSettings::setSrcQuality(SrcQuality quality) noexcept -> bool
{
	if (quality == srcQuality()) { return true; }
	const auto listener = s_srcQualityListener.load(std::memory_order_acquire);
	if (listener == nullptr)
	{
		s_srcQuality.store(static_cast<int>(quality), std::memory_order_relaxed);
		return true;
	}
	s_proposedSrcQuality.store(static_cast<int>(quality), std::memory_order_relaxed);
#ifdef LMMS_TESTING
	if (const auto probe = s_srcQualityPublicationProbe.load(std::memory_order_acquire))
	{
		probe(s_srcQualityPublicationProbeUserData.load(std::memory_order_acquire));
	}
#endif
	if (listener(quality)) { return true; }
	s_srcQualityPropagationFailures.fetch_add(1, std::memory_order_relaxed);
	return false;
}

void ExportRenderSettings::commitProposedSrcQuality(SrcQuality quality) noexcept
{
	s_srcQuality.store(static_cast<int>(quality), std::memory_order_relaxed);
}

void ExportRenderSettings::setSrcQualityListener(SrcQualityListener listener) noexcept
{
	s_srcQualityListener.store(listener, std::memory_order_release);
}

auto ExportRenderSettings::srcQualityPropagationFailures() noexcept -> std::uint64_t
{
	return s_srcQualityPropagationFailures.load(std::memory_order_relaxed);
}

#ifdef LMMS_TESTING
void ExportRenderSettings::setSrcQualityPublicationProbeForTest(SrcQualityPublicationProbe probe,
	void* userData) noexcept
{
	s_srcQualityPublicationProbeUserData.store(userData, std::memory_order_release);
	s_srcQualityPublicationProbe.store(probe, std::memory_order_release);
}
#endif

void ExportRenderSettings::reset() noexcept
{
	setDither(false);
	setSrcQuality(SrcQuality::Linear);
	setLoudnessReport(false);
	// Drop every live publication as well, so a renderer constructed before the
	// reset cannot restore a selection from before it afterwards.
	const std::lock_guard<std::mutex> lock(s_selectionMutex);
	s_liveSelections.clear();
	s_preRenderValid = false;
}


auto ExportRenderSettings::publishRenderSelection(DitherMode mode, SrcQuality quality) -> RenderSelection
{
	RenderSelection published;
	{
		const std::lock_guard<std::mutex> lock(s_selectionMutex);
		if (!s_preRenderValid)
		{
			// The first publication since the last retirement remembers what it
			// found; every later one leaves that memory alone, so the LAST
			// retirement is the one that puts it back.
			s_preRenderDither = ditherMode();
			s_preRenderQuality = srcQuality();
			s_preRenderValid = true;
		}
		published.token = ++s_nextSelectionToken;
		s_liveSelections.push_back({published.token, mode, quality});
	}
	// Published outside the lock: the SRC change is the engine's transactional
	// one, and it takes the engine's own change guard.
	setDitherMode(mode);
	published.srcQualityApplied = setSrcQuality(quality);
	return published;
}


auto ExportRenderSettings::retireRenderSelection(std::uint64_t token) -> bool
{
	if (token == 0) { return true; }

	DitherMode dither = DitherMode::Off;
	SrcQuality quality = SrcQuality::Linear;
	{
		const std::lock_guard<std::mutex> lock(s_selectionMutex);
		const auto it = std::find_if(s_liveSelections.begin(), s_liveSelections.end(),
			[token](const LiveSelection& entry) { return entry.token == token; });
		if (it == s_liveSelections.end()) { return true; }   // already retired, or dropped by reset()
		s_liveSelections.erase(it);

		if (!s_liveSelections.empty())
		{
			// A newer renderer is still alive: ITS selection is the one in force.
			dither = s_liveSelections.back().mode;
			quality = s_liveSelections.back().quality;
		}
		else
		{
			if (!s_preRenderValid) { return true; }
			s_preRenderValid = false;
			dither = s_preRenderDither;
			quality = s_preRenderQuality;
		}
	}
	setDitherMode(dither);
	return setSrcQuality(quality);
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
