/*
 * NamModelValidation.h - bounded preflight checks for .nam model dimensions
 *
 * A .nam file states its own dimensions, and the loader turns them straight
 * into Eigen matrix sizes. These helpers run BEFORE the first resize(): they
 * bound how many layers a model may declare, count the weights its declared
 * dimensions imply, and cap the memory the engine would occupy, so a malformed
 * file is refused with an error instead of driving an unchecked allocation.
 *
 * Copyright (c) 2026 Zene Studio contributors
 * This file is part of Zene Studio and is distributed under the GPL-2.0-or-later.
 */

#pragma once

#include "nam/NamModel.h"

#include <json.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace lmms::nam::detail
{

inline constexpr std::size_t kMaxModelLayers = 256;
inline constexpr std::size_t kMaxModelBytes = 512U * 1024U * 1024U;

/// json::parse builds a heap tree that can outgrow the text it reads: measured
/// up to 23 bytes of heap per byte of input (empty objects in an array, "{},"),
/// with 32 as the analytic bound (a heap map plus its parent slot, doubled by
/// vector capacity slack). The file is refused before parsing when it is larger
/// than the engine budget divided by this, so parser memory stays inside it.
inline constexpr std::uintmax_t kParserBytesPerInputByte = 32U;
inline constexpr std::uintmax_t kMaxModelFileBytes =
	static_cast<std::uintmax_t>(kMaxModelBytes) / kParserBytesPerInputByte;


inline std::size_t checkedProduct(std::size_t left, std::size_t right)
{
	if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
	{
		throw std::runtime_error("model dimensions overflow allocation size");
	}
	return left * right;
}


inline void addCount(std::size_t& total, std::size_t amount)
{
	if (amount > std::numeric_limits<std::size_t>::max() - total)
	{
		throw std::runtime_error("model dimensions overflow allocation size");
	}
	total += amount;
}


inline std::size_t matrixElements(std::size_t rows, std::size_t columns)
{
	const auto maxIndex = static_cast<std::size_t>(std::numeric_limits<Eigen::Index>::max());
	if (rows > maxIndex || columns > maxIndex)
	{
		throw std::runtime_error("model matrix dimension exceeds Eigen index range");
	}
	const std::size_t elements = checkedProduct(rows, columns);
	if (elements > maxIndex)
	{
		throw std::runtime_error("model matrix size exceeds Eigen index range");
	}
	return elements;
}


inline void addEngineMatrix(std::size_t& elements, std::size_t rows, std::size_t columns)
{
	addCount(elements, matrixElements(rows, columns));
}


inline void addEngineVector(std::size_t& elements, std::size_t size)
{
	if (size > static_cast<std::size_t>(std::numeric_limits<Eigen::Index>::max()))
	{
		throw std::runtime_error("model vector size exceeds Eigen index range");
	}
	addCount(elements, size);
}


inline void addWeightedMatrix(std::size_t& weights, std::size_t& elements,
	std::size_t rows, std::size_t columns)
{
	const std::size_t count = matrixElements(rows, columns);
	addCount(weights, count);
	addCount(elements, count);
}


inline void addWeightedVector(std::size_t& weights, std::size_t& elements, std::size_t size)
{
	addEngineVector(elements, size);
	addCount(weights, size);
}


inline void addWeightedTensor(std::size_t& weights, std::size_t& elements,
	std::size_t rows, std::size_t columns, std::size_t matrices)
{
	const std::size_t matrixSize = matrixElements(rows, columns);
	const std::size_t count = checkedProduct(matrixSize, matrices);
	addCount(weights, count);
	addCount(elements, count);
}


inline void addLayerStorage(const LayerSpec& layer, int conditionSize,
	std::size_t& weights, std::size_t& elements, std::size_t& metadataBytes)
{
	const std::size_t channels = static_cast<std::size_t>(layer.channels);
	const std::size_t bottleneck = static_cast<std::size_t>(layer.bottleneck);
	const std::size_t kernel = static_cast<std::size_t>(layer.kernelSize);
	const std::size_t receptiveField = checkedProduct(kernel - 1,
		static_cast<std::size_t>(layer.dilation));
	std::size_t historyColumns = receptiveField;
	addCount(historyColumns, NamModel::kMaxBlock);

	addWeightedTensor(weights, elements, bottleneck, channels, kernel);
	addWeightedVector(weights, elements, bottleneck);
	addWeightedMatrix(weights, elements, bottleneck,
		static_cast<std::size_t>(conditionSize));
	if (layer.hasOneByOne)
	{
		addWeightedMatrix(weights, elements, channels, bottleneck);
		addWeightedVector(weights, elements, channels);
	}
	addEngineMatrix(elements, channels, historyColumns);
	addEngineMatrix(elements, bottleneck, NamModel::kMaxBlock);
	addEngineMatrix(elements, bottleneck, NamModel::kMaxBlock);
	addEngineMatrix(elements, bottleneck, NamModel::kMaxBlock);
	addEngineMatrix(elements, channels, NamModel::kMaxBlock);
	addCount(metadataBytes, 1024U);
	addCount(metadataBytes, checkedProduct(kernel, sizeof(Eigen::MatrixXf)));
}


inline void addArrayStorage(const LayerArraySpec& array, std::size_t& weights,
	std::size_t& elements, std::size_t& metadataBytes)
{
	const LayerSpec& first = array.layers.front();
	const std::size_t channels = static_cast<std::size_t>(first.channels);
	const std::size_t bottleneck = static_cast<std::size_t>(first.bottleneck);
	const std::size_t inputSize = static_cast<std::size_t>(array.inputSize);
	const std::size_t headSize = static_cast<std::size_t>(array.headSize);

	addWeightedMatrix(weights, elements, channels, inputSize);
	for (const LayerSpec& layer : array.layers)
	{
		addLayerStorage(layer, array.conditionSize, weights, elements, metadataBytes);
	}
	addWeightedMatrix(weights, elements, headSize, bottleneck);
	if (array.headBias)
	{
		addWeightedVector(weights, elements, headSize);
	}
	addEngineMatrix(elements, inputSize, NamModel::kMaxBlock);
	addEngineMatrix(elements, channels, NamModel::kMaxBlock);
	addEngineMatrix(elements, channels, NamModel::kMaxBlock);
	addEngineMatrix(elements, bottleneck, NamModel::kMaxBlock);
	addEngineMatrix(elements, headSize, NamModel::kMaxBlock);
	addCount(metadataBytes, 1024U);
}


inline void validateModelResources(const ModelSpec& spec, std::size_t weightCount,
	std::size_t layerCount)
{
	if (layerCount > kMaxModelLayers)
	{
		throw std::runtime_error("model has too many WaveNet layers");
	}

	std::size_t expectedWeights = 1;
	std::size_t engineElements = 2U * NamModel::kMaxBlock;
	std::size_t metadataBytes = 0;
	for (const LayerArraySpec& array : spec.arrays)
	{
		addArrayStorage(array, expectedWeights, engineElements, metadataBytes);
	}
	if (expectedWeights != weightCount)
	{
		throw std::runtime_error("weight count does not match model dimensions");
	}

	std::size_t totalBytes = checkedProduct(engineElements, sizeof(float));
	addCount(totalBytes, checkedProduct(weightCount, sizeof(float) + sizeof(nlohmann::json)));
	addCount(totalBytes, metadataBytes);
	if (totalBytes > kMaxModelBytes)
	{
		throw std::runtime_error("model exceeds the 512 MiB load limit");
	}
}

}  // namespace lmms::nam::detail