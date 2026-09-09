/*
 * wat2wasm.cpp - assemble WebAssembly text modules with the runtime's parser
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

#include <wasmtime.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace
{

void printError(wasmtime_error_t* error)
{
	wasm_name_t message;
	wasmtime_error_message(error, &message);
	std::fprintf(stderr, "wasm-wat2wasm: %.*s\n",
		static_cast<int>(message.size), message.data);
	wasm_name_delete(&message);
	wasmtime_error_delete(error);
}

} // namespace

int main(int argc, char** argv)
{
	if (argc != 3)
	{
		std::fprintf(stderr, "usage: %s input.wat output.wasm\n", argv[0]);
		return 2;
	}
	std::ifstream input(argv[1], std::ios::binary);
	if (!input)
	{
		std::fprintf(stderr, "wasm-wat2wasm: cannot open '%s'\n", argv[1]);
		return 1;
	}
	const std::string wat((std::istreambuf_iterator<char>(input)),
		std::istreambuf_iterator<char>());

	wasm_byte_vec_t bytes;
	wasmtime_error_t* error = wasmtime_wat2wasm(wat.data(), wat.size(), &bytes);
	if (error != nullptr)
	{
		printError(error);
		return 1;
	}

	std::ofstream output(argv[2], std::ios::binary);
	output.write(bytes.data, static_cast<std::streamsize>(bytes.size));
	const bool ok = output.good();
	std::printf("wasm-wat2wasm: %s -> %s (%zu bytes)\n", argv[1], argv[2],
		bytes.size);
	wasm_byte_vec_delete(&bytes);
	return ok ? 0 : 1;
}
