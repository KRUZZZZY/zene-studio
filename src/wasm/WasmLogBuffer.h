/*
 * WasmLogBuffer.h - bounded, nonblocking log handoff from the WASM host import
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_WASM_LOG_BUFFER_H
#define LMMS_WASM_LOG_BUFFER_H

#include "WasmAbi.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace lmms::wasm
{

//! Fixed-size last-message storage; writers and readers each make one atomic attempt.
class WasmLogBuffer final
{
public:
	//! Store a bounded host_log message, dropping it if a snapshot is in progress.
	void append(const std::uint8_t* text, std::size_t length) noexcept
	{
		if (m_busy.test_and_set(std::memory_order_acquire)) { return; }
		const std::size_t copyLength = std::min(length, m_text.size() - 1);
		std::memcpy(m_text.data(), text, copyLength);
		m_text[copyLength] = '\0';
		m_size = copyLength;
		m_busy.clear(std::memory_order_release);
	}

	//! Return and clear one consistent snapshot; empty means no message or contention.
	std::string take()
	{
		if (m_busy.test_and_set(std::memory_order_acquire)) { return {}; }
		struct Unlock
		{
			std::atomic_flag& busy;
			~Unlock() { busy.clear(std::memory_order_release); }
		} unlock{m_busy};

		if (m_size == 0) { return {}; }
		std::string result(m_text.data(), m_size);
		m_size = 0;
		return result;
	}

private:
	std::atomic_flag m_busy = ATOMIC_FLAG_INIT;
	std::array<char, abi::maxLogBytes> m_text{};
	std::size_t m_size = 0;
};

} // namespace lmms::wasm

#endif // LMMS_WASM_LOG_BUFFER_H
