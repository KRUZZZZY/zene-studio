/*
 * StemTypes.h - shared types of the offline stem-separation feature
 *
 * Copyright (c) 2026 LMMS Developers
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

// Model native sample rate. Inputs at other rates are rejected in v1; a
// resampler is deliberately left as an open question (SPEC-stem-split.md OQ-1).
inline constexpr int StemModelSampleRate = 44100;

} // namespace lmms

#endif // LMMS_STEM_TYPES_H
