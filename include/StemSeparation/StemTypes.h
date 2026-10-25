/*
 * StemTypes.h - shared types of the offline stem-separation feature
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

#ifndef LMMS_STEM_TYPES_H
#define LMMS_STEM_TYPES_H

#include <array>
#include <memory>

namespace lmms
{

class SampleBuffer;

// Stem order is fixed by the HTDemucs 4-stem model output order and is part of
// the model contract; the external-process backend and the reference CLI
// (tools/stem_split_cli.py) emit the same order.
enum class Stem
{
	Drums = 0,
	Bass = 1,
	Other = 2,
	Vocals = 3
};

inline constexpr int NumStems = 4;

// Model tensor names. These are the ONNX graph output names of the model
// contract documented in doc/STEM-SPLIT.md.
inline constexpr const char* stemName(Stem stem)
{
	switch (stem)
	{
	case Stem::Drums: return "drums";
	case Stem::Bass: return "bass";
	case Stem::Other: return "other";
	case Stem::Vocals: return "vocals";
	}
	return "unknown";
}

// One immutable buffer per stem. SampleBuffers are shared and never mutated,
// so stem buffers can be handed to SampleClips directly (see SampleBuffer.h).
using StemSet = std::array<std::shared_ptr<const SampleBuffer>, NumStems>;

// HTDemucs native segment length: 7.8 s at 44.1 kHz (findings-ai-dsp.md 1.2).
// The hybrid transformer needs the full segment as context, which is why stem
// separation can never run on the audio thread.
inline constexpr int HTDemucsSegmentFrames = 343980;

// Model native sample rate. Inputs at other rates are RESAMPLED to it offline
// (0.5.0 E27): the surface and the GUI resample before submitting, so stem
// separation is not limited to 44.1 kHz sources (SPEC-stem-split.md OQ-1 closed).
inline constexpr int StemModelSampleRate = 44100;

// Q050-062.1: the separation quality is how much consecutive model segments
// overlap. Every frame is predicted by more segments at a higher setting and
// the windowed predictions are averaged (overlap-add), at the cost of more
// inference passes: Fast runs about 2/3 of Standard's passes, High about 2x.
// Standard is the overlap every backend used before the setting existed, so a
// caller that does not choose keeps the legacy output bit for bit. Both C++
// backends and tools/stem_split_cli.py (--quality) derive the hop from
// stemHopFrames() / the same integer formula, so a setting means one thing
// everywhere. Real-model SDR per setting is not measured yet (Q050-062 stage 3).
enum class StemQuality
{
	Fast = 0,     //!< 25% overlap (hop = 3/4 segment)
	Standard = 1, //!< 50% overlap (hop = 1/2 segment), the legacy behaviour
	High = 2      //!< 75% overlap (hop = 1/4 segment)
};

inline constexpr StemQuality DefaultStemQuality = StemQuality::Standard;
inline constexpr int NumStemQualities = 3;

inline constexpr const char* stemQualityName(StemQuality quality)
{
	switch (quality)
	{
	case StemQuality::Fast: return "fast";
	case StemQuality::Standard: return "standard";
	case StemQuality::High: return "high";
	}
	return "standard";
}

//! The overlap between consecutive segments, in quarters of a segment (1..3).
inline constexpr int stemOverlapQuarters(StemQuality quality)
{
	switch (quality)
	{
	case StemQuality::Fast: return 1;
	case StemQuality::Standard: return 2;
	case StemQuality::High: return 3;
	}
	return 2;
}

//! The overlap as a fraction of the segment (0.25, 0.5 or 0.75).
inline constexpr double stemOverlapFraction(StemQuality quality)
{
	return stemOverlapQuarters(quality) / 4.0;
}

//! The distance in frames between two consecutive segment starts, never below
//! 1: floor(segment * (4 - quarters) / 4). Standard gives segment / 2 exactly.
inline constexpr int stemHopFrames(int segment, StemQuality quality)
{
	const long long hop = static_cast<long long>(segment) * (4 - stemOverlapQuarters(quality)) / 4;
	return hop < 1 ? 1 : static_cast<int>(hop);
}

} // namespace lmms

#endif // LMMS_STEM_TYPES_H
