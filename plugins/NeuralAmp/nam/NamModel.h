/*
 * NamModel.h - block-based WaveNet (A1) inference engine for .nam models
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

#ifndef LMMS_NAM_MODEL_H
#define LMMS_NAM_MODEL_H

#include <Eigen/Dense>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace lmms::nam
{

/// Static description of a single WaveNet layer (A1 subset: non-gated, kernel 3,
/// optional layer1x1, no FiLM / gating / head1x1).
struct LayerSpec
{
	int channels = 0;    ///< residual-stream width (conv output rows)
	int bottleneck = 0;  ///< rows of the activated signal feeding 1x1 + head
	int kernelSize = 3;
	int dilation = 1;
	bool hasOneByOne = true;
};

/// Static description of one layer array.
struct LayerArraySpec
{
	int inputSize = 1;
	int conditionSize = 1;
	int headSize = 1;
	int headKernelSize = 1;
	int headDilation = 1;
	bool headBias = false;
	std::vector<LayerSpec> layers;
};

/// Static description of a whole model.
struct ModelSpec
{
	std::string architecture;  ///< e.g. "WaveNet"
	std::string version;       ///< .nam file version string
	std::string name;          ///< metadata name, if any
	double sampleRate = -1.0;  ///< expected sample rate, -1 if the file does not say
	float headScale = 1.0f;
	std::vector<LayerArraySpec> arrays;
	std::size_t weightCount = 0;
	int receptiveField = 0;  ///< total lookback (state depth); output latency is 0
	int prewarmSamples = 0;  ///< silence samples needed to reach steady state
};

/// Block-based WaveNet A1 inference engine.
///
/// Threading contract:
///  * loadFromFile() allocates and is called from the GUI/worker thread only.
///  * process()/reset() are real-time safe: no allocation, no locks, no I/O,
///    no exceptions. All buffers are sized at load time for kMaxBlock frames.
class NamModel
{
public:
	/// Largest block process() handles in one call; longer buffers are chunked.
	static constexpr int kMaxBlock = 8192;

	/// Parse a .nam file and build the engine. NOT real-time safe.
	/// Returns nullptr and fills `error` on failure.
	static std::unique_ptr<NamModel> loadFromFile(const std::string& path, std::string* error);

	NamModel() = default;
	~NamModel() = default;
	NamModel(const NamModel&) = delete;
	NamModel& operator=(const NamModel&) = delete;

	/// Process numFrames mono samples: out[i] = model(in[i]). Real-time safe.
	void process(const float* input, float* output, int numFrames) noexcept;

	/// Drop all recurrent state (conv histories and head accumulators). Real-time safe.
	void reset() noexcept;

	/// Prime the recurrent state with the model's steady-state response to
	/// silence, matching the reference implementation's Reset() semantics
	/// (NeuralAmpModelerCore calls prewarm() from DSP::Reset). Without this the
	/// first block after load would start from an all-zero history, which the
	/// reference does not. Runs prewarmSamples of silence; called from
	/// loadFromFile() (worker thread) only. No allocation.
	void prewarm() noexcept;

	const ModelSpec& spec() const noexcept { return m_spec; }
	bool ready() const noexcept { return m_ready; }

private:
	struct LayerState
	{
		int inChannels = 0;
		int outChannels = 0;
		int kernelSize = 0;
		int dilation = 0;
		int receptiveField = 0;

		std::vector<Eigen::MatrixXf> convW;  ///< kernelSize matrices [outChannels x inChannels]
		Eigen::VectorXf convBias;            ///< [outChannels]
		Eigen::MatrixXf mixinW;              ///< [outChannels x conditionSize]
		Eigen::MatrixXf oneByOneW;           ///< [channels x bottleneck]
		Eigen::VectorXf oneByOneBias;        ///< [channels]
		bool hasOneByOne = false;

		/// [inChannels x (receptiveField + kMaxBlock)]; columns are frames.
		Eigen::MatrixXf history;
		Eigen::MatrixXf convOut;   ///< [outChannels x kMaxBlock]
		Eigen::MatrixXf mixinOut;  ///< [outChannels x kMaxBlock]
		Eigen::MatrixXf z;         ///< [bottleneck x kMaxBlock]
		Eigen::MatrixXf next;      ///< [channels x kMaxBlock]
	};

	struct ArrayState
	{
		int inputSize = 1;
		int headSize = 1;
		int headBottleneck = 1;

		Eigen::MatrixXf rechannelW;  ///< [channels x inputSize]
		Eigen::MatrixXf headRechannelW;  ///< [headSize x headBottleneck]
		Eigen::VectorXf headRechannelBias;  ///< [headSize] (only if headBias)
		bool headRechannelHasBias = false;

		std::vector<LayerState> layers;

		Eigen::MatrixXf rechannelIn;  ///< [inputSize x kMaxBlock] (condition / previous array output)
		Eigen::MatrixXf layerInput;   ///< [channels x kMaxBlock] (rechannel output = layer 0 input)
		Eigen::MatrixXf layerOutput;  ///< [channels x kMaxBlock]
		Eigen::MatrixXf head;         ///< [headBottleneck x kMaxBlock]
		Eigen::MatrixXf headOut;      ///< [headSize x kMaxBlock]
	};

	void processChunk(const float* input, float* output, int numFrames) noexcept;
	static void applyTanh(Eigen::MatrixXf& m, int numFrames) noexcept;

	ModelSpec m_spec;
	std::vector<ArrayState> m_arrays;
	Eigen::MatrixXf m_condition;  ///< [1 x kMaxBlock]
	std::vector<float> m_prewarmBuffer;  ///< kMaxBlock zeros; prewarm() only
	bool m_ready = false;

#ifdef NAM_DEBUG_SEAM
public:
	// Test-only accessors (compile with -DNAM_DEBUG_SEAM); not used in production.
	const std::vector<ArrayState>& debugArrays() const noexcept { return m_arrays; }
	const Eigen::MatrixXf& debugCondition() const noexcept { return m_condition; }
#endif
};

}  // namespace lmms::nam

#endif  // LMMS_NAM_MODEL_H
