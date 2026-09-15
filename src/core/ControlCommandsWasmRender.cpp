/*
 * ControlCommandsWasmRender.cpp - the wasm.* group's POOL and OFFLINE RENDER
 *                                  commands (CODE-5, feature row 73)
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
 * WHAT THESE TWO COMMANDS ADD. `wasm.pool` is the shared lane pool's own state
 * (lanes, workers, lane passes, blocks, wake-ups, suppressed wake-ups, parks),
 * i.e. the observable half of "the workers share a pool and a queued block is
 * picked up by a wake-up instead of a 200 us poll". `wasm.render_offline` is
 * the deterministic offline render, and its result carries a MEASURED verdict:
 * two inline (no-pool) renders of the same input give the same-build run-to-run
 * FLOOR, two or more pooled renders give the subject, and `deterministic` is
 * the subject being within that floor. Nothing is compared against a constant
 * and no byte-identity is required - see the header of WasmOfflineRender.h and
 * docs/RENDER-DETERMINISM.md for why.
 *
 * The two commands are read-only in the A16 sense (one not_mutating row each):
 * the render runs a module in a sandbox of its own and writes no project state.
 */

#include <QCryptographicHash>
#include <QByteArrayView>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"
#include "ControlWasmSupport.h"

#include "src/wasm/WasmOfflineRender.h"
#include "src/wasm/WasmSandboxHost.h"
#include "src/wasm/WasmWorker.h"
#include "src/wasm/WasmWorkerPool.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

namespace wasm = lmms::wasm;
using wasm::WasmSandboxHost;

QString fromStd(const std::string& text)
{
	return QString::fromUtf8(text.c_str(), static_cast<int>(text.size()));
}

//! Informational only: the digests are reported so a reader can see whether two
//! runs came out byte-identical, but the VERDICT is the measured comparison
//! below and never this.
QString digestOf(const std::vector<float>& samples)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!samples.empty())
	{
		// Qt 6.4 deprecates addData(const char*, qsizetype) in favour of the
		// QByteArrayView overload, and this tree builds with
		// -Werror=deprecated-declarations. The bytes hashed are the same, so
		// every digest this function returns is unchanged.
		hash.addData(QByteArrayView(reinterpret_cast<const char*>(samples.data()),
			static_cast<qsizetype>(samples.size() * sizeof(float))));
	}
	return QString::fromLatin1(hash.result().toHex());
}

QJsonObject differenceJson(const wasm::RenderDifference& difference)
{
	QJsonObject out;
	out.insert(QStringLiteral("frames"), static_cast<double>(difference.frames));
	out.insert(QStringLiteral("differing_frames"), static_cast<double>(difference.differingFrames));
	out.insert(QStringLiteral("max_abs_difference"), difference.maxAbsDifference);
	out.insert(QStringLiteral("worst_frame"), static_cast<double>(difference.worstFrame));
	out.insert(QStringLiteral("rms_difference"), difference.rmsDifference);
	out.insert(QStringLiteral("identical"), difference.equal());
	return out;
}

QJsonObject renderJson(const wasm::OfflineRender& render)
{
	double peak = 0.0;
	double sumOfSquares = 0.0;
	for (const float sample : render.output)
	{
		peak = std::max(peak, std::fabs(static_cast<double>(sample)));
		sumOfSquares += static_cast<double>(sample) * static_cast<double>(sample);
	}
	const double rms = render.output.empty()
		? 0.0
		: std::sqrt(sumOfSquares / static_cast<double>(render.output.size()));

	QJsonObject out;
	out.insert(QStringLiteral("path"), fromStd(render.path));
	out.insert(QStringLiteral("ok"), render.ok);
	out.insert(QStringLiteral("blocks"), static_cast<double>(render.blocks));
	out.insert(QStringLiteral("dropped_blocks"), static_cast<double>(render.droppedBlocks));
	out.insert(QStringLiteral("trapped_blocks"), static_cast<double>(render.trappedBlocks));
	out.insert(QStringLiteral("frames"), static_cast<int>(render.output.size()));
	out.insert(QStringLiteral("peak"), peak);
	out.insert(QStringLiteral("rms"), rms);
	out.insert(QStringLiteral("sha256"), digestOf(render.output));
	if (!render.ok) { out.insert(QStringLiteral("error"), fromStd(render.error)); }
	return out;
}

