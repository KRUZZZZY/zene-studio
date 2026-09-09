/*
 * WasmAbi.h - the frozen v0 audio ABI for WASM DSP modules
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

#ifndef LMMS_WASM_ABI_H
#define LMMS_WASM_ABI_H

#include <cstddef>
#include <cstdint>

namespace lmms::wasm::abi
{

//! ABI version exported by every module as the "abi" global. The host refuses
//! to load modules that export a different value.
constexpr std::int32_t version = 1;

//! Import module namespace for all host functions.
constexpr const char* importModule = "env";

//! Required export: the DSP entry point.
//! process(in_ptr, out_ptr, frames, sample_rate) -> i32 (0 = ok)
constexpr const char* processExport = "process";

//! Required export: linear memory holding the planar sample planes.
constexpr const char* memoryExport = "memory";

//! Optional export: number of process() calls the host must make per block,
//! one per channel plane (default 1, clamped to [1, maxChannels]).
constexpr const char* channelsExport = "channels";

//! Optional export: declared latency in frames (default 0).
constexpr const char* latencyExport = "latency";

//! Optional export: ABI version (default 1).
constexpr const char* abiVersionExport = "abi";

//! Host functions importable from "env".
constexpr const char* hostGetParam = "host_get_param";
constexpr const char* hostLog = "host_log";
constexpr const char* hostGetTransportState = "host_get_transport_state";

//! Hard limits enforced by the host.
constexpr std::uint32_t maxChannels = 2;
constexpr std::uint32_t maxParams = 16;
constexpr std::size_t maxLogBytes = 4096;

//! Default per-process()-call fuel budget. A normal 48-frame mono block costs
//! a few thousand fuel units; 1e6 leaves three orders of magnitude of headroom
//! while still bounding a runaway module to a few milliseconds.
constexpr std::uint64_t defaultFuelBudget = 1000000;

//! Store limits: 16 MiB of linear memory, 1 instance, 1 memory, 1 table.
constexpr std::int64_t storeMemoryLimitBytes = 16 * 1024 * 1024;
constexpr std::int64_t storeTableElementLimit = 10000;
constexpr std::int64_t storeInstanceLimit = 1;
constexpr std::int64_t storeTableLimit = 1;
constexpr std::int64_t storeMemoryCountLimit = 1;

//! Transport state values returned by host_get_transport_state().
constexpr std::int32_t transportStopped = 0;
constexpr std::int32_t transportPlaying = 1;

} // namespace lmms::wasm::abi

#endif // LMMS_WASM_ABI_H
