/*
 * WasmOfflineRenderLimitStubs.cpp - TEST ONLY
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
 * Stands in for the wasmtime-backed WasmSandbox and WasmWorker members that
 * WasmOfflineRender.cpp references, so the real offline-render code links and
 * runs in a build without WANT_WASM. Not production code.
 */

#include "WasmSandbox.h"
#include "WasmWorker.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lmms::wasm::offlineLimitStub
{
// Observed and switched by WasmOfflineRenderLimitTest.
int loadModuleFileCalls = 0;
bool functional = false;
}

namespace lmms::wasm
{

struct WasmSandbox::Impl
{
	std::vector<std::uint8_t> memory;
};

WasmSandbox::WasmSandbox() {}
WasmSandbox::~WasmSandbox() {}

bool WasmSandbox::loadModuleFile(const std::string&, std::string& error)
{
	++offlineLimitStub::loadModuleFileCalls;
	if (!offlineLimitStub::functional)
	{
		error = "offline limit stub: no module";
		return false;
	}
	m_impl = std::make_unique<Impl>();
	m_impl->memory.assign(std::size_t{1} << 20, 0);
	return true;
}

bool WasmSandbox::hasProcess() const { return m_impl != nullptr; }
int WasmSandbox::declaredChannels() const { return 1; }
int WasmSandbox::declaredLatency() const { return 0; }
void WasmSandbox::setParam(std::uint32_t, float) {}
std::uint8_t* WasmSandbox::memoryData() { return m_impl ? m_impl->memory.data() : nullptr; }
std::size_t WasmSandbox::memorySize() const { return m_impl ? m_impl->memory.size() : 0; }

CallResult WasmSandbox::callProcess(std::uint32_t, std::uint32_t, std::uint32_t, float)
{
	CallResult result;
	result.status = CallResult::Status::Ok;
	return result;
}

WasmWorker::~WasmWorker() {}

bool WasmWorker::start(const std::string&, std::string& error, int)
{
	error = "offline limit stub: no worker";
	return false;
}

void WasmWorker::stop() {}
bool WasmWorker::submit(const SampleFrame*, std::uint32_t, float) { return false; }
bool WasmWorker::collect(SampleFrame*, std::uint32_t) { return false; }
void WasmWorker::setParam(std::uint32_t, float) {}
void WasmWorker::setTransportState(std::int32_t) {}

} // namespace lmms::wasm
