/*
 * SampleOperators.h - the in-tree DSP behind the sample.* group (board #706)
 *
 * Copyright (2026) Zene Studio contributors
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_SAMPLE_OPERATORS_H
#define LMMS_SAMPLE_OPERATORS_H

#include <vector>

#include "SampleFrame.h"

namespace lmms
{
namespace sampleops
{

/*! The generators and bakers of the destructive waveform editor's first slice
 *  (board card #706, design/specs/DECISION-706-DESTRUCTION-MODEL.md §3.2).
 *
 *  LICENCE PROVENANCE, stated because the editor Audacity's own source is
 *  GPLv3: every function here is reimplemented from its PUBLISHED description
 *  (a sine tone, an xorshift white-noise generator, Paul Kellet's widely
 *  published pink-noise filter coefficients, a peak scan, and the four
 *  transforms below). No Audacity code is copied and no third-party DSP is
 *  linked - the arithmetic is in-tree over `SampleFrame`, which is also why
 *  these functions are control-thread only (SPEC A14: no allocation on an
 *  audio-thread path - the socket handler allocates the result, the audio
 *  thread only reads the buffer it is handed).
 *
 *  None of these mutates their input: a baker returns the transformed COPY the
 *  caller swaps in through `SampleClip::setSampleBuffer`, so the frames a clip
 *  already plays are never written in place (decision §2.1 - no operator
 *  writes sample data in place). */
enum class Kind { Tone, White, Pink, Silence };

//! \p amplitude is the peak scale in linear 0..1 applied to tone and noise
//! (silence ignores it); \p frequencyHz only reaches the tone generator.
std::vector<SampleFrame> generate(Kind kind, double frequencyHz, float amplitude,
	f_cnt_t frames, int sampleRate);

//! Every frame multiplied by \p gain - the bake of `sample.amplify` and the
//! scale half of `sample.normalize` (the peak scan lives in peakOf()).
std::vector<SampleFrame> amplify(const SampleFrame* in, f_cnt_t frames, float gain);

//! Frame order reversed, both channels together - `sample.reverse`.
std::vector<SampleFrame> reverse(const SampleFrame* in, f_cnt_t frames);

//! A linear ramp across the whole source: fadeIn grows 0 -> 1, fadeOut 1 -> 0
//! - `sample.fade`. A source of fewer than two frames is returned unchanged
//! (there is no ramp to bake across it).
std::vector<SampleFrame> fade(const SampleFrame* in, f_cnt_t frames, bool fadeIn);

//! The largest absolute sample across both channels (0 for an empty range),
//! which is the number `sample.normalize` scales against.
float peakOf(const SampleFrame* in, f_cnt_t frames);

} // namespace sampleops
} // namespace lmms

#endif // LMMS_SAMPLE_OPERATORS_H