// ---------------------------------------------------------------------------
// wasm.pool
// ---------------------------------------------------------------------------

ControlResult handlePool(const QJsonObject&)
{
	const wasm::WasmWorkerPool::Stats stats = wasm::WasmWorkerPool::instance().stats();

	QJsonObject pool;
	pool.insert(QStringLiteral("lanes"), static_cast<double>(stats.lanes));
	pool.insert(QStringLiteral("max_lanes"), static_cast<double>(wasm::WasmWorkerPool::maxLanes));
	pool.insert(QStringLiteral("workers"), static_cast<double>(stats.workers));
	pool.insert(QStringLiteral("max_workers"),
		static_cast<double>(wasm::WasmWorkerPool::maxWorkers));
	pool.insert(QStringLiteral("lane_passes"), static_cast<double>(stats.lanePasses));
	pool.insert(QStringLiteral("blocks"), static_cast<double>(stats.blocks));
	pool.insert(QStringLiteral("wakeups"), static_cast<double>(stats.wakeups));
	pool.insert(QStringLiteral("wakes_suppressed"), static_cast<double>(stats.wakesSuppressed));
	pool.insert(QStringLiteral("parks"), static_cast<double>(stats.parks));

	QJsonObject result;
	result.insert(QStringLiteral("state"), wasmStateJson());
	result.insert(QStringLiteral("pool"), pool);
	result.insert(QStringLiteral("lanes_per_process"),
		static_cast<double>(wasm::WasmWorkerPool::laneCount()));
	result.insert(QStringLiteral("note"),
		QStringLiteral("ONE pool of bounded lanes is shared by every hosted wasm worker in this "
					   "process: `workers` may exceed `lanes`, and the process costs `lanes` "
					   "threads rather than one per module. A lane parks on a generation counter "
					   "and is woken by WasmWorker::submit(); `wakes_suppressed` counts the "
					   "submits that needed no wake-up at all because a lane was already awake "
					   "(the audio thread then takes no syscall), `parks` counts how often a lane "
					   "went to sleep, and `wakeups` counts the futex wakes the audio path "
					   "requested. Counters are process-wide and never reset. Read-only."));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// wasm.render_offline
// ---------------------------------------------------------------------------

//! The conformance suite's own stimulus, so a caller that passes no input gets
//! something with structure rather than silence.
std::vector<float> rampInput(std::uint32_t frames)
{
	std::vector<float> out(frames, 0.0f);
	for (std::uint32_t i = 0; i < frames; ++i)
	{
		out[i] = 0.25f + 0.5f * static_cast<float>(i % 97) / 97.0f;
	}
	return out;
}

std::vector<float> jsonToFloats(const QJsonArray& values)
{
	std::vector<float> out;
	out.reserve(static_cast<std::size_t>(values.size()));
	for (const QJsonValue& value : values) { out.push_back(static_cast<float>(value.toDouble())); }
	return out;
}

ControlResult handleRenderOffline(const QJsonObject& args)
{
	WasmSandboxHost& host = WasmSandboxHost::instance();
	const QString asked = args.value(QStringLiteral("module")).toString();
	const std::string modulePath =
		asked.isEmpty() ? host.modulePath() : asked.toStdString();
	if (modulePath.empty())
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			wasmNotFound(QStringLiteral("wasm.render_offline"),
				QStringLiteral("no module: 'module' names a .wat/.wasm file, and none is loaded "
							   "either - wasm.load one first")));
	}

	const std::uint32_t frames = args.contains(QStringLiteral("frames"))
		? static_cast<std::uint32_t>(args.value(QStringLiteral("frames")).toInt())
		: 4096u;
	const std::uint32_t blockFrames = args.contains(QStringLiteral("block_frames"))
		? static_cast<std::uint32_t>(args.value(QStringLiteral("block_frames")).toInt())
		: 256u;
	const std::uint32_t repeats = args.contains(QStringLiteral("repeats"))
		? static_cast<std::uint32_t>(args.value(QStringLiteral("repeats")).toInt())
		: 2u;
	const float sampleRate = args.contains(QStringLiteral("sample_rate"))
		? static_cast<float>(args.value(QStringLiteral("sample_rate")).toDouble())
		: 48000.0f;
	const bool callerInput = args.contains(QStringLiteral("input"));
	const std::vector<float> input = callerInput
		? jsonToFloats(args.value(QStringLiteral("input")).toArray()) : rampInput(frames);

	const wasm::OfflineRenderOutcome outcome =
		wasm::renderOffline(modulePath, frames, blockFrames, repeats, sampleRate, input);
	if (!outcome.ok)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			wasmNotFound(QStringLiteral("wasm.render_offline"),
				QStringLiteral("the render did not complete: %1").arg(fromStd(outcome.error))));
	}

	QJsonArray runs;
	for (const wasm::OfflineRender& render : outcome.runs) { runs.append(renderJson(render)); }
	QJsonArray control;
	for (const wasm::OfflineRender& render : outcome.control) { control.append(renderJson(render)); }

	QJsonObject tolerance;
	tolerance.insert(QStringLiteral("floor"), differenceJson(outcome.tolerance.floor));
	tolerance.insert(QStringLiteral("control_pairs"), static_cast<double>(outcome.tolerance.controlPairs));
	tolerance.insert(QStringLiteral("source"), fromStd(outcome.tolerance.source));

	QJsonObject comparator;
	comparator.insert(QStringLiteral("perturbation"), outcome.comparatorPerturbation);
	comparator.insert(QStringLiteral("detected"), outcome.comparatorDetectsADifference);

	QJsonObject result;
	result.insert(QStringLiteral("state"), wasmStateJson());
	result.insert(QStringLiteral("module"), fromStd(modulePath));
	result.insert(QStringLiteral("format"), wasmText(WasmSandboxHost::formatOf(modulePath)));
	result.insert(QStringLiteral("frames"), static_cast<int>(outcome.frames));
	result.insert(QStringLiteral("block_frames"), static_cast<int>(outcome.blockFrames));
	result.insert(QStringLiteral("repeats"), static_cast<int>(outcome.repeats));
	result.insert(QStringLiteral("sample_rate"), static_cast<double>(outcome.sampleRate));
	result.insert(QStringLiteral("channels"), outcome.channels);
	result.insert(QStringLiteral("latency"), outcome.latency);
	result.insert(QStringLiteral("mode"), QStringLiteral("offline"));
	result.insert(QStringLiteral("blocks_in_flight"), 1);
	result.insert(QStringLiteral("stimulus"), callerInput ? QStringLiteral("caller")
														  : QStringLiteral("ramp"));
	// The subject (through the shared pool) and the floor it was measured against.
	result.insert(QStringLiteral("runs"), runs);
	result.insert(QStringLiteral("control_runs"), control);
	result.insert(QStringLiteral("subject"), differenceJson(outcome.subject));
	result.insert(QStringLiteral("subject_against_control"),
		differenceJson(outcome.subjectAgainstControl));
	result.insert(QStringLiteral("tolerance"), tolerance);
	result.insert(QStringLiteral("deterministic"), outcome.deterministic);
	result.insert(QStringLiteral("comparator_self_test"), comparator);
	result.insert(QStringLiteral("note"),
		QStringLiteral("A verdict, not a hash comparison: `tolerance.floor` is the same-build "
					   "run-to-run difference of two INLINE renders of this input, measured in "
					   "this call; `subject` is the worst pair among the pooled runs; "
					   "`deterministic` is subject <= floor in BOTH differing frames and largest "
					   "absolute difference. The sha256 digests are informational - this project "
					   "has a recorded render non-determinism (docs/RENDER-DETERMINISM.md), so "
					   "byte identity is not the criterion and would not be a proof. "
					   "`comparator_self_test.detected` is the comparator finding a one-sample "
					   "perturbation, which is what makes 'identical' mean something. Read-only."));
	return ControlResult::success(result);
}

