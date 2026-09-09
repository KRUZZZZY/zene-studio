/*
 * NamModelLoader.cpp - .nam JSON parsing and engine construction
 *
 * Supported subset: architecture "WaveNet", legacy A1 layer arrays
 * (non-gated, Tanh activation, optional layer1x1, optional head bias,
 * no FiLM, no condition_dsp, no post-stack head). Unsupported features are
 * rejected with an explicit error rather than silently mis-loaded.
 *
 * Weight order follows NeuralAmpModelerCore (MIT) NAM/conv1d.cpp
 * Conv1D::set_weights_ / NAM/dsp.cpp Conv1x1::set_weights_:
 *   conv:   for out, for in, for kernel tap k
 *   conv bias, then input mixin (no bias), then layer1x1 + bias,
 *   then per-array head_rechannel (+ bias), and finally head_scale as the
 *   last element of the weights array.
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
 */

#include "NamModel.h"

#include <json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include "NamTanh.h"

namespace lmms::nam
{

namespace
{

using nlohmann::json;

struct ParseError : public std::runtime_error
{
	explicit ParseError(const std::string& what) :
		std::runtime_error(what)
	{
	}
};

/// Sequential reader over the flat "weights" array. Throws when exhausted.
struct WeightCursor
{
	const std::vector<float>& weights;
	std::size_t index = 0;

	explicit WeightCursor(const std::vector<float>& w) :
		weights(w)
	{
	}

	float next()
	{
		if (index >= weights.size())
		{
			throw ParseError("weights array exhausted (model is truncated or uses an "
			                 "unsupported layer type)");
		}
		return weights[index++];
	}

	/// Row-major fill of a matrix: for row, for column.
	void fillMatrix(Eigen::MatrixXf& m)
	{
		for (Eigen::Index r = 0; r < m.rows(); ++r)
		{
			for (Eigen::Index c = 0; c < m.cols(); ++c)
			{
				m(r, c) = next();
			}
		}
	}

