/*
 * NamModelLoaderTest.cpp - the NAM loader's malformed-dimension refusals
 *
 * The loader is the only place a .nam file's declared dimensions become Eigen
 * matrix sizes, so every bound that stands between a model file and those
 * allocations belongs to it. This test writes one invalid model per bound and
 * requires the loader to refuse it, with an error string, instead of building
 * an engine from it.
 *
 * Qt-free on purpose: the loader is a self-contained engine with no Qt
 * dependency, so its regression needs no engine and no platform plugin.
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
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include "NamModel.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace
{

struct ModelShape
{
	int headSize = 1;
	int channels = 1;
	int bottleneck = 1;
	int kernelSize = 3;
	int dilation = 1;
	std::size_t layerCount = 1;
};


std::size_t nonnegative(int value)
{
	return value > 0 ? static_cast<std::size_t>(value) : 0;
}


std::size_t expectedWeights(const ModelShape& shape)
{
	const std::size_t channels = nonnegative(shape.channels);
	const std::size_t bottleneck = nonnegative(shape.bottleneck);
	const std::size_t headSize = nonnegative(shape.headSize);
	const std::size_t kernelSize = nonnegative(shape.kernelSize);
	const std::size_t layerWeights = kernelSize * channels * bottleneck +
		2 * bottleneck + channels * bottleneck + channels;
	return 1 + channels + shape.layerCount * layerWeights + headSize * bottleneck;
}


std::string modelJson(const ModelShape& shape)
{
	std::ostringstream json;
	json << "{\"architecture\":\"WaveNet\",\"config\":{\"layers\":[{"
	     << "\"input_size\":1,\"condition_size\":1,\"head_size\":" << shape.headSize
	     << ",\"head_bias\":false,\"channels\":" << shape.channels
	     << ",\"bottleneck\":" << shape.bottleneck
	     << ",\"kernel_size\":" << shape.kernelSize << ",\"dilations\":[";
	for (std::size_t i = 0; i < shape.layerCount; ++i)
	{
		if (i != 0)
		{
			json << ',';
		}
		json << shape.dilation;
	}
	json << "]}]},\"weights\":[";
	for (std::size_t i = 0; i < expectedWeights(shape); ++i)
	{
		if (i != 0)
		{
			json << ',';
		}
		json << '0';
	}
	json << "]}";
	return json.str();
}


bool rejectsModel(const std::filesystem::path& path, const ModelShape& shape,
	const char* label)
{
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output << modelJson(shape);
		if (!output)
		{
			std::cerr << "could not write model fixture\n";
			return false;
		}
	}

	std::string error;
	const auto model = lmms::nam::NamModel::loadFromFile(path.string(), &error);
	if (model || error.empty())
	{
		std::cerr << "loader accepted invalid model: " << label << '\n';
		return false;
	}
	return true;
}


}  // namespace


int main()
{
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto directory = std::filesystem::temp_directory_path() /
		("zene-nam-loader-test-" + std::to_string(stamp));
	if (!std::filesystem::create_directory(directory))
	{
		std::cerr << "could not create temporary fixture directory\n";
		return 1;
	}
	const auto fixture = directory / "invalid.nam";

	ModelShape overflow;
	overflow.dilation = std::numeric_limits<int>::max();
	if (!rejectsModel(fixture, overflow, "receptive-field overflow"))
	{
		std::filesystem::remove_all(directory);
		return 1;
	}

	ModelShape zeroHead;
	zeroHead.headSize = 0;
	if (!rejectsModel(fixture, zeroHead, "zero head size"))
	{
		std::filesystem::remove_all(directory);
		return 1;
	}

	ModelShape zeroChannels;
	zeroChannels.channels = 0;
	if (!rejectsModel(fixture, zeroChannels, "zero channel count"))
	{
		std::filesystem::remove_all(directory);
		return 1;
	}

	ModelShape zeroBottleneck;
	zeroBottleneck.bottleneck = 0;
	if (!rejectsModel(fixture, zeroBottleneck, "zero bottleneck"))
	{
		std::filesystem::remove_all(directory);
		return 1;
	}

	ModelShape zeroDilation;
	zeroDilation.dilation = 0;
	if (!rejectsModel(fixture, zeroDilation, "zero dilation"))
	{
		std::filesystem::remove_all(directory);
		return 1;
	}

	ModelShape tooManyLayers;
	tooManyLayers.kernelSize = 1;
	tooManyLayers.layerCount = 257;
	if (!rejectsModel(fixture, tooManyLayers, "too many layers"))
	{
		std::filesystem::remove_all(directory);
		return 1;
	}

	std::filesystem::remove_all(directory);
	return 0;
}