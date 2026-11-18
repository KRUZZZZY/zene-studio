/*
 * AudioAlsaWrite.h - helpers for writing complete ALSA playback periods
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

#ifndef LMMS_AUDIO_ALSA_WRITE_H
#define LMMS_AUDIO_ALSA_WRITE_H

#include <cerrno>
#include <cstddef>
#include <type_traits>

namespace lmms::alsa::detail
{

template <typename FrameCount, typename Write>
auto writeAllFrames(const float* data, FrameCount frames, std::size_t channels, Write&& write)
	-> std::invoke_result_t<Write, const float*, FrameCount>
{
	using WriteResult = std::invoke_result_t<Write, const float*, FrameCount>;
	auto remaining = frames;
	while (remaining > 0)
	{
		const WriteResult framesWritten = write(data, remaining);
		if (framesWritten < 0)
		{
			return framesWritten;
		}
		if (framesWritten == 0)
		{
			return static_cast<WriteResult>(-EIO);
		}

		const auto writtenFrames = static_cast<FrameCount>(framesWritten);
		if (writtenFrames > remaining)
		{
			return static_cast<WriteResult>(-EIO);
		}
		data += static_cast<std::size_t>(writtenFrames) * channels;
		remaining -= writtenFrames;
	}
	return static_cast<WriteResult>(frames);
}

} // namespace lmms::alsa::detail

#endif // LMMS_AUDIO_ALSA_WRITE_H
