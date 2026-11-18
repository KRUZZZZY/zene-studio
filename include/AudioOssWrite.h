/*
 * AudioOssWrite.h - retrying byte writes for OSS audio output
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

#ifndef LMMS_AUDIO_OSS_WRITE_H
#define LMMS_AUDIO_OSS_WRITE_H

#include <cerrno>
#include <cstddef>

namespace lmms::audio
{

//! Write one complete buffer through the supplied system-write operation.
template <typename Writer>
auto writeAll(Writer&& writer, const void* data, std::size_t size) -> bool
{
	auto* next = static_cast<const std::byte*>(data);
	while (size > 0)
	{
		const auto written = writer(next, size);
		if (written < 0)
		{
			if (errno == EINTR) { continue; }
			return false;
		}
		if (written == 0) { return false; }

		const auto writtenSize = static_cast<std::size_t>(written);
		if (writtenSize > size) { return false; }
		next += writtenSize;
		size -= writtenSize;
	}
	return true;
}

} // namespace lmms::audio

#endif // LMMS_AUDIO_OSS_WRITE_H
