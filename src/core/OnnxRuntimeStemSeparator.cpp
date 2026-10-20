/*
 * OnnxRuntimeStemSeparator.cpp - in-process ONNX Runtime backend (optional)
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

#include "StemSeparation/OnnxRuntimeStemSeparator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <QFileInfo>
#include <QThread>

#include <onnxruntime_cxx_api.h>

#include "SampleBuffer.h"
#include "SampleFrame.h"
#include "StemSeparation/StemModelStore.h"

namespace lmms
{

namespace
{

constexpr int Channels = 2;

// Look up an output by name, falling back to its fixed contract position.
int outputIndexFor(const std::vector<std::string>& names, const char* wanted, int fallback)
{
	for (size_t i = 0; i < names.size(); ++i)
	{
		if (names[i] == wanted)
		{
			return static_cast<int>(i);
		}
	}
	return fallback;
}

/*! Where each stem is read from. Four named outputs: an output per stem, names
 *  winning over the fixed contract order. Owner decision 14: the public HTDemucs
 *  exports ship ONE output, the stems stacked on axis 1 as [1, S, 2, T] in
 *  contract order (S >= 4; -1 is a dynamic dimension) - every stem is then read
 *  from output 0 at \a stemStride floats apart. tools/stem_split_cli.py accepts
 *  the same two layouts. False, with \a error, for anything else. */
bool isStackedOutput(Ort::Session& session, std::size_t outputCount)
{
	if (outputCount != 1) { return false; }
	const auto shape = session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
	return shape.size() == 4 && (shape[1] < 0 || shape[1] >= NumStems)
		&& (shape[2] < 0 || shape[2] == Channels);
}

bool resolveStemOutputs(Ort::Session& session, const std::vector<std::string>& outputNames, int segment,
	std::array<int, NumStems>* stemOutput, std::ptrdiff_t* stemStride, QString* error)
{
	const bool stacked = isStackedOutput(session, outputNames.size());
	if (outputNames.size() < NumStems && !stacked)
	{
		*error = QStringLiteral("Model has %1 outputs, expected at least %2 (or one stacked "
			"[1, %2, 2, T] output)").arg(outputNames.size()).arg(NumStems);
		return false;
	}
	for (int s = 0; s < NumStems; ++s)
	{
		(*stemOutput)[static_cast<size_t>(s)] = stacked ? 0 : outputIndexFor(
			outputNames, stemName(static_cast<Stem>(s)), s);
	}
	*stemStride = stacked ? static_cast<std::ptrdiff_t>(Channels) * segment : 0;
	return true;
}

/*! False, with \a error, when an output the stems are read from is smaller than what the loop
 *  below reads: two channels of \a segment frames, at \a stemStride floats per stem in the
 *  stacked layout. A dynamic dimension is only known once the model has run. */
bool outputsHoldSegment(const std::vector<Ort::Value>& outputs, const std::array<int, NumStems>& stemOutput,
	std::ptrdiff_t stemStride, int segment, void (*postRunHook)(), QString* error)
{
	for (int s = 0; s < NumStems; ++s)
	{
		const auto index = static_cast<std::size_t>(stemOutput[static_cast<std::size_t>(s)]);
		const auto needed = static_cast<std::size_t>(s * stemStride + static_cast<std::ptrdiff_t>(Channels) * segment);
		if (postRunHook != nullptr) { postRunHook(); }
		if (index >= outputs.size() || outputs[index].GetTensorTypeAndShapeInfo().GetElementCount() < needed)
		{
			*error = QStringLiteral("Model output %1 is smaller than a %2-frame stereo segment")
				.arg(index).arg(segment);
			return false;
		}
	}
	return true;
}

bool initializeSession(Ort::Env& env, const QString& modelPath, Ort::SessionOptions& options,
	int segment, std::unique_ptr<Ort::Session>* session, std::string* inputName,
	std::vector<std::string>* outputNames, std::array<int, NumStems>* stemOutput,
	std::ptrdiff_t* stemStride, QString* error)
{
	session->reset(new Ort::Session(env, modelPath.toStdString().c_str(), options));
	Ort::AllocatorWithDefaultOptions allocator;
	const auto inputNameAllocated = (*session)->GetInputNameAllocated(0, allocator);
	*inputName = inputNameAllocated.get();
	for (size_t i = 0; i < (*session)->GetOutputCount(); ++i)
	{
		outputNames->emplace_back((*session)->GetOutputNameAllocated(i, allocator).get());
	}
	return resolveStemOutputs(**session, *outputNames, segment, stemOutput, stemStride, error);
}

