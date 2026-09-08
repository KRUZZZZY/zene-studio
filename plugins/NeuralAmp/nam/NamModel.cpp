/*
 * NamModel.cpp - block-based WaveNet (A1) inference engine for .nam models
 *
 * The DSP graph follows the reference implementation in NeuralAmpModelerCore
 * (MIT, https://github.com/sdatkinson/NeuralAmpModelerCore), file
 * NAM/wavenet/model.cpp: per layer
 *
 *     z = tanh(conv(input) + conv_bias + input_mixin(condition))
 *     head += z
 *     next = input + layer1x1(z) + bias            (residual connection)
 *
 * and per layer array head = previous array's head + sum(z), followed by
 * head_rechannel and the model-wide head_scale. All state is causal, so the
 * model introduces zero latency.
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

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lmms::nam
{

void NamModel::applyTanh(Eigen::MatrixXf& m, int numFrames) noexcept
{
	// Column-major storage: the first numFrames columns are contiguous.
	float* p = m.data();
	const int total = static_cast<int>(m.rows()) * numFrames;
	for (int i = 0; i < total; ++i)
	{
		p[i] = std::tanh(p[i]);
	}
}


void NamModel::reset() noexcept
{
	for (auto& array : m_arrays)
	{
		for (auto& layer : array.layers)
		{
			layer.history.setZero();
		}
		array.head.setZero();
		array.headOut.setZero();
		array.rechannelIn.setZero();
		array.layerInput.setZero();
		array.layerOutput.setZero();
	}
	m_condition.setZero();
	m_lastChunkFrames = 0;
}


void NamModel::prewarm() noexcept
{
	// Reference semantics (NeuralAmpModelerCore WaveNet + DSP::prewarm):
	// reset() zeroes the state, then the model is run over `prewarmSamples`
	// of silence so that every conv history holds the bias-driven steady
	// state before the first real block. The block partitioning does not
	// matter: once the receptive field has been covered the state is a fixed
	// point of the silence recurrence.
	if (!m_ready || m_spec.prewarmSamples <= 0 ||
	    m_prewarmBuffer.size() < static_cast<std::size_t>(kMaxBlock))
	{
		return;
	}

	int remaining = m_spec.prewarmSamples;
	while (remaining > 0)
	{
		const int chunk = std::min(remaining, kMaxBlock);
		// In-place is safe: processChunk() copies the input into the
		// condition matrix before it writes any output, and the output of
		// prewarm is discarded.
		processChunk(m_prewarmBuffer.data(), m_prewarmBuffer.data(), chunk);
		remaining -= chunk;
	}
}


void NamModel::process(const float* input, float* output, int numFrames) noexcept
{
	if (!m_ready || numFrames <= 0) { return; }

	int offset = 0;
	while (offset < numFrames)
	{
		const int chunk = std::min(numFrames - offset, kMaxBlock);
		processChunk(input + offset, output + offset, chunk);
		offset += chunk;
	}
}


void NamModel::processChunk(const float* input, float* output, int numFrames) noexcept
{
	// Global condition signal (the raw input), one row.
	m_condition.row(0).head(numFrames) =
		Eigen::Map<const Eigen::RowVectorXf>(input, numFrames);

	for (std::size_t a = 0; a < m_arrays.size(); ++a)
	{
		ArrayState& array = m_arrays[a];
		ArrayState* prev = (a == 0) ? nullptr : &m_arrays[a - 1];

		// Array input: rechannel of the condition for the first array, the
		// previous array's residual output otherwise. The rechannel maps
		// inputSize -> channels; layer 0 consumes the rechannel output.
		if (prev == nullptr)
		{
			array.rechannelIn.row(0).head(numFrames) =
				m_condition.row(0).head(numFrames);
		}
		else
		{
			array.rechannelIn.leftCols(numFrames) = prev->layerOutput.leftCols(numFrames);
		}
		array.layerInput.leftCols(numFrames).noalias() =
			array.rechannelW * array.rechannelIn.leftCols(numFrames);

		// Head accumulator: starts from the previous array's head output.
		if (prev == nullptr)
		{
			array.head.leftCols(numFrames).setZero();
		}
		else
		{
			array.head.leftCols(numFrames) = prev->headOut.leftCols(numFrames);
		}

		const Eigen::MatrixXf* cur = &array.layerInput;

		for (std::size_t l = 0; l < array.layers.size(); ++l)
		{
			LayerState& layer = array.layers[l];
			const int inC = layer.inChannels;
			
			const int K = layer.kernelSize;
			const int D = layer.dilation;
			const int R = layer.receptiveField;

			// Slide the history window left by the number of frames written
			// by the *previous* processChunk() call and append the new block.
			// The shift amount must be the previous chunk length, not
			// numFrames: prewarm() runs one long chunk before the first short
			// audio block, and shifting by the wrong amount makes the taps
			// read stale samples from the start of the prewarm transient.
			// memmove handles the overlap when the previous block was shorter
			// than R; columns are contiguous in column-major storage.
			if (R > 0 && m_lastChunkFrames > 0)
			{
				std::memmove(layer.history.data(),
					layer.history.data() + static_cast<std::size_t>(m_lastChunkFrames) * inC,
					sizeof(float) * static_cast<std::size_t>(R) * inC);
			}
			layer.history.block(0, R, inC, numFrames) = cur->leftCols(numFrames);

			// Dilated causal convolution: tap k looks back (K-1-k)*D samples.
			// The history window is [previous R samples][current block], newest
			// last, so tap k starts at R - (K-1-k)*D.
			layer.convOut.leftCols(numFrames).setZero();
			for (int k = 0; k < K; ++k)
			{
				const int col = R - (K - 1 - k) * D;
				layer.convOut.leftCols(numFrames).noalias() +=
					layer.convW[k] * layer.history.block(0, col, inC, numFrames);
			}
			layer.convOut.leftCols(numFrames).colwise() += layer.convBias;

			// Conditioning mixin (kernel 1, no bias).
			layer.mixinOut.leftCols(numFrames).noalias() =
				layer.mixinW * m_condition.leftCols(numFrames);

			// z = conv + mixin, then activation.
			layer.z.leftCols(numFrames) =
				layer.convOut.leftCols(numFrames) + layer.mixinOut.leftCols(numFrames);
			applyTanh(layer.z, numFrames);

			// Skip connection into the array head.
			array.head.leftCols(numFrames) += layer.z.leftCols(numFrames);

			// Residual connection to the next layer.
			if (layer.hasOneByOne)
			{
				layer.next.leftCols(numFrames).noalias() =
					layer.oneByOneW * layer.z.leftCols(numFrames);
				layer.next.leftCols(numFrames).colwise() += layer.oneByOneBias;
				layer.next.leftCols(numFrames) += cur->leftCols(numFrames);
			}
			else
			{
				layer.next.leftCols(numFrames) = cur->leftCols(numFrames);
			}

			cur = &layer.next;
		}

		array.layerOutput.leftCols(numFrames) = cur->leftCols(numFrames);

		// Per-array head rechannel (1x1 + optional bias).
		array.headOut.leftCols(numFrames).noalias() =
			array.headRechannelW * array.head.leftCols(numFrames);
		if (array.headRechannelHasBias)
		{
			array.headOut.leftCols(numFrames).colwise() += array.headRechannelBias;
		}
	}

	// Output = head_scale * last array's head output (single channel).
	const float scale = m_spec.headScale;
	const Eigen::MatrixXf& finalHead = m_arrays.back().headOut;
	for (int i = 0; i < numFrames; ++i)
	{
		output[i] = scale * finalHead(0, i);
	}

	// Remember this chunk's length: the next call shifts its history windows
	// by exactly this many frames.
	m_lastChunkFrames = numFrames;
}

}  // namespace lmms::nam
