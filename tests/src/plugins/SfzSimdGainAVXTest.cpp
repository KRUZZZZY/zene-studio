/*
 * SfzSimdGainAVXTest.cpp - AVX gain advances its per-sample gain pointer
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA.
 */

#include "HelpersAVX.h"

#include <array>
#include <cstddef>
#include <cstdio>

namespace
{

bool matchesExpected(const float* gain, const float* input, float* output, unsigned size)
{
	gainAVX(gain, input, output, size);

	for (unsigned i = 0; i < size; ++i)
	{
		const auto expected = gain[i] * input[i];
		if (output[i] != expected)
		{
			std::fprintf(stderr, "gainAVX mismatch at sample %u: expected %g, got %g\n",
				i, static_cast<double>(expected), static_cast<double>(output[i]));
			return false;
		}
	}

	return true;
}

} // namespace

int main()
{
	__builtin_cpu_init();
	if (!__builtin_cpu_supports("avx"))
	{
		return 77;
	}

	alignas(32) std::array<float, 40> gain{};
	alignas(32) std::array<float, 40> input{};
	alignas(32) std::array<float, 40> output{};
	for (std::size_t i = 0; i < gain.size(); ++i)
	{
		gain[i] = static_cast<float>(i + 1) * 0.25f;
		input[i] = static_cast<float>(i + 2) * 0.5f;
	}

	// Three aligned AVX blocks followed by one scalar sample.
	if (!matchesExpected(gain.data(), input.data(), output.data(), 25))
	{
		return 1;
	}

	// A short buffer exercises the scalar-only path.
	output.fill(-1.0f);
	if (!matchesExpected(gain.data(), input.data(), output.data(), 7))
	{
		return 1;
	}

	// Same-offset pointers make the alignment prologue advance all three inputs
	// together before the aligned vector loop and scalar tail.
	output.fill(-1.0f);
	if (!matchesExpected(gain.data() + 1, input.data() + 1, output.data() + 1, 33))
	{
		return 1;
	}

	return 0;
}
