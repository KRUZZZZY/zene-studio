/*
 * WasmOfflineRenderLimits.h - the frame cap on one offline render
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

#ifndef LMMS_WASM_OFFLINE_RENDER_LIMITS_H
#define LMMS_WASM_OFFLINE_RENDER_LIMITS_H

#include <cstdint>
#include <string>

namespace lmms::wasm
{

//! The most frames one offline render may ask for (2^22).
inline constexpr std::uint32_t maxOfflineRenderFrames = 4194304;

//! The refusal text for a frame count, or "" when the count is within the cap.
inline std::string offlineRenderFramesError(std::uint32_t frames)
{
	if (frames > maxOfflineRenderFrames)
	{
		return "frames must not exceed the offline render frame limit of "
			+ std::to_string(maxOfflineRenderFrames);
	}
	return {};
}

} // namespace lmms::wasm

#endif // LMMS_WASM_OFFLINE_RENDER_LIMITS_H
