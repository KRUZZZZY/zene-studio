/*
 * SessionAudioBake.h - the model-thread rendering of an audio cell's warp and pitch (Q050-013)
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

#ifndef LMMS_SESSION_AUDIO_BAKE_H
#define LMMS_SESSION_AUDIO_BAKE_H

#include <vector>

#include "SampleFrame.h"
#include "lmms_export.h"

namespace lmms
{

class ClipSlot;

namespace sessionaudio
{

/*! What a cell's stored settings ask of its clip, in the two numbers the rendering needs. */
struct BakeParams
{
	//! Source frames consumed per output frame by the pitch-PRESERVING warp: 1 = none. A clip recorded at B bpm
	//! played at T bpm lasts B/T as long, so speed = T/B.
	double warpSpeed = 1.0;
	//! Playback-rate factor of the repitch (2^((transpose + detune/100) / 12)): 1 = none. Like a sampler's
	//! transpose it changes the clip's duration with its pitch.
	double pitchRatio = 1.0;

	bool isIdentity() const noexcept;
	bool operator==(const BakeParams& other) const noexcept = default;
};

//! The parameters @a slot asks for at @a tempo (bpm). A warp-off cell has warpSpeed 1; a warp bpm <= 0 is ignored.
LMMS_EXPORT BakeParams bakeParamsFor(const ClipSlot& slot, double tempo) noexcept;

/*! @a source warped (pitch preserved) and then repitched, as new frames at the same sample rate. Model thread
 *  only (it allocates and runs the stretcher); the audio thread plays the result as a plain buffer, which is why
 *  playback needs no resampler and no allocation. An identity @a params returns a copy. */
LMMS_EXPORT std::vector<SampleFrame> bakeClip(const SampleFrame* source, std::size_t frames, const BakeParams& params);

} // namespace sessionaudio

} // namespace lmms

#endif // LMMS_SESSION_AUDIO_BAKE_H
