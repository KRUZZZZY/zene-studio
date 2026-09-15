/*
 * WasmOfflineRender.cpp - the deterministic offline render of a WASM module,
 *                         and the tolerance its repeatability is judged against
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "WasmOfflineRender.h"

#include "SampleFrame.h"
#include "WasmAbi.h"
#include "WasmSandbox.h"
#include "WasmWorker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace lmms::wasm
{

namespace
{

/*! Parameter 0 of a render. A module's parameters start at 0 in a fresh
 *  worker, which makes whatever reads host_get_param(0) render silence - and a
 *  silent render is deterministic for reasons that have nothing to do with the
 *  pool. Every render here therefore drives parameter 0 with this value, on BOTH
 *  paths, so the comparison is made on a real signal.
 */
constexpr float kRenderParam0 = 0.5f;

//! One block of the stimulus, as the worker path carries it: interleaved
//! stereo, both channels the same value, so a mono and a stereo module see the
//! same plane 0.
void fillStimulus(SampleFrame* block, std::uint32_t frames, const std::vector<float>& input,
	std::uint32_t offset)
{
	for (std::uint32_t frame = 0; frame < frames; ++frame)
	{
		const std::size_t source = static_cast<std::size_t>(offset) + frame;
		const float value = source < input.size() ? input[source] : 0.0f;
		block[frame][0] = value;
		block[frame][1] = value;
	}
}

/*! The control path: direct calls on the caller's thread, the same ones the
 *  control surface's `wasm.process` makes (WasmSandboxHost). No pool, no
 *  threads, no queues - which is what makes it the floor.
 */
auto renderInline(const std::string& modulePath, std::uint32_t frames, std::uint32_t blockFrames,
	float sampleRate, const std::vector<float>& input) -> OfflineRender
{
	OfflineRender render;
	render.path = "inline";

	WasmSandbox sandbox;
	std::string error;
	if (!sandbox.loadModuleFile(modulePath, error))
	{
		render.error = error.empty() ? "the module could not be loaded" : error;
		return render;
	}
	if (!sandbox.hasProcess())
	{
		render.error = "the module exports no process()";
		return render;
	}
	render.channels = std::clamp(sandbox.declaredChannels(), 1, static_cast<int>(abi::maxChannels));
	render.latency = sandbox.declaredLatency();
	render.output.assign(frames, 0.0f);
	sandbox.setParam(0, kRenderParam0);

	std::vector<SampleFrame> block(blockFrames);
	for (std::uint32_t offset = 0; offset < frames; offset += blockFrames)
	{
		const std::uint32_t count = std::min(blockFrames, frames - offset);
		fillStimulus(block.data(), count, input, offset);

		const std::uint32_t planeBytes = count * static_cast<std::uint32_t>(sizeof(float));
		std::uint8_t* memory = sandbox.memoryData();
		const std::size_t needed = static_cast<std::size_t>(render.channels) * 2 * planeBytes;
		if (memory == nullptr || sandbox.memorySize() < needed)
		{
			render.error = "the module's linear memory is smaller than one planar block";
			return render;
		}

		// Deinterleave the stimulus into the module's input planes.
		for (int channel = 0; channel < render.channels; ++channel)
		{
			float* plane = reinterpret_cast<float*>(
				memory + static_cast<std::uint32_t>(channel) * planeBytes);
			for (std::uint32_t frame = 0; frame < count; ++frame)
			{
				plane[frame] = block[frame][static_cast<std::size_t>(channel)];
			}
		}

		const std::uint32_t outBase =
			static_cast<std::uint32_t>(render.channels) * planeBytes;
		CallResult result;
		for (int channel = 0; channel < render.channels; ++channel)
		{
			result = sandbox.callProcess(static_cast<std::uint32_t>(channel) * planeBytes,
				outBase + static_cast<std::uint32_t>(channel) * planeBytes, count, sampleRate);
			if (!result.ok()) { break; }
		}
		++render.blocks;
		if (!result.ok())
		{
			++render.trappedBlocks;
			render.error = result.message;
			return render;
		}

		// Plane 0 out.
		const float* plane = reinterpret_cast<const float*>(memory + outBase);
		for (std::uint32_t frame = 0; frame < count; ++frame)
		{
			render.output[offset + frame] = plane[frame];
		}
	}
	render.ok = true;
	return render;
}

