/*
 * rt_alloc_probe.cpp - runtime real-time-safety probe for the NamModel process path
 *
 * Counts heap allocations made by the engine while process() runs. It links the
 * plugin's own compiled engine objects and interposes:
 *   - malloc/calloc/realloc/free via ld --wrap (catches Eigen's std::malloc paths)
 *   - global operator new/new[]/delete/delete[] (catches std::vector etc.)
 *
 * Any allocation inside the measured window is a real-time violation. Model load
 * and prewarm happen before the window and are allowed (SPEC-neural-amp.md: load
 * in ctor/worker only).
 *
 * Build (Release objects from the LMMS build tree; run from the repo root):
 *   g++ -O2 -g -std=gnu++20 -I plugins/NeuralAmp \
 *       -I plugins/NeuralAmp/rtneural/modules/Eigen \
 *       -I plugins/NeuralAmp/rtneural/modules/json \
 *       plugins/NeuralAmp/tests/rt_alloc_probe.cpp \
 *       build/plugins/NeuralAmp/CMakeFiles/neuralamp.dir/nam/NamModel.cpp.o \
 *       build/plugins/NeuralAmp/CMakeFiles/neuralamp.dir/nam/NamModelLoader.cpp.o \
 *       -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free \
 *       -o build/rt_alloc_probe
 *
 * Usage: build/rt_alloc_probe plugins/NeuralAmp/models/wavenet_a1_standard.nam [blockSize]
 * Exit code 0 = no allocation in the process path, 1 = allocation detected.
 *
 * Copyright (c) 2026 AI-KOS Team
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "nam/NamModel.h"

#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

extern "C" {
void* __real_malloc(std::size_t);
void* __real_calloc(std::size_t, std::size_t);
void* __real_realloc(void*, std::size_t);
void __real_free(void*);
}

static unsigned long g_malloc = 0, g_calloc = 0, g_realloc = 0, g_free = 0;
static unsigned long g_freeNull = 0, g_freeNonNull = 0;
static unsigned long g_new = 0, g_newArr = 0, g_delete = 0, g_deleteArr = 0;

extern "C" void* __wrap_malloc(std::size_t n) { ++g_malloc; return __real_malloc(n); }
extern "C" void* __wrap_calloc(std::size_t a, std::size_t b) { ++g_calloc; return __real_calloc(a, b); }
extern "C" void* __wrap_realloc(void* p, std::size_t n) { ++g_realloc; return __real_realloc(p, n); }
extern "C" void __wrap_free(void* p)
{
	++g_free;
	if (p) { ++g_freeNonNull; } else { ++g_freeNull; }
	__real_free(p);
}

void* operator new(std::size_t n)
{
	++g_new;
	void* p = __real_malloc(n ? n : 1);
	if (!p) { throw std::bad_alloc(); }
	return p;
}
void* operator new[](std::size_t n)
{
	++g_newArr;
	void* p = __real_malloc(n ? n : 1);
	if (!p) { throw std::bad_alloc(); }
	return p;
}
void operator delete(void* p) noexcept { ++g_delete; __real_free(p); }
void operator delete[](void* p) noexcept { ++g_deleteArr; __real_free(p); }
void operator delete(void* p, std::size_t) noexcept { ++g_delete; __real_free(p); }
void operator delete[](void* p, std::size_t) noexcept { ++g_deleteArr; __real_free(p); }

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::fprintf(stderr, "usage: %s model.nam [blockSize]\n", argv[0]);
		return 2;
	}
	const int block = (argc > 2) ? std::atoi(argv[2]) : 512;

	std::string error;
	std::unique_ptr<lmms::nam::NamModel> model = lmms::nam::NamModel::loadFromFile(argv[1], &error);
	if (!model)
	{
		std::fprintf(stderr, "LOAD FAILED: %s\n", error.c_str());
		return 2;
	}

	std::vector<float> in(static_cast<std::size_t>(block));
	std::vector<float> out(static_cast<std::size_t>(block));
	for (int i = 0; i < block; ++i)
	{
		in[static_cast<std::size_t>(i)] = 0.1f * std::sin(0.01f * static_cast<float>(i));
	}

	// Warm-up: any lazy one-time initialisation (none expected) lands here.
	for (int b = 0; b < 32; ++b) { model->process(in.data(), out.data(), block); }

	const unsigned long m0 = g_malloc, c0 = g_calloc, r0 = g_realloc, f0 = g_free;
	const unsigned long n0 = g_new, na0 = g_newArr, d0 = g_delete, da0 = g_deleteArr;
	const unsigned long fn0 = g_freeNull, fnn0 = g_freeNonNull;

	const int kBlocks = 512;
	for (int b = 0; b < kBlocks; ++b) { model->process(in.data(), out.data(), block); }

	const unsigned long dm = g_malloc - m0, dc = g_calloc - c0, dr = g_realloc - r0;
	const unsigned long df = g_free - f0, dn = g_new - n0, dna = g_newArr - na0;
	const unsigned long dd = g_delete - d0, dda = g_deleteArr - da0;
	const unsigned long dfn = g_freeNull - fn0, dfnn = g_freeNonNull - fnn0;

	std::printf("model: %s  block=%d  blocks measured=%d (%d frames)\n",
		argv[1], block, kBlocks, kBlocks * block);
	std::printf("  malloc=%lu calloc=%lu realloc=%lu\n", dm, dc, dr);
	std::printf("  operator new=%lu new[]=%lu delete=%lu delete[]=%lu\n", dn, dna, dd, dda);
	std::printf("  free=%lu (null=%lu non-null=%lu)\n", df, dfn, dfnn);
	const unsigned long total = dm + dc + dr + dn + dna + dfnn;
	std::printf("RESULT: %s (heap allocations during process(): %lu)\n",
		total == 0 ? "NO ALLOCATION IN PROCESS PATH" : "ALLOCATION DETECTED", total);
	return total == 0 ? 0 : 1;
}