bool runSessionSegment(Ort::Session& session, const std::string& inputName,
	const std::vector<const char*>& outputNames, Ort::Value* tensor,
	std::vector<Ort::Value>* outputs, const std::array<int, NumStems>& stemOutput,
	std::ptrdiff_t stemStride, int segment, void (*postRunHook)(), QString* error)
{
	const char* inputNames[] = { inputName.c_str() };
	*outputs = session.Run(Ort::RunOptions{ nullptr }, inputNames, tensor, 1,
		outputNames.data(), outputNames.size());
	return outputsHoldSegment(*outputs, stemOutput, stemStride, segment, postRunHook, error);
}

} // namespace




OnnxRuntimeStemSeparator::OnnxRuntimeStemSeparator(QString modelPath) :
	m_modelPath(modelPath.isEmpty() ? StemModelStore::defaultModelPath() : std::move(modelPath))
{
}




bool OnnxRuntimeStemSeparator::segmentFits(int frames, int segment, QString* error)
{
	// separate() indexes the padded input and both stereo halves with int. Its largest index is
	// below frames + segment / 2 + 2 * segment (padded length plus one segment), so that sum,
	// taken in 64 bits, must fit int; the chunk count's paddedLength + hop - 1 is below it too.
	const std::int64_t largestIndexBound = static_cast<std::int64_t>(frames)
		+ segment / 2 + 2 * static_cast<std::int64_t>(segment);
	if (largestIndexBound <= std::numeric_limits<int>::max())
	{
		return true;
	}
	*error = QStringLiteral("A %1-frame segment is too long for a %2-frame input").arg(segment).arg(frames);
	return false;
}

int OnnxRuntimeStemSeparator::segmentFor(int frames, int segmentFrames, QString* error)
{
	const int segment = segmentFrames > 1 ? segmentFrames : HTDemucsSegmentFrames;
	return segmentFits(frames, segment, error) ? segment : 0;
}




QString OnnxRuntimeStemSeparator::runtimeVersion()
{
	return QString::fromStdString(Ort::GetVersionString());
}




QString OnnxRuntimeStemSeparator::backendName() const
{
	return QStringLiteral("onnxruntime-cpp %1").arg(runtimeVersion());
}