/*! The subject: the same block sequence through the shared pool, on a fresh
 *  worker, one block in flight at a time.
 */
auto renderThroughPool(const std::string& modulePath, std::uint32_t frames,
	std::uint32_t blockFrames, float sampleRate, const std::vector<float>& input) -> OfflineRender
{
	OfflineRender render;
	render.path = "pool";

	WasmWorker worker;
	std::string error;
	if (!worker.start(modulePath, error))
	{
		render.error = error.empty() ? "the worker could not start the module" : error;
		return render;
	}
	render.channels = std::clamp(worker.declaredChannels(), 1, static_cast<int>(abi::maxChannels));
	render.latency = worker.declaredLatency();
	render.output.assign(frames, 0.0f);
	worker.setParam(0, kRenderParam0);
	worker.setTransportState(abi::transportStopped);

	std::vector<SampleFrame> submitted(blockFrames);
	std::vector<SampleFrame> collected(blockFrames);
	for (std::uint32_t offset = 0; offset < frames; offset += blockFrames)
	{
		const std::uint32_t count = std::min(blockFrames, frames - offset);
		fillStimulus(submitted.data(), count, input, offset);
		if (!worker.submit(submitted.data(), count, sampleRate))
		{
			++render.droppedBlocks;
			render.error = "the worker refused a block (no free slot or a full queue)";
			worker.stop();
			return render;
		}
		// Offline ordering: the block is collected before the next is
		// submitted, so the module's state advances in block order and the
		// render cannot reorder or lose a block.
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
		bool got = false;
		while (!got && std::chrono::steady_clock::now() < deadline)
		{
			got = worker.collect(collected.data(), count);
			if (!got) { std::this_thread::sleep_for(std::chrono::microseconds(50)); }
		}
		if (!got)
		{
			++render.droppedBlocks;
			render.error = "a block was submitted and never came back (20 s)";
			worker.stop();
			return render;
		}
		for (std::uint32_t frame = 0; frame < count; ++frame)
		{
			render.output[offset + frame] = collected[frame][0];
		}
		++render.blocks;
	}
	render.trappedBlocks = worker.trappedBlocks();
	worker.stop();
	render.ok = true;
	return render;
}

} // namespace

auto compareRenders(const std::vector<float>& a, const std::vector<float>& b) -> RenderDifference
{
	RenderDifference difference;
	const std::size_t shorter = std::min(a.size(), b.size());
	difference.frames = shorter;
	double sumOfSquares = 0.0;
	for (std::size_t i = 0; i < shorter; ++i)
	{
		const double delta = static_cast<double>(a[i]) - static_cast<double>(b[i]);
		if (delta != 0.0)
		{
			++difference.differingFrames;
			sumOfSquares += delta * delta;
			const double magnitude = std::fabs(delta);
			if (magnitude > difference.maxAbsDifference)
			{
				difference.maxAbsDifference = magnitude;
				difference.worstFrame = static_cast<std::int64_t>(i);
			}
		}
	}
	// A length difference is a difference: the shorter render is missing frames
	// the other one has.
	difference.differingFrames += a.size() > b.size() ? a.size() - b.size() : b.size() - a.size();
	if (shorter > 0) { difference.rmsDifference = std::sqrt(sumOfSquares / static_cast<double>(shorter)); }
	return difference;
}

