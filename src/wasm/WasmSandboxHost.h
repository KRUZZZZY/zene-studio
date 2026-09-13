/*
 * WasmSandboxHost.h - the sandbox a CONTROL CLIENT drives directly
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

#ifndef LMMS_WASM_SANDBOX_HOST_H
#define LMMS_WASM_SANDBOX_HOST_H

#include "WasmAbi.h"
#include "WasmSandbox.h"
#include "lmms_export.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lmms::wasm
{

//! What one load() did. \c ok false means the previous module - if any - is
//! still loaded: a failed load never replaces a working one.
struct HostLoadOutcome
{
	bool ok = false;
	std::string error;
	std::string format;  //!< "wat" or "wasm"
	std::size_t bytes = 0;
	int channels = 1;
	int latency = 0;
	bool hasProcess = false;
	std::size_t memoryBytes = 0;
	std::uint64_t fuelBudget = 0;
};

//! What one process() did. A trap is an expected, contained outcome and is
//! reported here, not thrown: \c status says which of the three happened.
struct HostProcessOutcome
{
	CallResult::Status status = CallResult::Status::Error;
	std::int32_t returnValue = 0;
	int trapCode = -1;
	std::string message;
	std::uint64_t fuelConsumed = 0;
	int planesRun = 0;          //!< process() calls made, one per channel plane
	int channels = 1;
	std::vector<float> output;  //!< output plane 0, read back after the calls
	std::string log;            //!< text the module passed to host_log(), if any
};

/*! The one sandbox the control surface owns.
 *
 * WHY THIS EXISTS. The WASM DSP sandbox reaches audio through the `wasm_effect`
 * plugin, one instance per device, and an instance is only given a module
 * through the plugin's own control dialog - a modal file chooser, i.e. a
 * display. Nothing about the sandbox was therefore observable or drivable from
 * `--control-socket`, which is the one thing this release requires of every
 * feature. This class is the engine half of that: one sandbox owned by the
 * host, driven by the `wasm.*` command group.
 *
 * WHAT IT IS NOT. It is in no device chain and it produces no audio. It runs
 * the SAME `WasmSandbox` the effect runs, against the SAME ABI
 * (docs/WASM-EFFECT-ABI.md), so a module can be loaded, inspected, driven a
 * block at a time and parameterised without a display - but a module loaded
 * here is not heard, and no `wasm.*` command puts one into an effect. That
 * limit is stated in docs/WASM-EFFECT-ABI.md section 13 rather than implied.
 *
 * THREADING. Control thread only: a command handler, which the registry runs on
 * the UI thread. The audio thread never touches it, and unlike the effect's
 * worker there is no second thread here at all - no allocation, no lock and no
 * queue is added to any audio path.
 */
class LMMS_EXPORT WasmSandboxHost
{
public:
	//! The process's one host sandbox.
	static WasmSandboxHost& instance();

	WasmSandboxHost(const WasmSandboxHost&) = delete;
	WasmSandboxHost& operator=(const WasmSandboxHost&) = delete;

	/*! The module format \p path names: "wat", "wasm", or "" for neither.
	 *
	 * ONE definition of the rule, so `wasm.list` (which enumerates candidates)
	 * and `load()` (which reads one) cannot disagree about what is loadable.
	 */
	static std::string formatOf(const std::string& path);

	//! Load \p path, replacing whatever was loaded. Keeps the old module on failure.
	HostLoadOutcome load(const std::string& path, std::uint64_t fuelBudget);
	//! Drop the loaded module. False when there was none.
	bool unload();

	bool isLoaded() const { return m_sandbox != nullptr; }
	const std::string& modulePath() const { return m_path; }
	const std::string& moduleFormat() const { return m_format; }
	std::uint64_t fuelBudget() const { return m_fuel; }
	//! Every ABI quantity below reads 0/1/false when no module is loaded.
	int declaredChannels() const;
	int declaredLatency() const;
	bool hasProcess() const;
	std::size_t memoryBytes() const;

	//! A parameter slot. False when the index is outside `abi::maxParams`.
	bool setParam(std::uint32_t index, float value);
	float param(std::uint32_t index) const;

	/*! Run \p frames samples of \p input through the module.
	 *
	 * \p input is plane 0's samples; anything past \c frames is ignored and a
	 * short (or empty) vector is zero-filled. `process()` is entered once per
	 * channel plane, exactly as WasmWorker enters it (ABI doc section 3), and
	 * plane 0's output is read back. The call's own outcome - including a trap
	 * or fuel exhaustion - is reported in the return value; only "there is
	 * nothing to run" is an error the caller turns into a typed refusal.
	 */
	HostProcessOutcome process(std::uint32_t frames, float sampleRate,
		const std::vector<float>& input);

	//! Text the most recent call passed to host_log(); cleared on read.
	std::string takeLog();

private:
	WasmSandboxHost() = default;

	std::unique_ptr<WasmSandbox> m_sandbox;
	std::string m_path;
	std::string m_format;
	std::uint64_t m_fuel = abi::defaultFuelBudget;
};

} // namespace lmms::wasm

#endif // LMMS_WASM_SANDBOX_HOST_H
