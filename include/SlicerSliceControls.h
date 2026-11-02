/*
 * SlicerSliceControls.h - the per-slice controls of SlicerT: names, ranges, neutral values (Q050-263)
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

#ifndef LMMS_SLICER_SLICE_CONTROLS_H
#define LMMS_SLICER_SLICE_CONTROLS_H

#include <array>
#include <cstddef>

/*! Q050-263: every playable SlicerT slice (the interval between two adjacent slice points; MIDI key
 *  baseNote + 1 + slice) carries four controls. This header is the one place their names and ranges
 *  live: the SlicerT plug-in (storage, DSP, save/load), the registered commands plugin.slice_get /
 *  plugin.slice_set and the view all include it, so none of them can drift.
 *
 *  - gain   : dB, 0 is neutral.
 *  - pan    : -1 (left) .. +1 (right), 0 is centre. A balance: the far channel is attenuated, the near
 *             one is untouched, so 0 leaves the stereo sample as it is.
 *  - filter : -1 .. +1, 0 is off. Negative values low-pass (100 Hz at -1), positive values high-pass
 *             (8 kHz at +1); both sweep exponentially.
 *  - random : 0 .. 1, 0 is off. Each trigger of the slice draws its own offset, bounded by this amount:
 *             up to +-6 dB of gain and +-0.5 of pan at 1. */
namespace lmms::slicer
{

//! The most slices SlicerT can hold: findSlices() stops after 128 slice points and adds the end point.
inline constexpr std::size_t MaxSlices = 130;

struct SliceControlSpec
{
	const char* name;
	double min;
	double max;
	double neutral;
	const char* unit;
};

inline constexpr std::size_t SliceControlCount = 4;
inline constexpr std::size_t GainControl = 0;
inline constexpr std::size_t PanControl = 1;
inline constexpr std::size_t FilterControl = 2;
inline constexpr std::size_t RandomControl = 3;

inline constexpr std::array<SliceControlSpec, SliceControlCount> SliceControlSpecs{{
	{"gain", -48.0, 12.0, 0.0, "dB"},
	{"pan", -1.0, 1.0, 0.0, ""},
	{"filter", -1.0, 1.0, 0.0, ""},
	{"random", 0.0, 1.0, 0.0, ""},
}};

//! The per-trigger bounds of the random amount at 1.0.
inline constexpr float RandomGainDb = 6.0f;
inline constexpr float RandomPan = 0.5f;

//! The cutoff sweep of the filter control at |filter| = 1, in Hz.
inline constexpr float LowpassFloorHz = 100.0f;
inline constexpr float LowpassCeilingHz = 20000.0f;
inline constexpr float HighpassFloorHz = 20.0f;
inline constexpr float HighpassCeilingHz = 8000.0f;

} // namespace lmms::slicer

#endif // LMMS_SLICER_SLICE_CONTROLS_H
