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
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace lmms::nam
{
// Defined in NamModelEigenWarningProbe.cpp: the build fails when this target loses
// the vendored-Eigen -Werror suppression that the linux-arm64 CI job needs.
bool eigenDeprecatedEnumArithmeticProbe() noexcept;
}

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


class TemporaryFixtureDirectory
{
public:
	bool create()
	{
#ifdef _WIN32
		const char* temporaryDirectory = std::getenv("TEMP");
		const std::string base = temporaryDirectory != nullptr ? temporaryDirectory : ".";
		for (int attempt = 0; attempt < 1000; ++attempt)
		{
			m_path = base + "/zene-nam-loader-test-" + std::to_string(testProcessId()) +
				"-" + std::to_string(attempt);
			if (_mkdir(m_path.c_str()) == 0)
			{
				m_created = true;
				return true;
			}
			if (errno != EEXIST)
			{
				return false;
			}
		}
		return false;
#else
		const char* temporaryDirectory = std::getenv("TMPDIR");
		const std::string base = temporaryDirectory != nullptr ? temporaryDirectory : "/tmp";
		m_path = base + "/zene-nam-loader-test-" + std::to_string(testProcessId()) + "-XXXXXX";
		if (mkdtemp(&m_path[0]) == nullptr)
		{
			return false;
		}
		m_created = true;
		return true;
#endif
	}

	~TemporaryFixtureDirectory()
	{
		if (m_created)
		{
			const std::string fixture = fixturePath();
#ifdef _WIN32
			_unlink(fixture.c_str());
			_rmdir(m_path.c_str());
#else
			unlink(fixture.c_str());
			rmdir(m_path.c_str());
#endif
		}
	}

	std::string fixturePath() const
	{
		return m_path + "/invalid.nam";
	}

private:
	std::string m_path;
	bool m_created = false;
};


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


bool rejectsModelJson(const std::string& path, const std::string& content,
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
	const auto model = lmms::nam::NamModel::loadFromFile(path, &error);
	if (model || error.empty() ||
	    (expectedError != nullptr && error.find(expectedError) == std::string::npos))
	{
		std::cerr << "loader accepted or misreported invalid model: " << label
		          << " (" << error << ")\n";
		return false;
	}
	return true;
}


bool rejectsModel(const std::string& path, const ModelShape& shape,
	const char* label)
{
	return rejectsModelJson(path, modelJson(shape), label);
}


bool rejectsInvalidSampleRates(const std::string& path)
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
		const auto model = lmms::nam::NamModel::loadFromFile(path, &error);
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


bool rejectsOverflowingWeights(const std::string& path)
{
	const ModelShape shape;
	const std::size_t headScaleIndex = expectedWeights(shape) - 1;
	const bool rejectsTensorWeight = rejectsModelJson(path,
		modelJson(shape, {}, 0, "1e100"), "tensor weight overflow", "weight");
	const bool rejectsHeadScale = rejectsModelJson(path,
		modelJson(shape, {}, headScaleIndex, "1e100"), "head_scale overflow", "weight");
	return rejectsTensorWeight && rejectsHeadScale;
}


bool loadsModelWithSampleRate(const std::string& path,
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
	const auto model = lmms::nam::NamModel::loadFromFile(path, &error);
	if (!model || model->spec().sampleRate != expectedSampleRate)
	{
		std::cerr << "loader refused a valid sample_rate fixture: " << error << '\n';
		return false;
	}
	return true;
}


/// The parse limit: 512 MiB of engine memory divided by the parser's worst
/// measured expansion of 32 bytes of heap per byte of .nam text.
constexpr std::uint64_t kParseLimitBytes = 512ULL * 1024ULL * 1024ULL / 32ULL;


bool writeFixture(const std::string& path, const std::string& content)
{
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output << content;
	}
	bool hasExpectedSize = false;
#ifdef _WIN32
	struct _stat64 fileInfo;
	if (_stat64(path.c_str(), &fileInfo) == 0 && fileInfo.st_size >= 0)
#else
	struct stat fileInfo;
	if (stat(path.c_str(), &fileInfo) == 0 && fileInfo.st_size >= 0)
#endif
	{
		hasExpectedSize = static_cast<std::uint64_t>(fileInfo.st_size) == content.size();
	}
	if (!hasExpectedSize)
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


bool refusesOversizedFile(const std::string& path)
{
	if (!writeFixture(path, paddedModelJson(ModelShape{}, kParseLimitBytes + 1)))
	{
		return false;
	}

	std::string error;
	const auto model = lmms::nam::NamModel::loadFromFile(path, &error);
	if (model || error.find("size limit") == std::string::npos)
	{
		std::cerr << "loader accepted or misreported a model file one byte over the "
		             "parse limit\n";
		return false;
	}
	return true;
}


bool loadsFileAtLimit(const std::string& path)
{
	if (!writeFixture(path, paddedModelJson(ModelShape{}, kParseLimitBytes)))
	{
		return false;
	}

	std::string error;
	const auto model = lmms::nam::NamModel::loadFromFile(path, &error);
	if (!model)
	{
		std::cerr << "loader refused a model file exactly at the parse limit: "
		          << error << '\n';
		return false;
	}
	return true;
}


bool validatesSampleRatesAndFileLimits(const std::string& path)
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
	if (!lmms::nam::eigenDeprecatedEnumArithmeticProbe())
	{
		std::cerr << "vendored-Eigen enum arithmetic probe failed\n";
		return 1;
	}

	TemporaryFixtureDirectory directory;
	if (!directory.create())
	{
		std::cerr << "could not create temporary fixture directory\n";
		return 1;
	}
	const std::string fixture = directory.fixturePath();

	ModelShape overflow;
	overflow.dilation = std::numeric_limits<int>::max();
	if (!rejectsModel(fixture, overflow, "receptive-field overflow"))
	{
		return 1;
	}

	ModelShape zeroHead;
	zeroHead.headSize = 0;
	if (!rejectsModel(fixture, zeroHead, "zero head size"))
	{
		return 1;
	}

	ModelShape zeroChannels;
	zeroChannels.channels = 0;
	if (!rejectsModel(fixture, zeroChannels, "zero channel count"))
	{
		return 1;
	}

	ModelShape zeroBottleneck;
	zeroBottleneck.bottleneck = 0;
	if (!rejectsModel(fixture, zeroBottleneck, "zero bottleneck"))
	{
		return 1;
	}

	ModelShape zeroDilation;
	zeroDilation.dilation = 0;
	if (!rejectsModel(fixture, zeroDilation, "zero dilation"))
	{
		return 1;
	}

	ModelShape tooManyLayers;
	tooManyLayers.kernelSize = 1;
	tooManyLayers.layerCount = 257;
	if (!rejectsModel(fixture, tooManyLayers, "too many layers"))
	{
		return 1;
	}

	if (!validatesSampleRatesAndFileLimits(fixture))
	{
		return 1;
	}

	return 0;
}
