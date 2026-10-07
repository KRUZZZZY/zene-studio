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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <tuple>

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
	Logarithmic = 3, //!< 1 - (1 - x)^2 - a fast start (0.5.0 L07/L05 ED-174)
	SCurve = 4,      //!< 3x^2 - 2x^3 - slow at both ends
};

//! The wire name of \p shape ("linear", "exponential", "equal_power", "logarithmic", "s_curve").
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
//! ED-042 (0.5.0): one breakpoint of a clip's gain envelope - a tick from the clip's start and a
//! gain in dB. Between two points the gain moves linearly in dB; before the first and after the
//! last it holds.
struct GainPoint
{
	int tick = 0;
	float gainDb = 0.f;
	bool operator==(const GainPoint& other) const { return tick == other.tick && gainDb == other.gainDb; }
};

//! The envelope's bound: a fixed array, so copying ClipEdits onto the audio path never allocates.
constexpr int MaxGainPoints = 32;
//! A point's gain range (clip.set_envelope's -60..+24 dB); a loaded point is held to it too.
constexpr float MinGainPointDb = -60.f;
constexpr float MaxGainPointDb = 24.f;

struct ClipEdits
{
	float gain = 1.0f;      //!< linear amplitude multiplier; persisted as dB
	int fadeInTicks = 0;    //!< the ramp up from the clip's first tick
	int fadeOutTicks = 0;   //!< the ramp down to the clip's last tick
	FadeShape fadeInShape = FadeShape::Linear;
	FadeShape fadeOutShape = FadeShape::Linear;
	//! ED-038: the clip's non-destructive transpose in semitones (-48..+48): a MIDI clip's notes
	//! play transposed, a sample clip is pitch-shifted (Rubber Band) with its timing kept.
	int transpose = 0;
	//! 0.5.0 ED-153: a sample clip's formants under Rubber Band - kept where they were while the pitch
	//! moves (`keepFormants`) and shifted `formant` semitones (-24..24); both off is the plain shift.
	bool keepFormants = false;
	int formant = 0;
	//! 0.5.0 ED-029: a MIDI clip's content repeats every `loopTicks` across the clip's length (0: no loop).
	int loopTicks = 0;
	//! 0.5.0 ED-029 / ED-028 (audio): a sample clip's content loops over the SOURCE frames [audioLoopStart,
	//! audioLoopStart + audioLoopFrames) (absolute, so a tempo change or a trim keeps the loop on the same audio),
	//! and plays from source frame `in-point + audioLoopOffset + elapsed` - folded into the loop once past its end
	//! (a rotation moves the offset). All 0: no loop. See include/ClipAudioLoop.h.
	std::int64_t audioLoopStart = 0;
	std::int64_t audioLoopFrames = 0;
	std::int64_t audioLoopOffset = 0;
	//! 0.5.0 ED-022: a point inside the clip, `snapTicks` from its start, that lands on the grid when the clip is
	//! dragged (0: the clip's start, as always).
	int snapTicks = 0;
	//! 0.5.0 ED-152: a sample clip's speed ramps from `speedFrom` to `speedTo` percent (25..400) across the clip - a
	//! fixed varispeed when the two match; 100 and 100 is plain playback.
	int speedFrom = 100;
	int speedTo = 100;
	//! 0.5.0 ED-179: speed fades - the speed rises from nothing over the first `speedInTicks` (a tape start) and falls
	//! to nothing over the last `speedOutTicks` (a tape stop); 0 for none.
	int speedInTicks = 0;
	int speedOutTicks = 0;
	//! 0.5.0 W3 (ED-179): pre-fades - the fade-in starts `preFadeInTicks` BEFORE the clip's start and the fade-out ends
	//! `preFadeOutTicks` AFTER its end: the clip sounds its hidden source audio there (each ramp is one ramp with the
	//! in-clip fade, if any). 0 for none; saved only when set.
	int preFadeInTicks = 0;
	int preFadeOutTicks = 0;
	//! ED-042: the gain envelope, `envelopeCount` points in tick order.
	std::array<GainPoint, MaxGainPoints> envelope{};
	int envelopeCount = 0;

