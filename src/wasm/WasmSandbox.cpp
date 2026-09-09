/*
 * WasmSandbox.cpp - crash-isolated wasmtime host for a single DSP module
 *
 * Copyright (c) 2026 LMMS WASM DSP sandbox contributors
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

#include "WasmSandbox.h"

#include "WasmAbi.h"

#include <wasmtime.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>

namespace lmms::wasm
{

namespace
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

} // namespace

struct WasmSandbox::Impl
{
	wasm_engine_t* engine = nullptr;
	wasmtime_store_t* store = nullptr;
	wasmtime_context_t* context = nullptr;
	wasmtime_linker_t* linker = nullptr;
	wasmtime_module_t* module = nullptr;
	wasmtime_instance_t instance{};
	bool hasInstance = false;
	wasmtime_memory_t memory{};
	wasmtime_func_t process{};
	bool hasProcess = false;
	bool hasMemory = false;
	int channels = 1;
	int latency = 0;
	std::uint64_t fuelBudget = abi::defaultFuelBudget;
	std::array<float, abi::maxParams> params{};
	std::atomic<std::int32_t> transportState{abi::transportStopped};
	std::array<char, abi::maxLogBytes> log{};
	std::size_t logSize = 0;

	Impl()
	{
		wasm_config_t* config = wasm_config_new();
		wasmtime_config_consume_fuel_set(config, true);
		engine = wasm_engine_new_with_config(config);
		resetStore();
		linker = wasmtime_linker_new(engine);
		defineImports();
	}

	// Wasmtime instances have no destructor in the C API and are owned by
	// their store; the store limiter counts live instances (limit: 1). A
	// module reload therefore has to drop the whole store and build a fresh
	// one before instantiating into it - otherwise the second instantiation
	// fails with "resource limit exceeded: instance count too high".
	void resetStore()
	{
		if (store != nullptr)
		{
			wasmtime_store_delete(store);
		}
		instance = {};
		memory = {};
		process = {};
		hasInstance = false;
		hasProcess = false;
		hasMemory = false;
		store = wasmtime_store_new(engine, nullptr, nullptr);
		wasmtime_store_limiter(store, abi::storeMemoryLimitBytes,
			abi::storeTableElementLimit, abi::storeInstanceLimit,
			abi::storeTableLimit, abi::storeMemoryCountLimit);
		context = wasmtime_store_context(store);
	}

	~Impl()
	{
		if (module != nullptr)
		{
			wasmtime_module_delete(module);
		}
		wasmtime_linker_delete(linker);
		wasmtime_store_delete(store);
		wasm_engine_delete(engine);
	}

	void defineImports()
	{
		wasm_functype_t* getParamType =
			wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_f32());
		wasmtime_linker_define_func(linker, abi::importModule,
			std::strlen(abi::importModule), abi::hostGetParam,
			std::strlen(abi::hostGetParam), getParamType, hostGetParamCallback,
			this, nullptr);
		wasm_functype_delete(getParamType);

		wasm_functype_t* logType =
			wasm_functype_new_2_0(wasm_valtype_new_i32(), wasm_valtype_new_i32());
		wasmtime_linker_define_func(linker, abi::importModule,
			std::strlen(abi::importModule), abi::hostLog,
			std::strlen(abi::hostLog), logType, hostLogCallback, this, nullptr);
		wasm_functype_delete(logType);

		wasm_functype_t* transportType =
			wasm_functype_new_0_1(wasm_valtype_new_i32());
		wasmtime_linker_define_func(linker, abi::importModule,
			std::strlen(abi::importModule), abi::hostGetTransportState,
			std::strlen(abi::hostGetTransportState), transportType,
			hostGetTransportStateCallback, this, nullptr);
		wasm_functype_delete(transportType);
	}

	static wasm_trap_t* hostGetParamCallback(void* env, wasmtime_caller_t*,
		const wasmtime_val_t* args, std::size_t nargs, wasmtime_val_t* results,
		std::size_t nresults)
	{
		auto* self = static_cast<Impl*>(env);
		if (nargs < 1 || nresults < 1)
		{
			return nullptr;
		}
		const std::int32_t index = args[0].of.i32;
		float value = 0.0f;
		if (index >= 0 && static_cast<std::size_t>(index) < self->params.size())
		{
			value = self->params[static_cast<std::size_t>(index)];
		}
		results[0].kind = WASMTIME_F32;
		results[0].of.f32 = value;
		return nullptr;
	}

	static wasm_trap_t* hostLogCallback(void* env, wasmtime_caller_t* caller,
		const wasmtime_val_t* args, std::size_t nargs, wasmtime_val_t*,
		std::size_t)
	{
		auto* self = static_cast<Impl*>(env);
		if (nargs < 2)
		{
			return nullptr;
		}
		const std::int32_t offset = args[0].of.i32;
		const std::int32_t length = args[1].of.i32;
		if (offset < 0 || length <= 0)
		{
			return nullptr;
		}
		wasmtime_extern_t memoryExport;
		if (!wasmtime_caller_export_get(caller, abi::memoryExport,
				std::strlen(abi::memoryExport), &memoryExport))
		{
			return nullptr;
		}
		if (memoryExport.kind != WASMTIME_EXTERN_MEMORY)
		{
			return nullptr;
		}
		wasmtime_context_t* context = wasmtime_caller_context(caller);
		const std::uint8_t* data =
			wasmtime_memory_data(context, &memoryExport.of.memory);
		const std::size_t size =
			wasmtime_memory_data_size(context, &memoryExport.of.memory);
		if (static_cast<std::size_t>(offset) + static_cast<std::size_t>(length) > size)
		{
			return nullptr;
		}
		const std::size_t copyLength = std::min<std::size_t>(
			static_cast<std::size_t>(length), self->log.size() - 1);
		std::memcpy(self->log.data(), data + offset, copyLength);
		self->log[copyLength] = '\0';
		self->logSize = copyLength;
		return nullptr;
	}

	static wasm_trap_t* hostGetTransportStateCallback(void* env, wasmtime_caller_t*,
		const wasmtime_val_t*, std::size_t, wasmtime_val_t* results,
		std::size_t nresults)
	{
		auto* self = static_cast<Impl*>(env);
		if (nresults < 1)
		{
			return nullptr;
		}
		results[0].kind = WASMTIME_I32;
		results[0].of.i32 = self->transportState.load(std::memory_order_relaxed);
		return nullptr;
	}

	bool exportGet(const char* name, wasmtime_extern_t& out) const
	{
		return wasmtime_instance_export_get(context, &instance, name,
			std::strlen(name), &out);
	}

	void readGlobalI32(const char* name, int& target)
	{
		wasmtime_extern_t globalExport;
		if (!exportGet(name, globalExport) ||
			globalExport.kind != WASMTIME_EXTERN_GLOBAL)
		{
			return;
		}
		wasmtime_val_t value;
		wasmtime_global_get(context, &globalExport.of.global, &value);
		if (value.kind == WASMTIME_I32)
		{
			target = value.of.i32;
		}
	}

	CallResult callWithFuel(wasmtime_func_t& func, const wasmtime_val_t* args,
		std::size_t nargs, wasmtime_val_t* results, std::size_t nresults)
	{
		CallResult result;
		if (wasmtime_error_t* fuelError = wasmtime_context_set_fuel(context, fuelBudget))
		{
			result.status = CallResult::Status::Error;
			result.message = takeError(fuelError);
			return result;
		}
		wasm_trap_t* trap = nullptr;
		wasmtime_error_t* error = wasmtime_func_call(context, &func, args, nargs,
			results, nresults, &trap);
		if (error != nullptr)
		{
			result.status = CallResult::Status::Error;
			result.message = takeError(error);
		}
		else if (trap != nullptr)
		{
			result.status = CallResult::Status::Trap;
			result.message = takeTrap(trap, result.trapCode);
		}
		else
		{
			result.status = CallResult::Status::Ok;
		}
		std::uint64_t remaining = 0;
		if (wasmtime_error_t* fuelError =
				wasmtime_context_get_fuel(context, &remaining))
		{
			// Fuel is enabled in the engine config, so this cannot happen; if it
			// ever does, drop the error rather than leak it.
			wasmtime_error_delete(fuelError);
		}
		else
		{
			result.fuelConsumed = fuelBudget - remaining;
		}
		return result;
	}
};

WasmSandbox::WasmSandbox() :
	m_impl(std::make_unique<Impl>())
{
}

WasmSandbox::~WasmSandbox() = default;

bool WasmSandbox::assembleWat(const std::string& wat, std::vector<std::uint8_t>& out,
	std::string& error)
{
	wasm_byte_vec_t bytes;
	wasmtime_error_t* err = wasmtime_wat2wasm(wat.data(), wat.size(), &bytes);
	if (err != nullptr)
	{
		error = takeError(err);
		return false;
	}
	out.assign(reinterpret_cast<const std::uint8_t*>(bytes.data),
		reinterpret_cast<const std::uint8_t*>(bytes.data) + bytes.size);
	wasm_byte_vec_delete(&bytes);
	return true;
}

bool WasmSandbox::loadModuleBytes(const std::uint8_t* bytes, std::size_t size,
	std::string& error)
{
	if (m_impl->module != nullptr)
	{
		wasmtime_module_delete(m_impl->module);
		m_impl->module = nullptr;
	}
	m_impl->hasProcess = false;
	if (m_impl->hasInstance)
	{
		// Reload: release the previous instance by dropping its store (the
		// C API has no wasmtime_instance_delete) and instantiate into a
		// fresh one. All handles into the old store are invalidated here.
		m_impl->resetStore();
	}

	wasmtime_error_t* err = wasmtime_module_new(m_impl->engine, bytes, size,
		&m_impl->module);
	if (err != nullptr)
	{
		error = "module compile failed: " + takeError(err);
		return false;
	}

	wasm_trap_t* trap = nullptr;
	err = wasmtime_linker_instantiate(m_impl->linker, m_impl->context,
		m_impl->module, &m_impl->instance, &trap);
	if (err != nullptr)
	{
		error = "instantiation failed: " + takeError(err);
		return false;
	}
	if (trap != nullptr)
	{
		int code = -1;
		error = "instantiation trapped: " + takeTrap(trap, code);
		return false;
	}
	m_impl->hasInstance = true;

	wasmtime_extern_t memoryExport;
	const bool hasMemory = m_impl->exportGet(abi::memoryExport, memoryExport) &&
		memoryExport.kind == WASMTIME_EXTERN_MEMORY;
	if (hasMemory)
	{
		m_impl->memory = memoryExport.of.memory;
	}
	m_impl->hasMemory = hasMemory;

	wasmtime_extern_t processExport;
	if (m_impl->exportGet(abi::processExport, processExport) &&
		processExport.kind == WASMTIME_EXTERN_FUNC)
	{
		m_impl->process = processExport.of.func;
		m_impl->hasProcess = true;
	}
	// Linear memory is part of the DSP ABI: any module exporting process()
	// must export it. Pure probe modules (G1 hello) need not.
	if (m_impl->hasProcess && !hasMemory)
	{
		m_impl->hasProcess = false;
		error = "module does not export linear memory '" +
			std::string(abi::memoryExport) + "'";
		return false;
	}

	m_impl->channels = 1;
	m_impl->latency = 0;
	m_impl->readGlobalI32(abi::channelsExport, m_impl->channels);
	m_impl->readGlobalI32(abi::latencyExport, m_impl->latency);
	if (m_impl->channels < 1)
	{
		m_impl->channels = 1;
	}
	if (m_impl->channels > static_cast<int>(abi::maxChannels))
	{
		m_impl->channels = static_cast<int>(abi::maxChannels);
	}
	if (m_impl->latency < 0)
	{
		m_impl->latency = 0;
	}
	return true;
}

bool WasmSandbox::loadModuleFile(const std::string& path, std::string& error)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		error = "cannot open module '" + path + "'";
		return false;
	}
	std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());
	if (bytes.empty())
	{
		error = "module '" + path + "' is empty";
		return false;
	}
	return loadModuleBytes(bytes.data(), bytes.size(), error);
}

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
	return m_impl->hasMemory ? wasmtime_memory_data(m_impl->context, &m_impl->memory)
							 : nullptr;
}

std::size_t WasmSandbox::memorySize() const
{
	return m_impl->hasMemory
		? wasmtime_memory_data_size(m_impl->context, &m_impl->memory)
		: 0;
}

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
	if (m_impl->logSize == 0)
	{
		return {};
	}
	std::string result(m_impl->log.data(), m_impl->logSize);
	m_impl->logSize = 0;
	return result;
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
