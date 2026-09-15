/*
 * WasmOfflineRender.h - the deterministic offline render of a WASM module, and
 *                       the tolerance its repeatability is judged against
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
 *
 * WHY THE TOLERANCE IS MEASURED AND NOT ASSUMED. docs/RENDER-DETERMINISM.md is
 * this project's recorded contract on render reproducibility, and it is not
 * "bit-identical": a build whose synthesised wavetables come out of
 * FFTW_MEASURE, a plug-in that seeds noise from the clock, and a project whose
 * processing amplifies a last-bit arithmetic difference all defeat byte
 * identity, and the contract's own verdict says two of the nine bundled
 * projects still differ run to run. A determinism test that compares one run to
 * one hash therefore proves nothing about this build - it would either pass
 * vacuously or fail for a reason that is not the code under test.
 *
 * So the verdict here is a MEASURED comparison: this call renders the same
 * input twice on the SAME BUILD, on the CONTROL path (direct calls on the
 * caller's thread, no pool), and takes the difference between those two runs as
 * the run-to-run FLOOR. The subject - the same input rendered twice through the
 * shared pool - is then compared against that floor. Nothing is compared to a
 * constant, and no single value decides: both sides are differences measured in
 * the same call. The comparator is exposed (compareRenders) so a test can also
 * prove it is not a function that always answers "equal".
 */

#ifndef LMMS_WASM_OFFLINE_RENDER_H
#define LMMS_WASM_OFFLINE_RENDER_H

#include "lmms_export.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lmms::wasm
{

//! One offline render of one module.
struct OfflineRender
{
	bool ok = false;
	std::string error;
	//! Plane 0 of the module's output, `frames` long.
	std::vector<float> output;
	std::uint64_t blocks = 0;
	//! Blocks the path could not deliver (0 for a trustworthy render).
	std::uint64_t droppedBlocks = 0;
	std::uint64_t trappedBlocks = 0;
	int channels = 1;
	int latency = 0;
	//! "pool" - through the shared WasmWorkerPool lanes; "inline" - direct
	//! calls on the caller's thread, which is the control the floor is
	//! measured on.
	std::string path;
};

//! How two renders differ, sample by sample.
struct RenderDifference
{
	std::uint64_t frames = 0;
	std::uint64_t differingFrames = 0;
	double maxAbsDifference = 0.0;
	std::int64_t worstFrame = -1;
	double rmsDifference = 0.0;
	bool equal() const { return differingFrames == 0; }

	//! True when \p other is at most as large as this in both respects. This
	//! is the comparison the verdict uses: two measured differences, never a
	//! difference against a constant.
	bool covers(const RenderDifference& other) const
	{
		return other.differingFrames <= differingFrames && other.maxAbsDifference <= maxAbsDifference;
	}
};

//! The bound the subject is judged against, measured in the same call.
struct RenderTolerance
{
	RenderDifference floor;
	std::uint64_t controlPairs = 0;
	std::string source;
};

//! Everything one offline-render request produced.
struct OfflineRenderOutcome
{
	bool ok = false;
	std::string error;
	std::uint32_t frames = 0;
	std::uint32_t blockFrames = 0;
	std::uint32_t repeats = 0;
	float sampleRate = 0.0f;
	int channels = 1;
	int latency = 0;
	//! One per repeat, through the shared pool (the subject).
	std::vector<OfflineRender> runs;
	//! Two renders on the control path (the floor).
	std::vector<OfflineRender> control;
	//! The worst pair among the subject runs.
	RenderDifference subject;
	//! The worst pair among the control runs - the same-build run-to-run floor.
	RenderTolerance tolerance;
	//! The worst subject run against the first control run: the pooled path
	//! against the single-threaded one.
	RenderDifference subjectAgainstControl;
	//! subject <= tolerance.floor, both measured in this call.
	bool deterministic = false;
	//! The comparator's own self test: one sample of run 0 perturbed by
	//! `comparatorPerturbation`, which compareRenders() must report as a
	//! difference. False when the perturbation was not detected.
	bool comparatorDetectsADifference = false;
	double comparatorPerturbation = 0.0;
};

/*! The comparator: frames compared, frames differing, the largest absolute
 *  difference and the frame it is at, and the RMS of the differences. Two
 *  vectors of different lengths are compared over the shorter one and the
 *  length difference is counted as differing frames.
 */
LMMS_EXPORT auto compareRenders(const std::vector<float>& a, const std::vector<float>& b)
	-> RenderDifference;

/*! The deterministic offline render.
 *
 *  \p input is plane 0's stimulus, `frames` long (a short vector is zero
 *  filled). \p blockFrames is the block size submitted to the module (`frames`
 *  is rendered as ceil(frames / blockFrames) blocks; the last block is short).
 *  \p repeats renders go through the shared pool, each on a FRESH worker, and
 *  two control renders go through the direct sandbox path.
 *
 *  Both paths drive parameter 0 with 0.5, so a module that reads
 *  host_get_param(0) renders a real signal rather than the silence a
 *  value-initialised parameter would give (see kRenderParam0 in the .cpp).
 *
 *  Offline means one block in flight at a time: a block is always collected
 *  before the next is submitted, so a module's own state advances in block
 *  order and no block can be reordered or dropped. That is the property the
 *  pooled path is being measured for; live streaming (submit now, collect
 *  later) keeps its own, weaker contract.
 */
LMMS_EXPORT auto renderOffline(const std::string& modulePath, std::uint32_t frames,
	std::uint32_t blockFrames, std::uint32_t repeats, float sampleRate,
	const std::vector<float>& input) -> OfflineRenderOutcome;

} // namespace lmms::wasm

#endif // LMMS_WASM_OFFLINE_RENDER_H