auto renderOffline(const std::string& modulePath, std::uint32_t frames, std::uint32_t blockFrames,
	std::uint32_t repeats, float sampleRate, const std::vector<float>& input)
	-> OfflineRenderOutcome
{
	OfflineRenderOutcome outcome;
	outcome.frames = frames;
	outcome.blockFrames = blockFrames;
	outcome.repeats = repeats;
	outcome.sampleRate = sampleRate;

	if (modulePath.empty() || frames == 0 || blockFrames == 0 || blockFrames > WasmWorker::maxBlockFrames)
	{
		outcome.error = "frames and block_frames must be non-zero and block_frames must fit "
						"WasmWorker::maxBlockFrames";
		return outcome;
	}
	outcome.repeats = std::clamp<std::uint32_t>(repeats, 2, 8);

	// The control pair first: the floor is what THIS build does run to run on
	// the path with no concurrency in it.
	for (std::uint32_t i = 0; i < 2; ++i)
	{
		outcome.control.push_back(renderInline(modulePath, frames, blockFrames, sampleRate, input));
		if (!outcome.control.back().ok)
		{
			outcome.error = "the control render failed: " + outcome.control.back().error;
			return outcome;
		}
	}
	if (outcome.control[0].output.size() != outcome.control[1].output.size())
	{
		outcome.error = "the two control renders have different lengths";
		return outcome;
	}
	outcome.tolerance.floor = compareRenders(outcome.control[0].output, outcome.control[1].output);
	outcome.tolerance.controlPairs = 1;
	outcome.tolerance.source = "same-build run-to-run difference of two inline (no-pool) renders of "
							   "the same input, measured in this call";

	for (std::uint32_t i = 0; i < outcome.repeats; ++i)
	{
		outcome.runs.push_back(renderThroughPool(modulePath, frames, blockFrames, sampleRate, input));
		if (!outcome.runs.back().ok)
		{
			outcome.error = "render " + std::to_string(i + 1) + " failed: " + outcome.runs.back().error;
			return outcome;
		}
	}

	// The subject: the worst pair among the pooled runs, and every pooled run
	// against the control.
	RenderDifference worst;
	bool haveWorst = false;
	bool haveAgainstControl = false;
	for (std::size_t i = 0; i < outcome.runs.size(); ++i)
	{
		for (std::size_t j = i + 1; j < outcome.runs.size(); ++j)
		{
			const RenderDifference pair =
				compareRenders(outcome.runs[i].output, outcome.runs[j].output);
			// The first pair is recorded whatever it says, so `frames` always
			// reports what was compared - a pair of bit-identical renders is a
			// measurement of `frames` frames, not of nothing.
			if (!haveWorst || pair.maxAbsDifference > worst.maxAbsDifference ||
				pair.differingFrames > worst.differingFrames)
			{
				worst = pair;
				haveWorst = true;
			}
		}
		const RenderDifference againstControl =
			compareRenders(outcome.runs[i].output, outcome.control[0].output);
		if (!haveAgainstControl ||
			againstControl.maxAbsDifference > outcome.subjectAgainstControl.maxAbsDifference ||
			againstControl.differingFrames > outcome.subjectAgainstControl.differingFrames)
		{
			outcome.subjectAgainstControl = againstControl;
			haveAgainstControl = true;
		}
	}
	outcome.subject = worst;

	// The verdict: the subject is within the floor measured above. Both numbers
	// are measurements of this build in this call; neither is a constant.
	outcome.deterministic = outcome.tolerance.floor.covers(outcome.subject);

	// The comparator's own self test: it must be able to say "different", or a
	// verdict of "same" would mean nothing.
	if (!outcome.runs.empty() && !outcome.runs[0].output.empty())
	{
		std::vector<float> perturbed = outcome.runs[0].output;
		const std::size_t position = perturbed.size() / 2;
		const double delta = std::max(1e-6, std::fabs(perturbed[position]) * 0.5);
		perturbed[position] = static_cast<float>(perturbed[position] + delta);
		outcome.comparatorPerturbation = delta;
		outcome.comparatorDetectsADifference =
			!compareRenders(perturbed, outcome.runs[0].output).equal();
	}

	outcome.ok = true;
	outcome.channels = outcome.runs.empty() ? outcome.control[0].channels : outcome.runs[0].channels;
	outcome.latency = outcome.runs.empty() ? outcome.control[0].latency : outcome.runs[0].latency;
	return outcome;
}

} // namespace lmms::wasm
