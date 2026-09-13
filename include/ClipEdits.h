/*
 * ClipEdits.h - the per-clip fade and clip-gain envelope.
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
 */

#ifndef LMMS_CLIP_EDITS_H
#define LMMS_CLIP_EDITS_H

#include <cmath>

#include <QString>

#include "lmms_export.h"
#include "LmmsTypes.h"

namespace lmms
{

/*! The shape of one fade ramp, as a function of the normalised progress `x`
 *  through the ramp's own region (0 = the ramp's quiet end, 1 = its loud end).
 *
 *  `EqualPower` is the one the crossfade pairing relies on: two ramps of this
 *  shape that meet over the same range sum to unity POWER, i.e.
 *  `g_in(x)^2 + g_out(x)^2 == 1` for every x. That identity is what the
 *  `clip.crossfade` contract is, and it is asserted directly by
 *  `tests/src/core/ClipFadesTest.cpp`. */
enum class FadeShape
{
	Linear = 0,      //!< x - amplitude-linear; a pair sums to unity AMPLITUDE
	Exponential = 1, //!< x^2 - a slow start, the DAW's "logarithmic" ramp
	EqualPower = 2,  //!< sin(x * pi/2) - a pair sums to unity POWER
};

//! The wire name of \p shape ("linear", "exponential", "equal_power").
LMMS_EXPORT QString fadeShapeName(FadeShape shape);
//! Parses fadeShapeName(). False leaves \p shape alone.
LMMS_EXPORT bool fadeShapeFromName(const QString& name, FadeShape* shape);

/*! The ramp's gain at normalised progress \p x, clamped into [0, 1].
 *
 *  Allocation-free, branch-light and table-free: this is called once per
 *  rendered frame from the audio thread. */
LMMS_EXPORT float fadeShapeGain(FadeShape shape, double x);

//! Linear amplitude for a gain in dB (`10^(db/20)`).
LMMS_EXPORT float gainDbToLinear(float db);
//! dB for a linear amplitude (`20*log10`), which is what the project file stores.
LMMS_EXPORT float gainLinearToDb(float linear);

/*! Everything a clip carries besides its window and its position: the clip
 *  gain and the two fades.
 *
 *  Every default is NEUTRAL - gain 1.0, no fades - so a clip nobody has edited
 *  is `isNeutral()`, renders bit for bit as it did before this struct existed,
 *  and serialises without writing a single attribute. That is invariant I9 of
 *  docs/CLIP-CAPTURE-DESIGN.md §2.3, and the reason `clip.set_gain` on a
 *  project nobody has touched cannot move its render hash.
 *
 *  Placement follows docs/CLIP-CAPTURE-DESIGN.md §2.2: the values live on the
 *  base `Clip`, not on `SampleClip`, because a MIDI clip can carry them too.
 *  Applying them to audio is the play handle's job (`SamplePlayHandle`), and
 *  this release applies them to sample clips only - see
 *  docs/KNOWN-LIMITATIONS.md. */
struct ClipEdits
{
	float gain = 1.0f;      //!< linear amplitude multiplier; persisted as dB
	int fadeInTicks = 0;    //!< the ramp up from the clip's first tick
	int fadeOutTicks = 0;   //!< the ramp down to the clip's last tick
	FadeShape fadeInShape = FadeShape::Linear;
	FadeShape fadeOutShape = FadeShape::Linear;

	//! True when this clip contributes nothing to the audio it would not
	//! contribute without any edits at all.
	bool isNeutral() const { return gain == 1.0f && !hasFade(); }
	bool hasFade() const { return fadeInTicks > 0 || fadeOutTicks > 0; }

	bool operator==(const ClipEdits& other) const
	{
		return gain == other.gain
			&& fadeInTicks == other.fadeInTicks && fadeOutTicks == other.fadeOutTicks
			&& fadeInShape == other.fadeInShape && fadeOutShape == other.fadeOutShape;
	}
	bool operator!=(const ClipEdits& other) const { return !(*this == other); }
};

/*! The fade envelope at one output frame of a clip's rendered span.
 *
 *  \p frame counts output frames from the clip's FIRST rendered frame (not from
 *  the start of the current playback pass: a pass that begins in the middle of
 *  a clip must continue the ramp it interrupted, or a seek would restart the
 *  fade). \p clipFrames is the clip's whole rendered span in output frames, and
 *  the two fade lengths are in the same unit.
 *
 *  The two fades are half-open ranges measured inward from the clip's edges, so
 *  `fadeInFrames` of 0 and `fadeOutFrames` of 0 both leave the envelope at 1.0.
 *  An overlap longer than the clip is clamped by the caller, never here.
 *
 *  Allocation-free; no division by a length the caller has not bounded. */
LMMS_EXPORT float clipFadeGainAt(f_cnt_t frame, f_cnt_t clipFrames,
	f_cnt_t fadeInFrames, f_cnt_t fadeOutFrames,
	FadeShape fadeInShape, FadeShape fadeOutShape);

} // namespace lmms

#endif // LMMS_CLIP_EDITS_H
