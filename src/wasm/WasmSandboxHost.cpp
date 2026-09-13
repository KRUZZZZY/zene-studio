/*
 * WasmSandboxHost.cpp - the sandbox a CONTROL CLIENT drives directly
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

#include "WasmSandboxHost.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>

namespace lmms::wasm
{

namespace
{

//! Read the whole of \p path. An unreadable or empty file is an error: both are
//! module problems the caller reports as a typed refusal, never as a silent pass.
bool readWholeFile(const std::string& path, std::vector<std::uint8_t>& out,
	std::string& error)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		error = "cannot open module '" + path + "'";
		return false;
	}
	out.assign(std::istreambuf_iterator<char>(file),
		std::istreambuf_iterator<char>());
	if (out.empty())
	{
		error = "module '" + path + "' is empty";
		return false;
	}
	return true;
}

/*! The bytes `loadModuleBytes` wants, for the format `formatOf` named.
 *
 * A `.wat` is text and goes through the runtime's own assembler
 * (WasmSandbox::assembleWat), which is why the build needs no wabt, cargo or
 * emscripten. A `.wasm` is already the encoding and is passed through as read.
 */
bool decodeModule(const std::string& path, const std::string& format,
	std::vector<std::uint8_t>& bytes, std::string& error)
{
	if (!readWholeFile(path, bytes, error)) { return false; }
	if (format != "wat") { return true; }

	const std::string text(bytes.begin(), bytes.end());
	std::vector<std::uint8_t> assembled;
	if (!WasmSandbox::assembleWat(text, assembled, error))
	{
		error = "assembling '" + path + "' failed: " + error;
		return false;
	}
	if (assembled.empty())
	{
		error = "assembling '" + path + "' produced no bytes";
		return false;
	}
	bytes.swap(assembled);
	return true;
}

//! Plane 0's samples for a block of \p frames: a short or empty vector is
//! zero-filled, and anything past \p frames is ignored. Bounded by `frames`,
//! so a long `input` array cannot make the block bigger than it was asked for.
std::vector<float> inputPlane(const std::vector<float>& input, std::uint32_t frames)
{
	std::vector<float> plane(frames, 0.0f);
	const std::size_t count = std::min<std::size_t>(input.size(), frames);
	std::copy(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(count),
		plane.begin());
	return plane;
}

//! Byte offset of a plane's first sample, given the layout in ABI doc section 5.
std::uint32_t planeOffset(std::size_t index, std::size_t planeBytes)
{
	return static_cast<std::uint32_t>(index * planeBytes);
}

} // namespace

WasmSandboxHost& WasmSandboxHost::instance()
{
	static WasmSandboxHost host;
	return host;
}

std::string WasmSandboxHost::formatOf(const std::string& path)
{
	const std::string::size_type dot = path.find_last_of('.');
	if (dot == std::string::npos) { return {}; }

	std::string extension = path.substr(dot + 1);
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (extension == "wat") { return std::string("wat"); }
	if (extension == "wasm") { return std::string("wasm"); }
	return {};
}

HostLoadOutcome WasmSandboxHost::load(const std::string& path,
	std::uint64_t fuelBudget)
{
	HostLoadOutcome outcome;
	outcome.format = formatOf(path);
	if (outcome.format.empty())
	{
		outcome.error = "'" + path + "' is neither a .wat nor a .wasm module";
		return outcome;
	}

	std::vector<std::uint8_t> bytes;
	if (!decodeModule(path, outcome.format, bytes, outcome.error)) { return outcome; }

	// Compile and instantiate into a FRESH sandbox FIRST: a module that fails to
	// load must leave the running one alone. `wasm.load` is therefore atomic,
	// and the inverse a failed call never consumed stays the truth.
	auto candidate = std::make_unique<WasmSandbox>();
	if (!candidate->loadModuleBytes(bytes.data(), bytes.size(), outcome.error))
	{
		return outcome;
	}

	const std::uint64_t budget =
		fuelBudget == 0 ? abi::defaultFuelBudget : fuelBudget;
	candidate->setFuelBudget(budget);
	m_sandbox = std::move(candidate);
	m_path = path;
	m_format = outcome.format;
	m_fuel = budget;

	outcome.ok = true;
	outcome.bytes = bytes.size();
	outcome.channels = m_sandbox->declaredChannels();
	outcome.latency = m_sandbox->declaredLatency();
	outcome.hasProcess = m_sandbox->hasProcess();
	outcome.memoryBytes = m_sandbox->memorySize();
	outcome.fuelBudget = budget;
	return outcome;
}