	void fillVector(Eigen::VectorXf& v)
	{
		for (Eigen::Index r = 0; r < v.size(); ++r)
		{
			v(r) = next();
		}
	}
};

}  // namespace


std::unique_ptr<NamModel> NamModel::loadFromFile(const std::string& path, std::string* error)
{
	try
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
		{
			throw ParseError("cannot open file: " + path);
		}

		json root;
		try
		{
			root = json::parse(in);
		}
		catch (const json::exception& e)
		{
			throw ParseError(std::string("not valid JSON: ") + e.what());
		}

		const std::string architecture = root.value("architecture", std::string());
		if (architecture != "WaveNet")
		{
			throw ParseError("unsupported architecture '" + architecture +
			                 "' (this build supports WaveNet A1 only)");
		}

		ModelSpec spec;
		spec.architecture = architecture;
		spec.version = root.value("version", std::string());
		if (root.contains("sample_rate") && !root["sample_rate"].is_null())
		{
			spec.sampleRate = root["sample_rate"].get<double>();
		}
		if (root.contains("metadata") && root["metadata"].is_object())
		{
			spec.name = root["metadata"].value("name", std::string());
		}

		if (!root.contains("config") || !root["config"].is_object())
		{
			throw ParseError("missing config object");
		}
		const json& config = root["config"];

		if (config.contains("condition_dsp") && !config["condition_dsp"].is_null())
		{
			throw ParseError("models with a condition_dsp are not supported");
		}
		if (config.contains("head") && !config["head"].is_null())
		{
			throw ParseError("models with a post-stack 'head' are not supported (A1 only)");
		}
		if (!config.contains("layers") || !config["layers"].is_array() ||
		    config["layers"].empty())
		{
			throw ParseError("config has no layers");
		}

		for (const json& arrayConfig : config["layers"])
		{
			if (!arrayConfig.is_object())
			{
				throw ParseError("layer array entry is not an object");
			}

			LayerArraySpec array;
			array.inputSize = arrayConfig.at("input_size").get<int>();
			array.conditionSize = arrayConfig.at("condition_size").get<int>();
			array.headSize = arrayConfig.at("head_size").get<int>();
			array.headBias = arrayConfig.at("head_bias").get<bool>();
			const int channels = arrayConfig.at("channels").get<int>();
			const int bottleneck = arrayConfig.value("bottleneck", channels);

			if (array.conditionSize != 1)
			{
				throw ParseError("multi-channel condition signals are not supported");
			}
			if (arrayConfig.value("gated", false))
			{
				throw ParseError("gated WaveNet layers are not supported");
			}
			if (arrayConfig.value("groups_input", 1) != 1 ||
			    arrayConfig.value("groups_input_mixin", 1) != 1)
			{
				throw ParseError("grouped input convolutions are not supported");
			}

			bool layer1x1Active = true;
			if (arrayConfig.contains("layer1x1") && arrayConfig["layer1x1"].is_object())
			{
				layer1x1Active = arrayConfig["layer1x1"].value("active", true);
				if (arrayConfig["layer1x1"].value("groups", 1) != 1)
				{
					throw ParseError("grouped layer1x1 convolutions are not supported");
				}
			}

			if (!arrayConfig.contains("dilations") || !arrayConfig["dilations"].is_array())
			{
				throw ParseError("layer array has no dilations");
			}
			const json& dilations = arrayConfig["dilations"];
			const std::size_t numLayers = dilations.size();
			if (numLayers == 0)
			{
				throw ParseError("layer array has zero layers");
			}

			std::vector<int> kernelSizes(numLayers, 3);
			if (arrayConfig.contains("kernel_size"))
			{
				std::fill(kernelSizes.begin(), kernelSizes.end(),
					arrayConfig["kernel_size"].get<int>());
			}
			if (arrayConfig.contains("kernel_sizes"))
			{
				const json& ks = arrayConfig["kernel_sizes"];
				if (!ks.is_array() || ks.size() != numLayers)
				{
					throw ParseError("kernel_sizes size does not match dilations");
				}
				for (std::size_t i = 0; i < numLayers; ++i)
				{
					kernelSizes[i] = ks[i].get<int>();
				}
			}

			std::vector<std::string> activations(numLayers, "Tanh");
			if (arrayConfig.contains("activation"))
			{
				std::fill(activations.begin(), activations.end(),
					arrayConfig["activation"].get<std::string>());
			}
			if (arrayConfig.contains("activations"))
			{
				const json& acts = arrayConfig["activations"];
				if (!acts.is_array() || acts.size() != numLayers)
				{
					throw ParseError("activations size does not match dilations");
				}
				for (std::size_t i = 0; i < numLayers; ++i)
				{
					activations[i] = acts[i].get<std::string>();
				}
			}

			for (std::size_t i = 0; i < numLayers; ++i)
			{
				if (activations[i] != "Tanh")
				{
					throw ParseError("activation '" + activations[i] +
					                 "' is not supported (Tanh only)");
				}
				if (kernelSizes[i] < 1)
				{
					throw ParseError("invalid kernel size");
				}
				LayerSpec layer;
				layer.channels = channels;
				layer.bottleneck = bottleneck;
				layer.kernelSize = kernelSizes[i];
				layer.dilation = dilations[i].get<int>();
				layer.hasOneByOne = layer1x1Active;
				array.layers.push_back(layer);
			}

			spec.arrays.push_back(std::move(array));
		}

		if (spec.arrays.front().inputSize != 1)
		{
			throw ParseError("model input_size must be 1 (mono) for this effect");
		}

		// A1 arrays are chained: array a consumes array a-1's residual output
		// (inputSize == previous channels) and seeds its head accumulator from
		// array a-1's head output (bottleneck == previous head_size).
		for (std::size_t a = 1; a < spec.arrays.size(); ++a)
		{
			const LayerArraySpec& prev = spec.arrays[a - 1];
			const LayerArraySpec& cur = spec.arrays[a];
			if (cur.inputSize != prev.layers.front().channels ||
			    cur.layers.front().bottleneck != prev.headSize)
			{
				throw ParseError("unsupported A1 array chaining at array " +
				                 std::to_string(a) + " (input_size " +
				                 std::to_string(cur.inputSize) + " vs previous channels " +
				                 std::to_string(prev.layers.front().channels) + ")");
			}
		}

		if (!root.contains("weights") || !root["weights"].is_array())
		{
			throw ParseError("missing weights array");
		}
		std::vector<float> weights;
		weights.reserve(root["weights"].size());
		for (const json& w : root["weights"])
		{
			weights.push_back(w.get<float>());
		}

		// Build the engine (all allocation happens here, never in process()).
		auto model = std::make_unique<NamModel>();
		WeightCursor cursor(weights);
		int totalReceptiveField = 0;

		for (const LayerArraySpec& arraySpec : spec.arrays)
		{
			ArrayState array;
			array.inputSize = arraySpec.inputSize;
			array.headSize = arraySpec.headSize;

			const int channels = arraySpec.layers.front().channels;
			const int bottleneck = arraySpec.layers.front().bottleneck;
			array.headBottleneck = bottleneck;

			array.rechannelW.resize(channels, arraySpec.inputSize);
			cursor.fillMatrix(array.rechannelW);

			array.layers.resize(arraySpec.layers.size());
			for (std::size_t li = 0; li < arraySpec.layers.size(); ++li)
			{
				const LayerSpec& layerSpec = arraySpec.layers[li];
				LayerState& layer = array.layers[li];
				layer.inChannels = channels;
				layer.outChannels = layerSpec.bottleneck;  // non-gated
				layer.kernelSize = layerSpec.kernelSize;
				layer.dilation = layerSpec.dilation;
				layer.receptiveField = (layerSpec.kernelSize - 1) * layerSpec.dilation;
				totalReceptiveField += layer.receptiveField;

				layer.convW.resize(layerSpec.kernelSize);
				for (auto& matrix : layer.convW)
				{
					matrix.resize(layer.outChannels, layer.inChannels);
				}
				for (int out = 0; out < layer.outChannels; ++out)
				{
					for (int in = 0; in < layer.inChannels; ++in)
					{
						for (int k = 0; k < layer.kernelSize; ++k)
						{
							layer.convW[k](out, in) = cursor.next();
						}
					}
				}

				layer.convBias.resize(layer.outChannels);
				cursor.fillVector(layer.convBias);

				layer.mixinW.resize(layer.outChannels, arraySpec.conditionSize);
				cursor.fillMatrix(layer.mixinW);

				layer.hasOneByOne = layerSpec.hasOneByOne;
				if (layer.hasOneByOne)
				{
					layer.oneByOneW.resize(channels, bottleneck);
					cursor.fillMatrix(layer.oneByOneW);
					layer.oneByOneBias.resize(channels);
					cursor.fillVector(layer.oneByOneBias);
				}

				layer.history.resize(layer.inChannels, layer.receptiveField + kMaxBlock);
				layer.history.setZero();
				layer.convOut.resize(layer.outChannels, kMaxBlock);
				layer.mixinOut.resize(layer.outChannels, kMaxBlock);
				layer.z.resize(bottleneck, kMaxBlock);
				layer.next.resize(channels, kMaxBlock);
			}

			array.headRechannelW.resize(arraySpec.headSize, bottleneck);
			cursor.fillMatrix(array.headRechannelW);
			array.headRechannelHasBias = arraySpec.headBias;
			if (array.headRechannelHasBias)
			{
				array.headRechannelBias.resize(arraySpec.headSize);
				cursor.fillVector(array.headRechannelBias);
			}

			// rechannelIn receives the condition (array 0) or the previous
			// array's residual output; layerInput is the rechannel output.
			array.rechannelIn.resize(arraySpec.inputSize, kMaxBlock);
			array.layerInput.resize(channels, kMaxBlock);
			array.layerOutput.resize(channels, kMaxBlock);
			array.head.resize(bottleneck, kMaxBlock);
			array.headOut.resize(arraySpec.headSize, kMaxBlock);
			model->m_arrays.push_back(std::move(array));
		}

		// head_scale is stored as the last element of the weights array.
		spec.headScale = cursor.next();

		if (cursor.index != weights.size())
		{
			throw ParseError("weight count mismatch: consumed " +
			                 std::to_string(cursor.index) + " of " +
			                 std::to_string(weights.size()) +
			                 " (model uses an unsupported layer type)");
		}

		spec.weightCount = weights.size();
		spec.receptiveField = totalReceptiveField;
		// Prewarm length follows the reference WaveNet constructor:
		//   1 + sum(layer-array receptive fields)   (no condition_dsp, no head)
		// where an array's receptive field is sum over layers of D*(K-1) plus
		// the head rechannel's D*(K-1) (kernel size 1 here, so 0).
		spec.prewarmSamples = 1 + totalReceptiveField;

		model->m_spec = spec;
		model->m_condition.resize(1, kMaxBlock);
		model->m_condition.setZero();
		model->m_prewarmBuffer.assign(kMaxBlock, 0.0f);
		model->reset();
		model->m_ready = true;
		// Resolve the libmvec tanh kernels here, on the loader thread, so the
		// audio thread never triggers a dlopen().
		tanhdetail::warmUp();
		model->prewarm();
		return model;
	}
	catch (const std::exception& e)
	{
		if (error != nullptr)
		{
			*error = e.what();
		}
		return nullptr;
	}
}

}  // namespace lmms::nam
