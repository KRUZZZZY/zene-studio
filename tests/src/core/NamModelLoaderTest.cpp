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
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

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


long long testProcessId()
{
#ifdef _WIN32
	return static_cast<long long>(_getpid());
#else
	return static_cast<long long>(getpid());
#endif
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


std::string modelJson(const ModelShape& shape, const std::string& sampleRateJson = {},
	std::size_t specialWeightIndex = std::numeric_limits<std::size_t>::max(),
	const std::string& specialWeightJson = "0")
{
	std::ostringstream json;
	json << "{\"architecture\":\"WaveNet\"";
	if (!sampleRateJson.empty())
	{
		json << ",\"sample_rate\":" << sampleRateJson;
	}
	json << ",\"config\":{\"layers\":[{"
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
		json << (i == specialWeightIndex ? specialWeightJson : "0");
	}
	json << "]}";
	return json.str();
}


bool rejectsModelJson(const std::filesystem::path& path, const std::string& content,
	const char* label, const char* expectedError = nullptr)
{
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output << content;
		if (!output)
		{
			std::cerr << "could not write model fixture\n";
			return false;
		}
	}

	std::string error;
	const auto model = lmms::nam::NamModel::loadFromFile(path.string(), &error);
	if (model || error.empty() ||
	    (expectedError != nullptr && error.find(expectedError) == std::string::npos))
	{
		std::cerr << "loader accepted or misreported invalid model: " << label
		          << " (" << error << ")\n";
		return false;
	}
	return true;
}


bool rejectsModel(const std::filesystem::path& path, const ModelShape& shape,
	const char* label)
{
	return rejectsModelJson(path, modelJson(shape), label);
}


bool rejectsInvalidSampleRates(const std::filesystem::path& path)
{
	const auto rejectsRate = [&path](const std::string& sampleRate, const char* label,
		const char* expectedError)
	{
		const std::string content = modelJson(ModelShape{}, sampleRate);
		{
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output << content;
			if (!output)
			{
				std::cerr << "could not write model fixture\n";
				return false;
			}
		}

		std::string error;
		const auto model = lmms::nam::NamModel::loadFromFile(path.string(), &error);
		if (model || error.empty() || error.find(expectedError) == std::string::npos)
		{
			std::cerr << "loader accepted or misreported invalid sample_rate: " << label
			          << " (" << error << ")\n";
			return false;
		}
		return true;
	};

	return rejectsRate("0", "zero", "sample_rate") &&
		rejectsRate("-1", "negative", "sample_rate") &&
		rejectsRate("1e400", "overflow to non-finite double", "not valid JSON");
}


bool rejectsOverflowingWeights(const std::filesystem::path& path)
{
	const ModelShape shape;
	const std::size_t headScaleIndex = expectedWeights(shape) - 1;
	const bool rejectsTensorWeight = rejectsModelJson(path,
		modelJson(shape, {}, 0, "1e100"), "tensor weight overflow", "weight");
	const bool rejectsHeadScale = rejectsModelJson(path,
		modelJson(shape, {}, headScaleIndex, "1e100"), "head_scale overflow", "weight");
	return rejectsTensorWeight && rejectsHeadScale;
}


bool loadsModelWithSampleRate(const std::filesystem::path& path,
	const std::string& sampleRate, double expectedSampleRate)
{
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output << modelJson(ModelShape{}, sampleRate);
		if (!output)
		{
			std::cerr << "could not write valid sample-rate fixture\n";
			return false;
		}
	}

	std::string error;
	const auto model = lmms::nam::NamModel::loadFromFile(path.string(), &error);
	if (!model || model->spec().sampleRate != expectedSampleRate)
	{
		std::cerr << "loader refused a valid sample_rate fixture: " << error << '\n';
		return false;
	}
	return true;
}


/// The parse limit: 512 MiB of engine memory divided by the parser's worst
/// measured expansion of 32 bytes of heap per byte of .nam text.
constexpr std::uintmax_t kParseLimitBytes = 512ULL * 1024ULL * 1024ULL / 32ULL;


bool writeFixture(const std::filesystem::path& path, const std::string& content)
{
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output << content;
	}
	if (std::filesystem::file_size(path) != content.size())
	{
		std::cerr << "could not write model fixture\n";
		return false;
	}
	return true;
}


/// The model JSON with an unused string field, sized so the file is exactly
/// `bytes` long. The model itself stays valid, so only the size can refuse it.
std::string paddedModelJson(const ModelShape& shape, std::uintmax_t bytes)
{
	const std::string model = modelJson(shape);
	const std::string prefix = model.substr(0, model.size() - 1) + ",\"padding\":\"";
	const std::string suffix = "\"}";
	return prefix + std::string(bytes - prefix.size() - suffix.size(), 'x') + suffix;
}


bool refusesOversizedFile(const std::filesystem::path& path)
{
	if (!writeFixture(path, paddedModelJson(ModelShape{}, kParseLimitBytes + 1)))
	{
		return false;
	}

	std::string error;
	const auto model = lmms::nam::NamModel::loadFromFile(path.string(), &error);
	if (model || error.find("size limit") == std::string::npos)
	{
		std::cerr << "loader accepted or misreported a model file one byte over the "
		             "parse limit\n";
		return false;
	}
	return true;
}


bool loadsFileAtLimit(const std::filesystem::path& path)
{
	if (!writeFixture(path, paddedModelJson(ModelShape{}, kParseLimitBytes)))
	{
		return false;
	}

	std::string error;
	const auto model = lmms::nam::NamModel::loadFromFile(path.string(), &error);
	if (!model)
	{
		std::cerr << "loader refused a model file exactly at the parse limit: "
		          << error << '\n';
		return false;
	}
	return true;
}


bool validatesSampleRatesAndFileLimits(const std::filesystem::path& path)
{
	return rejectsInvalidSampleRates(path) &&
		loadsModelWithSampleRate(path, "48000", 48000.0) &&
		loadsModelWithSampleRate(path, "null", -1.0) &&
		rejectsOverflowingWeights(path) &&
		refusesOversizedFile(path) && loadsFileAtLimit(path);
}


}  // namespace


int main()
{
	const auto directory = std::filesystem::temp_directory_path() /
		("zene-nam-loader-test-" + std::to_string(testProcessId()));
	std::filesystem::remove_all(directory);
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

	if (!validatesSampleRatesAndFileLimits(fixture))
	{
		std::filesystem::remove_all(directory);
		return 1;
	}

	std::filesystem::remove_all(directory);
	return 0;
}
