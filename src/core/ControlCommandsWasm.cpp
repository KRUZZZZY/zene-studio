/*
 * ControlCommandsWasm.cpp - the wasm.* command group's READ half: list, get_state
 *                           and process (SPEC A11-A16, item #614).
 *
 * The group's mutating half (load, unload, set_param) is ControlCommandsWasmEdit.cpp
 * and their shared vocabulary is ControlWasmSupport.h. The split is the one the
 * automation, warp, rack, clip-edit and comp groups already use: this fork's
 * file-length ratchet reads a file as a unit.
 *
 * `WANT_WASM` compiles the sandbox in only when the wasmtime C API is on the find
 * path (CMakeLists.txt:957-963, cmake/modules/FindWasmtime.cmake). This file is
 * added to the source list under the same condition, its registration call is
 * guarded by `#ifdef LMMS_HAVE_WASM` in src/core/ControlRegistry.cpp and its A16
 * rows carry the same #ifdef, so a build without wasmtime neither compiles it,
 * nor registers its ids, nor carries rows for ids it does not have.
 *
 * WHY THE GROUP EXISTS. The sandbox is reachable from the interface only through
 * the `wasm_effect` plugin's modal file chooser - a display - so before this group
 * nothing about it could be seen or driven from `--control-socket`, and the
 * release contract (V0.3-ALPHA-PLAN.md section 1) says a feature that cannot be
 * driven through the socket is not in the release. The engine half is
 * src/wasm/WasmSandboxHost.h; this is the surface half.
 *
 * THE LIMIT, STATED RATHER THAN IMPLIED. These commands host a module in the
 * host's own sandbox and report what the ABI does with it. They do NOT put a
 * module into a device's audio path - the effect's own instance is still given a
 * module only through its dialog. docs/WASM-EFFECT-ABI.md section 13 records it.
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

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"
#include "ControlWasmSupport.h"

#include "src/wasm/WasmAbi.h"
#include "src/wasm/WasmSandboxHost.h"
#include "src/wasm/WasmWorker.h"

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

// ---------------------------------------------------------------------------
// wasm.list - what the sandbox can host
// ---------------------------------------------------------------------------

//! The sandbox's hard limits, read from the ABI header rather than restated, so
//! a module author reading this result reads the values the host enforces.
QJsonObject limitsJson()
{
	QJsonObject out;
	out.insert(QStringLiteral("channels"), static_cast<int>(wasm::abi::maxChannels));
	out.insert(QStringLiteral("params"), static_cast<int>(wasm::abi::maxParams));
	out.insert(QStringLiteral("log_bytes"), static_cast<double>(wasm::abi::maxLogBytes));
	out.insert(QStringLiteral("memory_bytes"),
		static_cast<double>(wasm::abi::storeMemoryLimitBytes));
	out.insert(QStringLiteral("instances"),
		static_cast<double>(wasm::abi::storeInstanceLimit));
	out.insert(QStringLiteral("tables"), static_cast<double>(wasm::abi::storeTableLimit));
	out.insert(QStringLiteral("table_elements"),
		static_cast<double>(wasm::abi::storeTableElementLimit));
	out.insert(QStringLiteral("memories"),
		static_cast<double>(wasm::abi::storeMemoryCountLimit));
	out.insert(QStringLiteral("default_fuel"),
		static_cast<double>(wasm::abi::defaultFuelBudget));
	out.insert(QStringLiteral("max_block_frames"),
		static_cast<int>(wasm::WasmWorker::maxBlockFrames));
	return out;
}

//! The exports a module declares, from ABI doc section 1.
QJsonArray exportsJson()
{
	const auto entry = [](const char* name, const char* type, bool required) {
		QJsonObject item;
		item.insert(QStringLiteral("name"), QString::fromUtf8(name));
		item.insert(QStringLiteral("type"), QString::fromUtf8(type));
		item.insert(QStringLiteral("required"), required);
		return item;
	};

	QJsonArray out;
	out.append(entry(wasm::abi::processExport, "(i32 i32 i32 f32) -> i32", true));
	out.append(entry(wasm::abi::memoryExport, "linear memory", true));
	out.append(entry(wasm::abi::channelsExport, "i32 global", false));
	out.append(entry(wasm::abi::latencyExport, "i32 global", false));
	return out;
}

//! The host imports a module may use, from ABI doc section 1.
QJsonArray importsJson()
{
	const QString module = QString::fromUtf8(wasm::abi::importModule);
	const auto entry = [&module](const char* name, const char* type) {
		QJsonObject item;
		item.insert(QStringLiteral("name"),
			module + QLatin1Char('.') + QString::fromUtf8(name));
		item.insert(QStringLiteral("type"), QString::fromUtf8(type));
		return item;
	};

	QJsonArray out;
	out.append(entry(wasm::abi::hostGetParam, "(i32) -> f32"));
	out.append(entry(wasm::abi::hostLog, "(i32 i32) -> ()"));
	out.append(entry(wasm::abi::hostGetTransportState, "() -> i32"));
	return out;
}

//! Every loadable module directly inside \p root as JSON, or -1 when \p root is
//! not a readable directory (which the caller reports as a typed error rather
//! than as "no modules").
int collectModules(const QString& root, QJsonArray* modules)
{
	QDir dir(root);
	if (!dir.exists()) { return -1; }

	const QFileInfoList entries =
		dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
	for (const QFileInfo& entry : entries)
	{
		// ONE definition of "loadable": the rule load() applies.
		const std::string format = WasmSandboxHost::formatOf(entry.absoluteFilePath().toStdString());
		if (format.empty()) { continue; }

		QJsonObject module;
		module.insert(QStringLiteral("path"), entry.absoluteFilePath());
		module.insert(QStringLiteral("name"), entry.fileName());
		module.insert(QStringLiteral("format"), wasmText(format));
		module.insert(QStringLiteral("bytes"), static_cast<double>(entry.size()));
		modules->append(module);
	}
	return static_cast<int>(modules->size());
}

ControlResult handleList(const QJsonObject& args)
{
	const QString asked = args.value(QStringLiteral("root")).toString();
	const QString root = asked.isEmpty() ? wasmDefaultModuleRoot() : asked;

	QJsonArray modules;
	const int count = collectModules(root, &modules);
	if (count < 0)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			wasmNotFound(QStringLiteral("wasm.list"),
				QStringLiteral("no readable directory at '%1'. 'root' names a directory holding "
					".wat or .wasm modules; omitted, it defaults to '%2' - the directory the build "
					"assembles the demo modules into - resolved against the process's working "
					"directory").arg(root, wasmDefaultModuleRoot())));
	}

	QJsonObject result;
	result.insert(QStringLiteral("abi_version"), static_cast<int>(wasm::abi::version));
	result.insert(QStringLiteral("import_module"),
		QString::fromUtf8(wasm::abi::importModule));
	result.insert(QStringLiteral("exports"), exportsJson());
	result.insert(QStringLiteral("imports"), importsJson());
	result.insert(QStringLiteral("limits"), limitsJson());
	result.insert(QStringLiteral("root"), root);
	result.insert(QStringLiteral("modules"), modules);
	result.insert(QStringLiteral("count"), count);
	result.insert(QStringLiteral("state"), wasmStateJson());
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// wasm.get_state
// ---------------------------------------------------------------------------

ControlResult handleGetState(const QJsonObject&)
{
	QJsonObject result;
	result.insert(QStringLiteral("state"), wasmStateJson());
	// takeLog() clears what it returns: the log belongs to the most recent call,
	// and reading it once is what "the most recent call's text" means.
	result.insert(QStringLiteral("last_log"),
		wasmText(WasmSandboxHost::instance().takeLog()));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// wasm.process
// ---------------------------------------------------------------------------

//! The conformance suite's own stimulus (tests/src/wasm/WasmAbiConformanceTest.cpp):
//! a rising ramp, so a module that clips or scales has something to show it with.
std::vector<float> rampInput(std::uint32_t frames)
{
	std::vector<float> out(frames, 0.0f);
	for (std::uint32_t i = 0; i < frames; ++i) { out[i] = 1.0f + static_cast<float>(i); }
	return out;
}

std::vector<float> jsonToFloats(const QJsonArray& values)
{
	std::vector<float> out;
	out.reserve(static_cast<std::size_t>(values.size()));
	for (const QJsonValue& value : values)
	{
		out.push_back(static_cast<float>(value.toDouble()));
	}
	return out;
}

QString statusName(wasm::CallResult::Status status)
{
	switch (status)
	{
		case wasm::CallResult::Status::Ok: return QStringLiteral("ok");
		case wasm::CallResult::Status::Trap: return QStringLiteral("trap");
		case wasm::CallResult::Status::Error: return QStringLiteral("error");
	}
	return QStringLiteral("error");
}

QJsonObject processJson(const wasm::HostProcessOutcome& outcome, std::uint32_t frames,
	float sampleRate, const QString& stimulus)
{
	QJsonArray output;
	float peak = 0.0f;
	for (const float sample : outcome.output)
	{
		peak = std::max(peak, std::fabs(sample));
		output.append(static_cast<double>(sample));
	}

	QJsonObject result;
	result.insert(QStringLiteral("state"), wasmStateJson());
	result.insert(QStringLiteral("status"), statusName(outcome.status));
	result.insert(QStringLiteral("return_value"), outcome.returnValue);
	result.insert(QStringLiteral("trap_code"), outcome.trapCode);
	result.insert(QStringLiteral("message"), wasmText(outcome.message));
	result.insert(QStringLiteral("fuel_consumed"), static_cast<double>(outcome.fuelConsumed));
	result.insert(QStringLiteral("planes_run"), outcome.planesRun);
	result.insert(QStringLiteral("channels"), outcome.channels);
	result.insert(QStringLiteral("frames"), static_cast<int>(frames));
	result.insert(QStringLiteral("sample_rate"), static_cast<double>(sampleRate));
	result.insert(QStringLiteral("stimulus"), stimulus);
	result.insert(QStringLiteral("output"), output);
	result.insert(QStringLiteral("output_peak"), static_cast<double>(peak));
	result.insert(QStringLiteral("log"), wasmText(outcome.log));
	return result;
}

ControlResult handleProcess(const QJsonObject& args)
{
	WasmSandboxHost& host = WasmSandboxHost::instance();
	if (!host.isLoaded())
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			wasmNotFound(QStringLiteral("wasm.process"),
				QStringLiteral("no module is loaded: wasm.load one first")));
	}
	if (!host.hasProcess())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			wasmNotFound(QStringLiteral("wasm.process"),
				QStringLiteral("the hosted module exports no process(), so it is a probe rather "
					"than an effect and there is nothing to run")));
	}

	const std::uint32_t frames = args.contains(QStringLiteral("frames"))
		? static_cast<std::uint32_t>(args.value(QStringLiteral("frames")).toInt()) : 48u;
	const float sampleRate = args.contains(QStringLiteral("sample_rate"))
		? static_cast<float>(args.value(QStringLiteral("sample_rate")).toDouble()) : 48000.0f;
	const bool callerInput = args.contains(QStringLiteral("input"));
	const std::vector<float> input = callerInput
		? jsonToFloats(args.value(QStringLiteral("input")).toArray()) : rampInput(frames);

	const wasm::HostProcessOutcome outcome = host.process(frames, sampleRate, input);
	if (outcome.status == wasm::CallResult::Status::Error)
	{
		// The embedder rejected the call: a signature that does not match, or a
		// fuel error. That is a refusal, not a result the caller can read.
		return ControlResult::failure(ControlErrorKind::Refused,
			wasmNotFound(QStringLiteral("wasm.process"),
				QStringLiteral("the call was rejected before it ran: %1")
					.arg(wasmText(outcome.message))));
	}

	return ControlResult::success(processJson(outcome, frames, sampleRate,
		callerInput ? QStringLiteral("caller") : QStringLiteral("ramp")));
}

void registerWasmListCommand(ControlRegistry& registry)
{
	registry.registerCommand(wasmCommand(QStringLiteral("list"),
		QStringLiteral("What this build's WASM DSP sandbox can host: the ABI version and import "
			"module, the exports a module must declare (with their wasm types) and the host "
			"imports it may use, the hard limits the host enforces (channels, parameter slots, log "
			"bytes, linear-memory and store limits, the default fuel budget, the largest block), "
			"and every .wat/.wasm module in a directory. The directory is 'root', defaulting to "
			"'wasm-modules' - the directory the build assembles the demo modules into - resolved "
			"against the process's working directory. Also reports the currently hosted module."),
		{{QStringLiteral("root"), stringProperty()}}, {},
		{{QStringLiteral("abi_version"), integerProperty()},
			{QStringLiteral("import_module"), stringProperty()},
			{QStringLiteral("exports"), arrayProperty()},
			{QStringLiteral("imports"), arrayProperty()},
			{QStringLiteral("limits"), objectProperty()},
			{QStringLiteral("root"), stringProperty()},
			{QStringLiteral("modules"), arrayProperty()},
			{QStringLiteral("count"), integerProperty()},
			{QStringLiteral("state"), wasmStateProperty()}},
		false, [](const QJsonObject& args) { return handleList(args); }));
}

void registerWasmGetStateCommand(ControlRegistry& registry)
{
	registry.registerCommand(wasmCommand(QStringLiteral("get_state"),
		QStringLiteral("The hosted module's state: whether anything is hosted, and its "
			"path/format/fuel budget; when one is, its declared channel count and latency (the "
			"values the host reads from the channels and latency globals AND then clamps, ABI doc "
			"section 4.1), whether it exports process(), its linear-memory size, and all 16 "
			"parameter slots. 'last_log' is the text the most recent call passed to host_log() - "
			"reading it clears it. Writes nothing."),
		{}, {},
		{{QStringLiteral("state"), wasmStateProperty()},
			{QStringLiteral("last_log"), stringProperty()}},
		false, [](const QJsonObject& args) { return handleGetState(args); }));
}

void registerWasmProcessCommand(ControlRegistry& registry)
{
	registry.registerCommand(wasmCommand(QStringLiteral("process"),
		QStringLiteral("Run one block through the hosted module and report what happened. "
			"process() is entered once per channel plane, exactly as WasmWorker enters it, and "
			"plane 0's output is read back. 'frames' (1..8192) and 'sample_rate' default to 48 and "
			"48000; 'input' supplies plane 0's samples, and when it is absent the conformance "
			"suite's rising ramp is used. 'status' is 'ok', 'trap' or 'error': a trap is REPORTED "
			"rather than thrown, which is the sandbox's whole purpose. The call's own outcome is a "
			"successful result; only having nothing to run is a refusal."),
		{{QStringLiteral("frames"),
				integerProperty(1, static_cast<int>(wasm::WasmWorker::maxBlockFrames))},
			{QStringLiteral("sample_rate"), numberProperty()},
			{QStringLiteral("input"), arrayProperty()}}, {},
		{{QStringLiteral("state"), wasmStateProperty()},
			{QStringLiteral("status"), enumProperty({QStringLiteral("ok"), QStringLiteral("trap"),
				QStringLiteral("error")})},
			{QStringLiteral("return_value"), integerProperty()},
			{QStringLiteral("trap_code"), integerProperty()},
			{QStringLiteral("message"), stringProperty()},
			{QStringLiteral("fuel_consumed"), numberProperty()},
			{QStringLiteral("planes_run"), integerProperty()},
			{QStringLiteral("channels"), integerProperty()},
			{QStringLiteral("frames"), integerProperty()},
			{QStringLiteral("sample_rate"), numberProperty()},
			{QStringLiteral("stimulus"), stringProperty()},
			{QStringLiteral("output"), arrayProperty()},
			{QStringLiteral("output_peak"), numberProperty()},
			{QStringLiteral("log"), stringProperty()}},
		false, [](const QJsonObject& args) { return handleProcess(args); }));
}

} // namespace

void registerWasmCommands(ControlRegistry& registry)
{
	registerWasmListCommand(registry);
	registerWasmGetStateCommand(registry);
	registerWasmProcessCommand(registry);
	// The shared worker pool and the deterministic offline render (CODE-5,
	// feature row 73), in their own translation unit.
	registerWasmRenderCommands(registry);
	// The mutating half, in its own translation unit (the automation, warp, rack
	// and comp groups' read/edit split).
	registerWasmEditCommands(registry);
}

} // namespace lmms
