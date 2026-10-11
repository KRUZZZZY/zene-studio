/*
 * nam_profile.cpp - stage-level CPU profile of NamModel::process()
 *
 * Copyright (c) 2026 AI-KOS Team
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
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Build:  cmake --build <build> -j4 --target nam_profile
 * Run:    ./nam_profile model.nam [blocks] [blockSize] [warmup]
 *
 * The engine is compiled with -DNAM_PROFILE_LAYERS for this target only, which
 * turns the NAM_PROF_* hooks in NamModel.cpp into CLOCK_THREAD_CPUTIME_ID
 * accumulators per processing stage. The measured loop is otherwise the same
 * code path as nam_harness (same signal, same block size, same warmup).
 */

#include "nam/NamModel.h"
#include "nam/NamProfile.h"

#include <cmath>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{

constexpr double kTwoPi = 6.283185307179586476925286766559;

/// Same deterministic signal as nam_harness.cpp.
double testSignal(int64_t sample, double sampleRate)
{
	const double t = static_cast<double>(sample) / sampleRate;
	const double pluckPeriod = 0.5;
	const double phase = std::fmod(t, pluckPeriod);
	const double envelope = std::exp(-phase * 6.0);
	const double f0 = 110.0;
	double value = 0.0;
	for (int h = 1; h <= 6; ++h)
	{
		value += std::sin(kTwoPi * f0 * h * t) / static_cast<double>(h);
	}
	value *= envelope * 0.2 / 2.449;
	const uint32_t r = static_cast<uint32_t>(sample * 1664525u + 1013904223u);
	const double noise = (static_cast<double>(r) / 4294967295.0 - 0.5) * 2.0;
	return value + noise * 0.001;
}

double threadCpuUs()
{
	timespec t{};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return static_cast<double>(t.tv_sec) * 1e6 + static_cast<double>(t.tv_nsec) / 1e3;
}

const char* slotName(int slot)
{
	switch (slot)
	{
	case lmms::nam::NAM_PROF_CONDITION: return "condition copy";
	case lmms::nam::NAM_PROF_RECHANNEL: return "rechannel (copy+GEMM)";
	case lmms::nam::NAM_PROF_HEAD_INIT: return "head init";
	case lmms::nam::NAM_PROF_HISTORY: return "history shift+append";
	case lmms::nam::NAM_PROF_CONV: return "conv GEMMs (K taps)";
	case lmms::nam::NAM_PROF_CONV_BIAS: return "conv bias";
	case lmms::nam::NAM_PROF_MIXIN: return "mixin GEMM";
	case lmms::nam::NAM_PROF_Z_ADD: return "z = conv + mixin";
	case lmms::nam::NAM_PROF_TANH: return "tanh";
	case lmms::nam::NAM_PROF_HEAD_ADD: return "head += z";
	case lmms::nam::NAM_PROF_ONEBYONE: return "1x1 + bias + residual";
	case lmms::nam::NAM_PROF_LAYER_OUT: return "array output copy";
	case lmms::nam::NAM_PROF_HEAD_RECHANNEL: return "head rechannel";
	case lmms::nam::NAM_PROF_OUTPUT: return "output scale";
	default: return "?";
	}
}

struct ProfileArguments
{
	int blocks = 2000;
	int blockSize = 512;
	int warmup = 20;
	double sampleRate = 48000.0;
};

enum class ArgumentError
{
	None,
	Blocks,
	BlockSize,
	Warmup,
	SampleRate
};

bool parseInteger(const char* text, int& value)
{
	char* end = nullptr;
	errno = 0;
	const long parsed = std::strtol(text, &end, 10);
	if (end == text || *end != '\0' || errno == ERANGE ||
	    parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max())
	{
		return false;
	}
	value = static_cast<int>(parsed);
	return true;
}

bool parsePositiveBlocks(const char* text, int& value)
{
	return parseInteger(text, value) && value > 0;
}

bool parseBlockSize(const char* text, int& value)
{
	return parseInteger(text, value) && value >= 1 && value <= lmms::nam::NamModel::kMaxBlock;
}

bool parseWarmup(const char* text, int& value)
{
	return parseInteger(text, value) && value >= 0;
}

bool parseSampleRate(const char* text, double& value)
{
	char* end = nullptr;
	errno = 0;
	const double parsed = std::strtod(text, &end);
	if (end == text || *end != '\0' || errno == ERANGE || !std::isfinite(parsed) || parsed <= 0.0)
	{
		return false;
	}
	value = parsed;
	return true;
}

