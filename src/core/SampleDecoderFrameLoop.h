/*
 * SampleDecoderFrameLoop.h - frame iteration shared by sample decoding and its tests
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_SAMPLE_DECODER_FRAME_LOOP_H
#define LMMS_SAMPLE_DECODER_FRAME_LOOP_H

#include <cstddef>

namespace lmms::sample_decoder_detail
{
template <typename Visitor>
auto forEachSampleFrame(std::size_t frameCount, Visitor&& visitor) -> bool
{
	for (auto i = std::size_t{0}; i < frameCount; ++i)
	{
		if (!visitor(i)) { return false; }
	}
	return true;
}
} // namespace lmms::sample_decoder_detail

#endif // LMMS_SAMPLE_DECODER_FRAME_LOOP_H
