/*
 * NamModelMonoOutputTest.cpp - final head width must match the mono output.
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

#include "../nam/NamModel.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{

bool writeModel(const std::filesystem::path& path, int headSize)
{
	std::ofstream file(path);
	if (!file)
	{
		return false;
	}
	file << R"({"architecture":"WaveNet","config":{"layers":[{"input_size":1,"condition_size":1,"head_size":)"
	     << headSize
	     << R"(,"head_bias":false,"channels":1,"bottleneck":1,"dilations":[1],"kernel_size":1,"layer1x1":{"active":false}}]},"weights":[)";
	for (int i = 0; i < headSize + 5; ++i)
	{
		if (i > 0)
		{
			file << ',';
		}
		file << '0';
	}
	file << "]}";
	return static_cast<bool>(file);
}

bool expectLoad(const std::filesystem::path& path, int headSize, bool expected)
{
	if (!writeModel(path, headSize))
	{
		std::cerr << "could not write the temporary .nam fixture\n";
		return false;
	}

	std::string error;
	auto model = lmms::nam::NamModel::loadFromFile(path.string(), &error);
	if (static_cast<bool>(model) != expected)
	{
		std::cerr << "final head_size " << headSize << " load result was "
		          << (model ? "accepted" : "rejected") << "; expected "
		          << (expected ? "accepted" : "rejected") << ": " << error << '\n';
		return false;
	}
	if (!expected && error.find("head_size must be 1") == std::string::npos)
	{
		std::cerr << "rejection did not explain the mono head_size requirement: " << error << '\n';
		return false;
	}
	return true;
}

}  // namespace

int main()
{
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto path = std::filesystem::temp_directory_path() /
		("zene-nam-mono-output-" + std::to_string(stamp) + ".nam");
	if (!expectLoad(path, 1, true) || !expectLoad(path, 2, false))
	{
		std::filesystem::remove(path);
		return 1;
	}
	std::filesystem::remove(path);
	return 0;
}
