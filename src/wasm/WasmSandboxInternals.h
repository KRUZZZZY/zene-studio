/*
 * WasmSandboxInternals.h - private implementation shared by sandbox translation units
 *
 * Copyright (c) 2026 Zene Studio contributors
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 */

#ifndef LMMS_WASM_SANDBOX_INTERNALS_H
#define LMMS_WASM_SANDBOX_INTERNALS_H

#include "WasmSandbox.h"
#include "WasmAbi.h"
#include "WasmLogBuffer.h"

#include <wasmtime.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace lmms::wasm
{

namespace detail
{
std::string takeError(wasmtime_error_t* error);
std::string takeTrap(wasm_trap_t* trap, int& code);
}

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
	WasmLogBuffer log;

	Impl()
	{
		wasm_config_t* config = wasm_config_new();
		wasmtime_config_consume_fuel_set(config, true);
#if defined(WASM_TEST_DISABLE_PARALLEL_COMPILATION) && defined(WASMTIME_FEATURE_PARALLEL_COMPILATION)
		// Keep ThreadSanitizer focused on the host/worker boundary: this vendored
		// Wasmtime build reports races inside its parallel Cranelift compiler.
		wasmtime_config_parallel_compilation_set(config, false);
#endif
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

	// Drop the previous module and, with it, the instance that came from it.
	void resetModule()
	{
		if (module != nullptr)
		{
			wasmtime_module_delete(module);
			module = nullptr;
		}
		hasProcess = false;
		if (hasInstance)
		{
			// Reload: release the previous instance by dropping its store (the
			// C API has no wasmtime_instance_delete) and instantiate into a
			// fresh one. All handles into the old store are invalidated here.
			resetStore();
		}
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
		self->log.append(data + offset, static_cast<std::size_t>(length));
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

	//! Read the memory and process exports; false (with @a error) when a module
	//! exporting process() does not export linear memory.
	bool readExports(std::string* error)
	{
		wasmtime_extern_t memoryExport;
		const bool hasMemoryExport = exportGet(abi::memoryExport, memoryExport)
			&& memoryExport.kind == WASMTIME_EXTERN_MEMORY;
		if (hasMemoryExport) { memory = memoryExport.of.memory; }
		hasMemory = hasMemoryExport;

		wasmtime_extern_t processExport;
		if (exportGet(abi::processExport, processExport)
			&& processExport.kind == WASMTIME_EXTERN_FUNC)
		{
			process = processExport.of.func;
			hasProcess = true;
		}
		// Linear memory is part of the DSP ABI: any module exporting process()
		// must export it. Pure probe modules (G1 hello) need not.
		if (hasProcess && !hasMemoryExport)
		{
			hasProcess = false;
			*error = "module does not export linear memory '" + std::string(abi::memoryExport) + "'";
			return false;
		}
		return true;
	}

	//! Clamp the module's declared channel count and latency to the ABI's ranges.
	void clampTuning()
	{
		channels = 1;
		latency = 0;
		readGlobalI32(abi::channelsExport, channels);
		readGlobalI32(abi::latencyExport, latency);
		if (channels < 1) { channels = 1; }
		if (channels > static_cast<int>(abi::maxChannels)) { channels = static_cast<int>(abi::maxChannels); }
		if (latency < 0) { latency = 0; }
	}

	//! Fuel consumption is enabled in the engine, so the store carries the
	//! configured budget before any wasm code runs. callWithFuel() primes it per
	//! call; instantiation runs the module's start function outside that path and
	//! needs the same headroom - without it a valid module is rejected on load.
	void primeFuelBudget()
	{
		wasmtime_error_t* error = wasmtime_context_set_fuel(context, fuelBudget);
		if (error != nullptr) { wasmtime_error_delete(error); }
	}

	CallResult callWithFuel(wasmtime_func_t& func, const wasmtime_val_t* args,
		std::size_t nargs, wasmtime_val_t* results, std::size_t nresults)
	{
		CallResult result;
		if (wasmtime_error_t* fuelError = wasmtime_context_set_fuel(context, fuelBudget))
		{
			result.status = CallResult::Status::Error;
			result.message = detail::takeError(fuelError);
			return result;
		}
		wasm_trap_t* trap = nullptr;
		wasmtime_error_t* error = wasmtime_func_call(context, &func, args, nargs,
			results, nresults, &trap);
		if (error != nullptr)
		{
			result.status = CallResult::Status::Error;
			result.message = detail::takeError(error);
		}
		else if (trap != nullptr)
		{
			result.status = CallResult::Status::Trap;
			result.message = detail::takeTrap(trap, result.trapCode);
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


} // namespace lmms::wasm

#endif // LMMS_WASM_SANDBOX_INTERNALS_H
