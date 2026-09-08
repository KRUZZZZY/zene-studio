/*
 * WasmSandbox.h - crash-isolated wasmtime host for a single DSP module
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

#ifndef LMMS_WASM_SANDBOX_H
#define LMMS_WASM_SANDBOX_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lmms::wasm
{

//! Outcome of a call into a module. A trap is an expected, contained outcome:
//! the sandbox and the host process both stay alive.
struct CallResult
{
	enum class Status
	{
		Ok,    //!< returned normally
		Trap,  //!< the module trapped (unreachable, out-of-bounds, out of fuel)
		Error  //!< the embedder rejected the call (no export, bad arguments)
	};

	Status status = Status::Error;
	std::int32_t returnValue = 0;
	int trapCode = -1;
	std::string message;
	std::uint64_t fuelConsumed = 0;

	bool ok() const { return status == Status::Ok; }
	bool trapped() const { return status == Status::Trap; }
};

//! Owns one wasmtime engine/store/instance and the host import surface.
//!
//! The sandbox is deliberately single-threaded: it must only be used by the
//! thread that owns it (the WasmWorker thread). The audio thread never touches
//! it; it communicates through WasmWorker's lock-free queues.
class WasmSandbox
{
public:
	WasmSandbox();
	~WasmSandbox();

	WasmSandbox(const WasmSandbox&) = delete;
	WasmSandbox& operator=(const WasmSandbox&) = delete;

	//! Assemble a .wat text module into .wasm bytes. Uses the runtime's own
	//! wat2wasm so the build needs no extra tooling.
	static bool assembleWat(const std::string& wat, std::vector<std::uint8_t>& out, std::string& error);

	//! Compile and instantiate a module. Returns false and sets \p error on
	//! malformed input or failed instantiation; never aborts the process.
	bool loadModuleFile(const std::string& path, std::string& error);
	bool loadModuleBytes(const std::uint8_t* bytes, std::size_t size, std::string& error);

	//! Call process(in_ptr, out_ptr, frames, sample_rate). Offsets are byte
	//! offsets into the module's linear memory - the host never passes a host
	//! pointer into the sandbox.
	CallResult callProcess(std::uint32_t inOffset, std::uint32_t outOffset,
		std::uint32_t frames, float sampleRate);

	//! Call a zero-argument export returning i32 (G1 hello probe, diagnostics).
	CallResult callI32(const std::string& exportName);

	//! Call a zero-argument export returning nothing (host import probe).
	CallResult callVoid(const std::string& exportName);

	std::uint8_t* memoryData();
	std::size_t memorySize() const;
	bool writeMemory(std::uint32_t offset, const void* src, std::size_t bytes);
	bool readMemory(std::uint32_t offset, void* dst, std::size_t bytes);

	void setParam(std::uint32_t index, float value);
	float param(std::uint32_t index) const;
	void setTransportState(std::int32_t state);

	//! Text passed to host_log() by the most recent call ("" if none).
	std::string takeLog();

	int declaredChannels() const;
	int declaredLatency() const;
	//! Whether the loaded module exports the DSP entry point. G1-style probe
	//! modules need not; WasmWorker refuses to start without it.
	bool hasProcess() const;
	void setFuelBudget(std::uint64_t fuel);
	std::uint64_t fuelBudget() const;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace lmms::wasm

#endif // LMMS_WASM_SANDBOX_H
