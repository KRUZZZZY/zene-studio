/*
 * ControlCommandsWasmEdit.cpp - the wasm.* command group's MUTATING half: load,
 *                               unload and set_param (SPEC A11-A16, item #614).
 *
 * The read half (list, get_state, process) is ControlCommandsWasm.cpp and their
 * shared vocabulary is ControlWasmSupport.h. The split is the one the automation,
 * warp, rack and comp groups already use: this fork's file-length ratchet reads a
 * file as a unit.
 *
 * THE INVERSES ARE RECORDED, NOT ASSUMED. Each of the three records a snapshot
 * whose inverse is a COMMAND (`applies: command`), which control.undo dispatches
 * through the registry - the same shape browser.tag.add and control.set_undo_depth
 * use. What the inverse restores is the module PATH or the parameter VALUE, not a
 * module's internal state: re-hosting a module instantiates it from a cold memory
 * image (docs/WASM-EFFECT-ABI.md section 7), and the mechanism string of each row
 * says so rather than letting an undone load imply more than it does.
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

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"
#include "ControlWasmSupport.h"

#include "src/wasm/WasmAbi.h"
#include "src/wasm/WasmSandboxHost.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

namespace wasm = lmms::wasm;
using wasm::WasmSandboxHost;

// ---------------------------------------------------------------------------
// wasm.load / wasm.unload
// ---------------------------------------------------------------------------

//! The inverse of a load: the module that was hosted before it, or an unload
//! when there was none. `applies: command` is what control.undo dispatches.
QJsonObject loadTransaction(const QString& previous)
{
	QJsonObject inverse;
	inverse.insert(QStringLiteral("applies"), QStringLiteral("command"));
	if (previous.isEmpty())
	{
		inverse.insert(QStringLiteral("op"), QStringLiteral("wasm.unload"));
		inverse.insert(QStringLiteral("args"), QJsonObject());
	}
	else
	{
		inverse.insert(QStringLiteral("op"), QStringLiteral("wasm.load"));
		inverse.insert(QStringLiteral("args"),
			QJsonObject{{QStringLiteral("path"), previous}});
	}

	QJsonObject transaction;
	transaction.insert(QStringLiteral("before"),
		QJsonObject{{QStringLiteral("path"), previous}});
	transaction.insert(QStringLiteral("inverse"), inverse);
	transaction.insert(QStringLiteral("reversible"), true);
	transaction.insert(QStringLiteral("mechanism"), QStringLiteral(
		"snapshot: the module hosted before this call is in before.path, and the recorded inverse "
		"- wasm.load with that path, or wasm.unload when nothing was hosted - is dispatched by "
		"control.undo. What the inverse does NOT restore is the module's own linear memory and its "
		"16 parameter slots: a re-host instantiates from a cold memory image (ABI doc section 7), "
		"so a stateful module starts over"));
	return transaction;
}

QJsonObject unloadTransaction(const QString& unloaded)
{
	QJsonObject transaction;
	transaction.insert(QStringLiteral("before"),
		QJsonObject{{QStringLiteral("path"), unloaded}});
	transaction.insert(QStringLiteral("inverse"),
		QJsonObject{{QStringLiteral("op"), QStringLiteral("wasm.load")},
			{QStringLiteral("applies"), QStringLiteral("command")},
			{QStringLiteral("args"), QJsonObject{{QStringLiteral("path"), unloaded}}}});
	transaction.insert(QStringLiteral("reversible"), true);
	transaction.insert(QStringLiteral("mechanism"), QStringLiteral(
		"snapshot: the path of the module this call dropped is in before.path and the recorded "
		"inverse is wasm.load with it, dispatched by control.undo. The module is re-instantiated "
		"from a cold memory image, so its own state and its parameter slots do not come back"));
	return transaction;
}

ControlResult handleLoad(const QJsonObject& args)
{
	WasmSandboxHost& host = WasmSandboxHost::instance();
	const QString path = args.value(QStringLiteral("path")).toString();
	if (path.isEmpty() || !QFileInfo::exists(path))
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			wasmNotFound(QStringLiteral("wasm.load"),
				QStringLiteral("no module file at '%1'").arg(path)));
	}

	std::uint64_t fuel = 0;
	if (args.contains(QStringLiteral("fuel")))
	{
		fuel = static_cast<std::uint64_t>(args.value(QStringLiteral("fuel")).toDouble());
	}

	const QString previous = wasmText(host.modulePath());
	const wasm::HostLoadOutcome outcome = host.load(path.toStdString(), fuel);
	if (!outcome.ok)
	{
		// The module already hosted is untouched by a refused load (the host
		// compiles into a fresh sandbox and commits only on success), so nothing
		// has changed and nothing needs an inverse.
		return ControlResult::failure(ControlErrorKind::Refused,
			wasmNotFound(QStringLiteral("wasm.load"),
				QStringLiteral("'%1' could not be hosted: %2")
					.arg(path, wasmText(outcome.error))));
	}

	QJsonObject result;
	result.insert(QStringLiteral("state"), wasmStateJson());
	result.insert(QStringLiteral("previous_path"), previous);
	result.insert(QStringLiteral("bytes"), static_cast<double>(outcome.bytes));
	result.insert(QStringLiteral("__transaction"), loadTransaction(previous));
	return ControlResult::success(result);
}

ControlResult handleUnload(const QJsonObject&)
{
	WasmSandboxHost& host = WasmSandboxHost::instance();
	const QString previous = wasmText(host.modulePath());
	if (previous.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			wasmNotFound(QStringLiteral("wasm.unload"),
				QStringLiteral("no module is hosted, so there is nothing to unload "
					"(wasm.get_state reports what is)")));
	}

	host.unload();
	QJsonObject result;
	result.insert(QStringLiteral("state"), wasmStateJson());
	result.insert(QStringLiteral("unloaded"), true);
	result.insert(QStringLiteral("path"), previous);
	result.insert(QStringLiteral("__transaction"), unloadTransaction(previous));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// wasm.set_param
// ---------------------------------------------------------------------------

ControlResult handleSetParam(const QJsonObject& args)
{
	WasmSandboxHost& host = WasmSandboxHost::instance();
	if (!host.isLoaded())
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			wasmNotFound(QStringLiteral("wasm.set_param"),
				QStringLiteral("no module is hosted, so there is no host_get_param() to answer")));
	}

	const int index = args.value(QStringLiteral("index")).toInt();
	const float value = static_cast<float>(args.value(QStringLiteral("value")).toDouble());
	const std::uint32_t slot = static_cast<std::uint32_t>(std::max(index, 0));
	const float previous = host.param(slot);
	if (!host.setParam(slot, value))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			wasmNotFound(QStringLiteral("wasm.set_param"),
				QStringLiteral("index %1 is outside the %2 slots a module can read through "
					"host_get_param()").arg(index)
					.arg(static_cast<int>(wasm::abi::maxParams))));
	}

	QJsonObject result;
	result.insert(QStringLiteral("state"), wasmStateJson());
	result.insert(QStringLiteral("index"), index);
	result.insert(QStringLiteral("value"), static_cast<double>(value));
	result.insert(QStringLiteral("previous"), static_cast<double>(previous));
	result.insert(QStringLiteral("__transaction"), QJsonObject{
		{QStringLiteral("before"),
			QJsonObject{{QStringLiteral("index"), index},
				{QStringLiteral("value"), static_cast<double>(previous)}}},
		{QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("wasm.set_param")},
				{QStringLiteral("applies"), QStringLiteral("command")},
				{QStringLiteral("args"),
					QJsonObject{{QStringLiteral("index"), index},
						{QStringLiteral("value"), static_cast<double>(previous)}}}}},
		{QStringLiteral("reversible"), true},
		{QStringLiteral("mechanism"), QStringLiteral(
			"snapshot: the slot's previous value is in before.value and the recorded inverse is "
			"wasm.set_param with it, dispatched by control.undo. What it restores is the HOST's "
			"slot: a module is free to ignore host_get_param() entirely, because there is no "
			"parameter ABI beyond the index (ABI doc section 6), so whether putting the value back "
			"changes what the module does is the module's business")}});
	return ControlResult::success(result);
}

void registerWasmLoadCommand(ControlRegistry& registry)
{
	registry.registerCommand(wasmCommand(QStringLiteral("load"),
		QStringLiteral("Host a module in the sandbox: 'path' is a .wat (assembled with the "
			"runtime's own wat2wasm, so no extra toolchain is needed) or a .wasm file. The module "
			"is compiled and instantiated, and 'fuel' overrides the per-call fuel budget (the "
			"default is the ABI's). ALL OR NOTHING: a module that fails to compile or instantiate "
			"is refused, typed, and the module already hosted is left alone - the host builds the "
			"new instance before replacing the old one. Reports the hosted module's own state, "
			"including the channels and latency the ABI reads from its globals."),
		{{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("fuel"), integerProperty(1, 1000000000)}},
		{QStringLiteral("path")},
		{{QStringLiteral("state"), wasmStateProperty()},
			{QStringLiteral("previous_path"), stringProperty()},
			{QStringLiteral("bytes"), numberProperty()}},
		true, [](const QJsonObject& args) { return handleLoad(args); }));
}

void registerWasmUnloadCommand(ControlRegistry& registry)
{
	registry.registerCommand(wasmCommand(QStringLiteral("unload"),
		QStringLiteral("Drop the hosted module, releasing its wasmtime store and the linear memory "
			"that went with it. Refused, typed, when nothing is hosted - there is no silent no-op. "
			"The inverse of wasm.load and of itself: the dropped module's path is recorded and one "
			"control.undo hosts it again, from a cold memory image."),
		{}, {},
		{{QStringLiteral("state"), wasmStateProperty()},
			{QStringLiteral("unloaded"), booleanProperty()},
			{QStringLiteral("path"), stringProperty()}},
		true, [](const QJsonObject& args) { return handleUnload(args); }));
}

void registerWasmSetParamCommand(ControlRegistry& registry)
{
	registry.registerCommand(wasmCommand(QStringLiteral("set_param"),
		QStringLiteral("Write one of the 16 parameter slots a module reads through "
			"env.host_get_param(index): 'index' is 0..15 and 'value' an f32. Refused, typed, when "
			"no module is hosted or the index is outside the range - never clamped silently, "
			"because a silent clamp is indistinguishable from a working write. 'previous' reports "
			"the slot's old value and one control.undo puts it back. This does NOT touch the "
			"wasm_effect plugin's 8 parameter models: those are project state, persisted with the "
			"project - reach them with plugin.param_set on a wasm_effect device instead."),
		{{QStringLiteral("index"),
				integerProperty(0, static_cast<int>(wasm::abi::maxParams) - 1)},
			{QStringLiteral("value"), numberProperty()}},
		{QStringLiteral("index"), QStringLiteral("value")},
		{{QStringLiteral("state"), wasmStateProperty()},
			{QStringLiteral("index"), integerProperty()},
			{QStringLiteral("value"), numberProperty()},
			{QStringLiteral("previous"), numberProperty()}},
		true, [](const QJsonObject& args) { return handleSetParam(args); }));
}

} // namespace

void registerWasmEditCommands(ControlRegistry& registry)
{
	registerWasmLoadCommand(registry);
	registerWasmUnloadCommand(registry);
	registerWasmSetParamCommand(registry);
}

} // namespace lmms