bool WasmSandboxHost::unload()
{
	const bool had = m_sandbox != nullptr;
	m_sandbox.reset();
	m_path.clear();
	m_format.clear();
	m_fuel = abi::defaultFuelBudget;
	return had;
}

int WasmSandboxHost::declaredChannels() const
{
	return m_sandbox == nullptr ? 1 : m_sandbox->declaredChannels();
}

int WasmSandboxHost::declaredLatency() const
{
	return m_sandbox == nullptr ? 0 : m_sandbox->declaredLatency();
}

bool WasmSandboxHost::hasProcess() const
{
	return m_sandbox != nullptr && m_sandbox->hasProcess();
}

std::size_t WasmSandboxHost::memoryBytes() const
{
	return m_sandbox == nullptr ? 0 : m_sandbox->memorySize();
}

bool WasmSandboxHost::setParam(std::uint32_t index, float value)
{
	if (m_sandbox == nullptr || index >= abi::maxParams) { return false; }
	m_sandbox->setParam(index, value);
	return true;
}

float WasmSandboxHost::param(std::uint32_t index) const
{
	if (m_sandbox == nullptr || index >= abi::maxParams) { return 0.0f; }
	return m_sandbox->param(index);
}

HostProcessOutcome WasmSandboxHost::process(std::uint32_t frames, float sampleRate,
	const std::vector<float>& input)
{
	HostProcessOutcome outcome;
	if (m_sandbox == nullptr)
	{
		outcome.message = "no module is loaded";
		return outcome;
	}
	outcome.channels = m_sandbox->declaredChannels();
	if (!m_sandbox->hasProcess())
	{
		outcome.message = "the loaded module exports no process()";
		return outcome;
	}

	// ABI doc section 5: the block needs 2 * channels * planeBytes of linear
	// memory. The host refuses a block that does not fit rather than writing
	// past the module's memory, and says which quantity was short.
	const std::size_t planeBytes = static_cast<std::size_t>(frames) * sizeof(float);
	const std::size_t needed = 2u * static_cast<std::size_t>(outcome.channels) * planeBytes;
	if (m_sandbox->memorySize() < needed)
	{
		outcome.message = "the module's linear memory is too small for the block: '"
			+ std::to_string(needed) + "' bytes are needed and it exports '"
			+ std::to_string(m_sandbox->memorySize()) + "'";
		return outcome;
	}

	const std::vector<float> plane = inputPlane(input, frames);
	for (int channel = 0; channel < outcome.channels; ++channel)
	{
		const std::uint32_t in = planeOffset(static_cast<std::size_t>(channel), planeBytes);
		const std::uint32_t out = planeOffset(
			static_cast<std::size_t>(outcome.channels + channel), planeBytes);
		if (!m_sandbox->writeMemory(in, plane.data(), planeBytes))
		{
			outcome.message = "the module's linear memory refused a "
				+ std::to_string(frames) + "-frame input plane";
			return outcome;
		}

		const CallResult call = m_sandbox->callProcess(in, out, frames, sampleRate);
		outcome.status = call.status;
		outcome.returnValue = call.returnValue;
		outcome.trapCode = call.trapCode;
		outcome.message = call.message;
		outcome.fuelConsumed += call.fuelConsumed;
		++outcome.planesRun;
		if (!call.ok())
		{
			// A trap or an embedder rejection stops the block: the loop in
			// WasmWorker does the same and quarantines the module for it.
			outcome.log = m_sandbox->takeLog();
			return outcome;
		}
	}

	outcome.output.resize(frames);
	const std::uint32_t outBase = planeOffset(
		static_cast<std::size_t>(outcome.channels), planeBytes);
	if (!m_sandbox->readMemory(outBase, outcome.output.data(), planeBytes))
	{
		outcome.output.clear();
	}
	outcome.log = m_sandbox->takeLog();
	return outcome;
}

std::string WasmSandboxHost::takeLog()
{
	return m_sandbox == nullptr ? std::string() : m_sandbox->takeLog();
}

} // namespace lmms::wasm
