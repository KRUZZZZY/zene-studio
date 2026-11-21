/*
 * WasmSandbox.cpp - crash-isolated wasmtime host for a single DSP module
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "WasmSandboxInternals.h"

#include "WasmAbi.h"
#include "WasmLogBuffer.h"

#include <wasmtime.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>

namespace lmms::wasm
{

namespace
{

#ifdef LMMS_TESTING
using MemoryViewTestHook = WasmSandbox::MemoryViewTestHook;
std::atomic<MemoryViewTestHook> memoryViewTestHook{nullptr};
std::atomic<void*> memoryViewTestContext{nullptr};
#endif


} // namespace


namespace detail
{

//! Consume a wasmtime error and render it as text. Takes ownership.
std::string takeError(wasmtime_error_t* error)
{
	if (error == nullptr)
	{
		return {};
	}
	wasm_name_t message;
	wasmtime_error_message(error, &message);
	std::string result(message.data, message.size);
	wasm_name_delete(&message);
	wasmtime_error_delete(error);
	return result;
}

//! Consume a trap and render it as text, recording the trap code. Takes ownership.
std::string takeTrap(wasm_trap_t* trap, int& code)
{
	code = -1;
	if (trap == nullptr)
	{
		return {};
	}
	wasmtime_trap_code_t trapCode = 0;
	if (wasmtime_trap_code(trap, &trapCode))
	{
		code = static_cast<int>(trapCode);
	}
	wasm_name_t message;
	wasm_trap_message(trap, &message);
	std::string result(message.data, message.size);
	wasm_name_delete(&message);
	wasm_trap_delete(trap);
	return result;
}

} // namespace detail


WasmSandbox::WasmSandbox() :
	m_impl(std::make_unique<Impl>())
{
}

WasmSandbox::~WasmSandbox() = default;


CallResult WasmSandbox::callProcess(std::uint32_t inOffset, std::uint32_t outOffset,
	std::uint32_t frames, float sampleRate)
{
	CallResult result;
	if (!m_impl->hasProcess)
	{
		result.status = CallResult::Status::Error;
		result.message = "no process() export loaded";
		return result;
	}
	wasmtime_val_t args[4];
	args[0].kind = WASMTIME_I32;
	args[0].of.i32 = static_cast<std::int32_t>(inOffset);
	args[1].kind = WASMTIME_I32;
	args[1].of.i32 = static_cast<std::int32_t>(outOffset);
	args[2].kind = WASMTIME_I32;
	args[2].of.i32 = static_cast<std::int32_t>(frames);
	args[3].kind = WASMTIME_F32;
	args[3].of.f32 = sampleRate;

	wasmtime_val_t results[1];
	result = m_impl->callWithFuel(m_impl->process, args, 4, results, 1);
	if (result.ok() && results[0].kind == WASMTIME_I32)
	{
		result.returnValue = results[0].of.i32;
	}
	return result;
}

CallResult WasmSandbox::callI32(const std::string& exportName)
{
	CallResult result;
	wasmtime_extern_t exported;
	if (!m_impl->exportGet(exportName.c_str(), exported) ||
		exported.kind != WASMTIME_EXTERN_FUNC)
	{
		result.status = CallResult::Status::Error;
		result.message = "no exported function '" + exportName + "'";
		return result;
	}
	wasmtime_func_t func = exported.of.func;
	wasmtime_val_t results[1];
	result = m_impl->callWithFuel(func, nullptr, 0, results, 1);
	if (result.ok() && results[0].kind == WASMTIME_I32)
	{
		result.returnValue = results[0].of.i32;
	}
	return result;
}

CallResult WasmSandbox::callVoid(const std::string& exportName)
{
	CallResult result;
	wasmtime_extern_t exported;
	if (!m_impl->exportGet(exportName.c_str(), exported) ||
		exported.kind != WASMTIME_EXTERN_FUNC)
	{
		result.status = CallResult::Status::Error;
		result.message = "no exported function '" + exportName + "'";
		return result;
	}
	wasmtime_func_t func = exported.of.func;
	return m_impl->callWithFuel(func, nullptr, 0, nullptr, 0);
}

std::uint8_t* WasmSandbox::memoryData()
{
	std::uint8_t* data = m_impl->hasMemory
		? wasmtime_memory_data(m_impl->context, &m_impl->memory)
		: nullptr;
#ifdef LMMS_TESTING
	if (const auto hook = memoryViewTestHook.load(std::memory_order_acquire))
	{
		std::size_t size = m_impl->hasMemory
			? wasmtime_memory_data_size(m_impl->context, &m_impl->memory)
			: 0;
		hook(memoryViewTestContext.load(std::memory_order_relaxed), true, data,
			size);
	}
#endif
	return data;
}

std::size_t WasmSandbox::memorySize() const
{
	std::size_t size = m_impl->hasMemory
		? wasmtime_memory_data_size(m_impl->context, &m_impl->memory)
		: 0;
#ifdef LMMS_TESTING
	if (const auto hook = memoryViewTestHook.load(std::memory_order_acquire))
	{
		std::uint8_t* data = m_impl->hasMemory
			? wasmtime_memory_data(m_impl->context, &m_impl->memory)
			: nullptr;
		hook(memoryViewTestContext.load(std::memory_order_relaxed), false, data,
			size);
	}
#endif
	return size;
}

#ifdef LMMS_TESTING
void WasmSandbox::setMemoryViewTestHook(MemoryViewTestHook hook, void* context)
{
	memoryViewTestContext.store(context, std::memory_order_relaxed);
	memoryViewTestHook.store(hook, std::memory_order_release);
}
#endif

bool WasmSandbox::writeMemory(std::uint32_t offset, const void* src,
	std::size_t bytes)
{
	if (offset > memorySize() || bytes > memorySize() - offset)
	{
		return false;
	}
	std::memcpy(memoryData() + offset, src, bytes);
	return true;
}

bool WasmSandbox::readMemory(std::uint32_t offset, void* dst, std::size_t bytes)
{
	if (offset > memorySize() || bytes > memorySize() - offset)
	{
		return false;
	}
	std::memcpy(dst, memoryData() + offset, bytes);
	return true;
}

void WasmSandbox::setParam(std::uint32_t index, float value)
{
	if (index < m_impl->params.size())
	{
		m_impl->params[index] = value;
	}
}

float WasmSandbox::param(std::uint32_t index) const
{
	if (index < m_impl->params.size())
	{
		return m_impl->params[index];
	}
	return 0.0f;
}

void WasmSandbox::setTransportState(std::int32_t state)
{
	m_impl->transportState.store(state, std::memory_order_relaxed);
}

std::string WasmSandbox::takeLog()
{
	return m_impl->log.take();
}

int WasmSandbox::declaredChannels() const
{
	return m_impl->channels;
}

int WasmSandbox::declaredLatency() const
{
	return m_impl->latency;
}

bool WasmSandbox::hasProcess() const
{
	return m_impl->hasProcess;
}

void WasmSandbox::setFuelBudget(std::uint64_t fuel)
{
	m_impl->fuelBudget = fuel;
}

std::uint64_t WasmSandbox::fuelBudget() const
{
	return m_impl->fuelBudget;
}

} // namespace lmms::wasm