StemSeparator::Status OnnxRuntimeStemSeparator::separate(const SampleBuffer& mix,
	int sampleRate,
	int segmentFrames,
	const ProgressFn& progress,
	StemSet& out,
	QString& error)
{
	try
	{
		if (!StemModelStore::isModelPresent(m_modelPath, &error))
		{
			return Status::Failed;
		}
		if (sampleRate != StemModelSampleRate)
		{
			error = QStringLiteral("Stem separation needs %1 Hz input, got %2 Hz")
				.arg(StemModelSampleRate).arg(sampleRate);
			return Status::Failed;
		}
		const int frames = static_cast<int>(mix.size());
		if (frames <= 0)
		{
			error = QStringLiteral("Input is empty");
			return Status::Failed;
		}
		// A one-frame segment would make the hop below 0 (and the chunk count a division by it).
		const int segment = segmentFor(frames, segmentFrames, &error);
		if (segment == 0)
		{
			return Status::Failed;
		}

		static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "zene-stem-split");
		Ort::SessionOptions options;
		options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
		options.SetIntraOpNumThreads(std::max(1, QThread::idealThreadCount() - 1));

		std::unique_ptr<Ort::Session> session;
		std::string inputNameStr;
		std::vector<std::string> outputNames;
		std::array<int, NumStems> stemOutput{};
		std::ptrdiff_t stemStride = 0;
		if (!initializeSession(env, m_modelPath, options, segment, &session, &inputNameStr,
			&outputNames, &stemOutput, &stemStride, &error))
		{
			return Status::Failed;
		}
		std::vector<const char*> outputNamesRaw;
		for (const auto& name : outputNames)
		{
			outputNamesRaw.push_back(name.c_str());
		}

		const int hop = segment / 2;
		const int padLeft = segment / 2;
		const int paddedLength = frames + padLeft + segment;
		const int chunkCount = (paddedLength + hop - 1) / hop;

		std::vector<float> window(segment);
		for (int i = 0; i < segment; ++i)
		{
			window[static_cast<size_t>(i)] = 0.5f
				- 0.5f * std::cos(2.0f * static_cast<float>(M_PI)
					* (static_cast<float>(i) + 0.5f) / static_cast<float>(segment));
		}

		// Per stem, per channel accumulation of windowed model output.
		std::vector<std::array<std::vector<float>, Channels>> acc(
			NumStems, { std::vector<float>(frames, 0.0f), std::vector<float>(frames, 0.0f) });
		std::vector<float> windowSum(frames, 0.0f);

		const auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
		std::vector<float> inputTensor(static_cast<size_t>(Channels) * segment);
		const SampleFrame* mixFrames = mix.data();

		for (int chunk = 0; chunk < chunkCount; ++chunk)
		{
			if (progress && !progress(static_cast<float>(chunk) / static_cast<float>(chunkCount)))
			{
				return Status::Cancelled;
			}
			const int start = chunk * hop;
			for (int i = 0; i < segment; ++i)
			{
				const int src = start + i - padLeft;
				const SampleFrame frame = (src >= 0 && src < frames) ? mixFrames[static_cast<size_t>(src)] : SampleFrame();
				inputTensor[static_cast<size_t>(i)] = frame.left();
				inputTensor[static_cast<size_t>(segment + i)] = frame.right();
			}

			const std::array<int64_t, 3> shape{ 1, Channels, segment };
			auto tensor = Ort::Value::CreateTensor<float>(memoryInfo,
				inputTensor.data(), inputTensor.size(), shape.data(), shape.size());

			std::vector<Ort::Value> outputs;
			if (!runSessionSegment(*session, inputNameStr, outputNamesRaw, &tensor,
				&outputs, stemOutput, stemStride, segment, m_postRunTestHook, &error)) { return Status::Failed; }

			for (int s = 0; s < NumStems; ++s)
			{
				const float* data = outputs[static_cast<size_t>(stemOutput[static_cast<size_t>(s)])]
					.GetTensorData<float>() + static_cast<std::ptrdiff_t>(s) * stemStride;
				for (int i = 0; i < segment; ++i)
				{
					const int dst = start + i - padLeft;
					if (dst < 0 || dst >= frames)
					{
						continue;
					}
					const float w = window[static_cast<size_t>(i)];
					acc[static_cast<size_t>(s)][0][static_cast<size_t>(dst)]
						+= data[i] * w;
					acc[static_cast<size_t>(s)][1][static_cast<size_t>(dst)]
						+= data[segment + i] * w;
					if (s == 0)
					{
						windowSum[static_cast<size_t>(dst)] += w;
					}
				}
			}
		}

		for (int s = 0; s < NumStems; ++s)
		{
			std::vector<SampleFrame> stemFrames(frames);
			for (int i = 0; i < frames; ++i)
			{
				const float norm = windowSum[static_cast<size_t>(i)] > 1e-8f
					? 1.0f / windowSum[static_cast<size_t>(i)] : 0.0f;
				stemFrames[static_cast<size_t>(i)] = SampleFrame(
					acc[static_cast<size_t>(s)][0][static_cast<size_t>(i)] * norm,
					acc[static_cast<size_t>(s)][1][static_cast<size_t>(i)] * norm);
			}
			out[static_cast<size_t>(s)] = std::make_shared<const SampleBuffer>(
				std::move(stemFrames), sampleRate);
		}
		if (progress)
		{
			progress(1.0f);
		}
		return Status::Success;
	}
	catch (const Ort::Exception& exception)
	{
		error = QString::fromUtf8(exception.what());
		return Status::Failed;
	}
	catch (const std::exception& exception)
	{
		error = QString::fromUtf8(exception.what());
		return Status::Failed;
	}
}

} // namespace lmms