void registerWasmPoolCommand(ControlRegistry& registry)
{
	registry.registerCommand(wasmCommand(QStringLiteral("pool"),
		QStringLiteral("The shared WASM worker pool: how many lane threads this process runs "
			"(bounded, shared by every hosted module - not one per module), how many workers are "
			"registered, how many lane passes found work, how many queued blocks the lanes "
			"processed, and what the audio path's wake-ups cost: `wakeups` (a parked lane was "
			"woken), `wakes_suppressed` (a lane was already awake, so the audio thread took no "
			"syscall) and `parks` (a lane went to sleep). Counters are process-wide and never "
			"reset. Writes nothing."),
		{}, {},
		{{QStringLiteral("state"), wasmStateProperty()},
			{QStringLiteral("pool"), objectProperty()},
			{QStringLiteral("lanes_per_process"), integerProperty()},
			{QStringLiteral("note"), stringProperty()}},
		false, [](const QJsonObject& args) { return handlePool(args); }));
}

void registerWasmRenderOfflineCommand(ControlRegistry& registry)
{
	registry.registerCommand(wasmCommand(QStringLiteral("render_offline"),
		QStringLiteral("Render a module offline - one block in flight, on a fresh worker of the "
			"shared pool, so a module's state advances in block order - and report whether the "
			"render is reproducible WITHIN A MEASURED TOLERANCE rather than against a hash. "
			"'repeats' pooled renders (2..8, default 2) are compared pairwise, and two inline "
			"(no-pool) renders of the same input give this build's own run-to-run floor; "
			"'deterministic' is the worst pooled pair being within that floor in both differing "
			"frames and largest absolute difference. 'comparator_self_test' perturbs one sample "
			"and reports that the comparator finds it, so 'identical' is a measurement and not a "
			"constant. 'module' defaults to the hosted module; 'frames' (default 4096), "
			"'block_frames' (default 256), 'sample_rate' (default 48000) and 'input' (plane 0; a "
			"ramp by default) describe the render. Writes nothing."),
		{{QStringLiteral("module"), stringProperty()},
			{QStringLiteral("frames"), integerProperty(1, 4194304)},
			{QStringLiteral("block_frames"), integerProperty(1, 8192)},
			{QStringLiteral("repeats"), integerProperty(2, 8)},
			{QStringLiteral("sample_rate"), numberProperty()},
			{QStringLiteral("input"), arrayProperty()}},
		{},
		{{QStringLiteral("state"), wasmStateProperty()},
			{QStringLiteral("module"), stringProperty()},
			{QStringLiteral("format"), stringProperty()},
			{QStringLiteral("frames"), integerProperty()},
			{QStringLiteral("block_frames"), integerProperty()},
			{QStringLiteral("repeats"), integerProperty()},
			{QStringLiteral("sample_rate"), numberProperty()},
			{QStringLiteral("channels"), integerProperty()},
			{QStringLiteral("latency"), integerProperty()},
			{QStringLiteral("mode"), enumProperty({QStringLiteral("offline")})},
			{QStringLiteral("blocks_in_flight"), integerProperty()},
			{QStringLiteral("stimulus"), enumProperty({QStringLiteral("caller"), QStringLiteral("ramp")})},
			{QStringLiteral("runs"), arrayProperty()},
			{QStringLiteral("control_runs"), arrayProperty()},
			{QStringLiteral("subject"), objectProperty()},
			{QStringLiteral("subject_against_control"), objectProperty()},
			{QStringLiteral("tolerance"), objectProperty()},
			{QStringLiteral("deterministic"), booleanProperty()},
			{QStringLiteral("comparator_self_test"), objectProperty()},
			{QStringLiteral("note"), stringProperty()}},
		false, [](const QJsonObject& args) { return handleRenderOffline(args); }));
}

} // namespace

void registerWasmRenderCommands(ControlRegistry& registry)
{
	registerWasmPoolCommand(registry);
	registerWasmRenderOfflineCommand(registry);
}

} // namespace lmms
