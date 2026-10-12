/*
 * SampleDecoder.h - Decodes audio files in various formats
 *
 * Copyright (c) 2023 saker <sakertooth@gmail.com>
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

#ifndef LMMS_SAMPLE_DECODER_H
#define LMMS_SAMPLE_DECODER_H

#include <QString>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "SampleFrame.h"

namespace lmms {
class SampleDecoder
{
public:
	struct Result
	{
		std::vector<SampleFrame> data;
		int sampleRate;
	};

	struct AudioType
	{
		std::string name;
		std::string extension;
	};

	//! A leading window of a file plus the file's full length (Q050-048).
	struct BoundedResult
	{
		//! At most maxSeconds * sampleRate leading frames; data.capacity() is bounded by the same figure.
		Result window;
		//! The whole file's length in frames: from the header where it carries one, else counted while
		//! discarding the rest in small chunks (memory stays bounded either way).
		std::int64_t totalFrames = 0;
	};

	static auto decode(const QString& audioFile) -> std::optional<Result>;
	//! Decodes only the first @a maxSeconds seconds (at the file's own rate) (same channel handling and decoder order as decode()) and
	//! reports the full length. A synthesised DrumSynth file is produced whole, then cut.
	static auto decodeLimited(const QString& audioFile, double maxSeconds) -> std::optional<BoundedResult>;
	static auto supportedAudioTypes() -> const std::vector<AudioType>&;
};
} // namespace lmms

#endif // LMMS_SAMPLE_DECODER_H