	//! True when the gain stage has nothing to do: unity gain, no fades, no envelope, no pre-fades.
	//! Speed (hasSpeed()), transpose, formants and loops are not gain edits; callers test them apart.
	bool isNeutral() const { return gain == 1.0f && !hasFade() && !hasEnvelope() && !hasPreFade(); }
	bool hasFade() const { return fadeInTicks > 0 || fadeOutTicks > 0; }
	bool hasSpeed() const { return speedFrom != 100 || speedTo != 100 || speedInTicks > 0 || speedOutTicks > 0; }
	bool hasEnvelope() const { return envelopeCount > 0; }
	bool hasPreFade() const { return preFadeInTicks > 0 || preFadeOutTicks > 0; }
	bool hasAudioLoop() const { return audioLoopFrames > 0; }

	//! Every field but the envelope's points, for comparing (one tuple, so a new field is one more name here).
	auto scalars() const
	{
		return std::tie(gain, fadeInTicks, fadeOutTicks, fadeInShape, fadeOutShape, transpose, keepFormants, formant, loopTicks, snapTicks, speedFrom, speedTo, speedInTicks,
			speedOutTicks, preFadeInTicks, preFadeOutTicks, envelopeCount, audioLoopStart, audioLoopFrames, audioLoopOffset);
	}
	bool operator==(const ClipEdits& other) const
	{
		return scalars() == other.scalars() && std::equal(envelope.begin(), envelope.begin() + envelopeCount, other.envelope.begin());
	}
	bool operator!=(const ClipEdits& other) const { return !(*this == other); }
};

/*! ED-042: the gain envelope's LINEAR gain at @a tick (from the clip's start); 1.0 with no
 *  envelope. Realtime-safe: a bounded scan of a fixed array, no allocation. */
LMMS_EXPORT float envelopeGainAt(const ClipEdits& edits, double tick);

//! The envelope as the project file stores it ("tick:dB;tick:dB"), and back (bounded, sorted).
LMMS_EXPORT QString envelopeToText(const ClipEdits& edits);
LMMS_EXPORT void envelopeFromText(const QString& text, ClipEdits* edits);

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
/*! ED-152 / ED-179: the playback speed (1 is natural) at output frame @a frame of a clip @a clipFrames long, with the
 *  speed fades @a speedInFrames and @a speedOutFrames long (output frames): the ramp from speedFrom to speedTo times
 *  the two fades. Never below 1/32, so the resampler's ratio stays bounded. Allocation-free. */
LMMS_EXPORT float clipSpeedAt(const ClipEdits& edits, f_cnt_t frame, f_cnt_t clipFrames, f_cnt_t speedInFrames, f_cnt_t speedOutFrames);

LMMS_EXPORT float clipFadeGainAt(f_cnt_t frame, f_cnt_t clipFrames,
	f_cnt_t fadeInFrames, f_cnt_t fadeOutFrames,
	FadeShape fadeInShape, FadeShape fadeOutShape);

//! ED-179: a pre-faded clip's span in output frames - the clip, its pre-fades outside it and its fades inside it.
struct PreFadeSpan
{
	std::int64_t clipFrames = 0;
	std::int64_t preInFrames = 0;   //!< the fade-in's part before the clip's first frame
	std::int64_t preOutFrames = 0;  //!< the fade-out's part after the clip's last frame
	std::int64_t fadeInFrames = 0;  //!< the fade-in's part inside the clip
	std::int64_t fadeOutFrames = 0; //!< the fade-out's part inside the clip
};

/*! ED-179: the fade envelope of a pre-faded clip at @a clipPos output frames from its first frame (negative before
 *  it, past @a span.clipFrames after it). The fade-in is ONE ramp from preIn frames before the clip to fadeIn frames
 *  into it, the fade-out one ramp from fadeOut frames before its end to preOut frames after it; 0 outside both.
 *  Only the pre-faded path calls it (a clip without pre-fades keeps clipFadeGainAt). Allocation-free. */
LMMS_EXPORT float preFadedGainAt(const PreFadeSpan& span, std::int64_t clipPos, FadeShape fadeInShape, FadeShape fadeOutShape);

} // namespace lmms

#endif // LMMS_CLIP_EDITS_H