ArgumentError parseProfileArguments(int argc, char** argv, ProfileArguments& arguments)
{
	if (argc > 2 && !parsePositiveBlocks(argv[2], arguments.blocks))
	{
		return ArgumentError::Blocks;
	}
	if (argc > 3 && !parseBlockSize(argv[3], arguments.blockSize))
	{
		return ArgumentError::BlockSize;
	}
	if (argc > 4 && !parseWarmup(argv[4], arguments.warmup))
	{
		return ArgumentError::Warmup;
	}
	if (argc > 5 && !parseSampleRate(argv[5], arguments.sampleRate))
	{
		return ArgumentError::SampleRate;
	}
	return ArgumentError::None;
}

void printArgumentError(ArgumentError error)
{
	switch (error)
	{
	case ArgumentError::Blocks:
		std::fprintf(stderr, "blocks must be a positive integer\n");
		break;
	case ArgumentError::BlockSize:
		std::fprintf(stderr, "blockSize must be in [1, %d]\n", lmms::nam::NamModel::kMaxBlock);
		break;
	case ArgumentError::Warmup:
		std::fprintf(stderr, "warmup must be a non-negative integer\n");
		break;
	case ArgumentError::SampleRate:
		std::fprintf(stderr, "sampleRate must be a positive finite number\n");
		break;
	case ArgumentError::None:
		break;
	}
}

}  // namespace

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::fprintf(stderr,
			"usage: %s <model.nam> [blocks=2000] [blockSize=512] [warmup=20] [sampleRate=48000]\n",
			argv[0]);
		return 2;
	}
	const std::string modelPath = argv[1];
	ProfileArguments arguments;
	const ArgumentError argumentError = parseProfileArguments(argc, argv, arguments);
	if (argumentError != ArgumentError::None)
	{
		printArgumentError(argumentError);
		return 2;
	}

	std::string error;
	std::unique_ptr<lmms::nam::NamModel> model = lmms::nam::NamModel::loadFromFile(modelPath, &error);
	if (!model)
	{
		std::fprintf(stderr, "load failed: %s\n", error.c_str());
		return 1;
	}
	const lmms::nam::ModelSpec& spec = model->spec();

	std::printf("model:      %s\n", modelPath.c_str());
	std::printf("arch:       %s v%s\n", spec.architecture.c_str(), spec.version.c_str());
	std::printf("arrays:     %zu  weights: %zu  RF: %d  prewarm: %d\n",
		spec.arrays.size(), spec.weightCount, spec.receptiveField, spec.prewarmSamples);
	std::printf("blocks:     %d x %d frames (warmup %d) @ %.0f Hz\n", arguments.blocks,
		arguments.blockSize, arguments.warmup, arguments.sampleRate);

	std::vector<float> in(arguments.blockSize);
	std::vector<float> out(arguments.blockSize);
	for (int i = 0; i < arguments.blockSize; ++i)
	{
		in[i] = static_cast<float>(testSignal(i, arguments.sampleRate));
	}

	model->reset();
	model->prewarm();
	for (int b = 0; b < arguments.warmup; ++b)
	{
		model->process(in.data(), out.data(), arguments.blockSize);
	}

	std::memset(lmms::nam::g_namProfUs, 0, sizeof(lmms::nam::g_namProfUs));
	const double cpu0 = threadCpuUs();
	for (int b = 0; b < arguments.blocks; ++b)
	{
		model->process(in.data(), out.data(), arguments.blockSize);
	}
	const double cpu1 = threadCpuUs();

	double slotSum = 0.0;
	for (int s = 0; s < lmms::nam::NAM_PROF_SLOT_COUNT; ++s)
	{
		slotSum += lmms::nam::g_namProfUs[s];
	}

	const double totalUs = cpu1 - cpu0;
	const double perBlock = totalUs / static_cast<double>(arguments.blocks);
	std::printf("\n%-26s %12s %12s %8s\n", "stage", "us total", "us/block", "% slot");
	for (int s = 0; s < lmms::nam::NAM_PROF_SLOT_COUNT; ++s)
	{
		const double v = lmms::nam::g_namProfUs[s];
		std::printf("%-26s %12.1f %12.3f %7.1f%%\n", slotName(s), v,
			v / static_cast<double>(arguments.blocks), slotSum > 0.0 ? 100.0 * v / slotSum : 0.0);
	}
	std::printf("%-26s %12.1f %12.3f %7.1f%%\n", "SUM of slots", slotSum,
		slotSum / static_cast<double>(arguments.blocks), 100.0);
	std::printf("%-26s %12.1f %12.3f\n", "measured process total", totalUs, perBlock);
	std::printf("%-26s %12.1f %12.3f\n", "unaccounted", totalUs - slotSum,
		(totalUs - slotSum) / static_cast<double>(arguments.blocks));
	std::printf("\ncpu/block: %.1f us  (%.3f%% of one 48 kHz core at %d-frame blocks)\n",
		perBlock, 100.0 * perBlock / (1e6 * arguments.blockSize / arguments.sampleRate),
		arguments.blockSize);
	return 0;
}
