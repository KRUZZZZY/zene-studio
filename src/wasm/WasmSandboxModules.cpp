/*
 * WasmSandboxModules.cpp - WAT assembly and module loading for the sandbox
 *
 * Copyright (c) 2026 Zene Studio contributors
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 */

#include "WasmSandboxInternals.h"

#include <fstream>
#include <iterator>
#include <new>

namespace lmms::wasm
{

bool WasmSandbox::assembleWat(const std::string& wat, std::vector<std::uint8_t>& out,
	std::string& error)
{
	wasm_byte_vec_t bytes;
	wasmtime_error_t* err = wasmtime_wat2wasm(wat.data(), wat.size(), &bytes);
	if (err != nullptr)
	{
		error = detail::takeError(err);
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
	// The store limit bounds linear memory, not the module image: wasmtime
	// compiles these bytes in host memory. Refuse an oversized image before
	// touching the loaded module, so a bad load cannot destroy a good one.
	if (size > abi::maxModuleBytes)
	{
		error = "module is " + std::to_string(size) + " bytes, over the " +
			std::to_string(abi::maxModuleBytes) + "-byte module size limit";
		return false;
	}

	m_impl->resetModule();

	wasmtime_error_t* err = wasmtime_module_new(m_impl->engine, bytes, size,
		&m_impl->module);
	if (err != nullptr)
	{
		error = "module compile failed: " + detail::takeError(err);
		return false;
	}

	// Instantiation runs the module's start function, and fuel consumption is
	// enabled in the engine. Prime the store with the configured budget first:
	// without it the start function traps out of fuel and a valid module is
	// rejected while loading.
	m_impl->primeFuelBudget();

	wasm_trap_t* trap = nullptr;
	err = wasmtime_linker_instantiate(m_impl->linker, m_impl->context,
		m_impl->module, &m_impl->instance, &trap);
	if (err != nullptr)
	{
		error = "instantiation failed: " + detail::takeError(err);
		return false;
	}
	if (trap != nullptr)
	{
		int code = -1;
		error = "instantiation trapped: " + detail::takeTrap(trap, code);
		return false;
	}
	m_impl->hasInstance = true;

	if (!m_impl->readExports(&error)) { return false; }
	m_impl->clampTuning();
	return true;
}

bool WasmSandbox::readModuleFile(const std::string& path,
	std::vector<std::uint8_t>& out, std::string& error)
{
	out.clear();
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file)
	{
		error = "cannot open module '" + path + "'";
		return false;
	}

	const std::streamoff length = file.tellg();
	if (length < 0)
	{
		error = "cannot determine the size of module '" + path + "'";
		return false;
	}
	const auto size = static_cast<std::uintmax_t>(length);
	if (size > abi::maxModuleBytes)
	{
		error = "module '" + path + "' is " + std::to_string(size) +
			" bytes, over the " + std::to_string(abi::maxModuleBytes) +
			"-byte module size limit";
		return false;
	}
	if (size == 0)
	{
		error = "module '" + path + "' is empty";
		return false;
	}

	try
	{
		out.resize(static_cast<std::size_t>(size));
		file.seekg(0, std::ios::beg);
		file.read(reinterpret_cast<char*>(out.data()),
			static_cast<std::streamsize>(out.size()));
		if (!file || file.gcount() != static_cast<std::streamsize>(out.size()))
		{
			out.clear();
			error = "cannot read module '" + path + "'";
			return false;
		}
	}
	catch (const std::bad_alloc&)
	{
		out.clear();
		error = "not enough memory to read module '" + path + "'";
		return false;
	}
	return true;
}

bool WasmSandbox::loadModuleFile(const std::string& path, std::string& error)
{
	std::vector<std::uint8_t> bytes;
	if (!readModuleFile(path, bytes, error))
	{
		return false;
	}
	return loadModuleBytes(bytes.data(), bytes.size(), error);
}

} // namespace lmms::wasm
