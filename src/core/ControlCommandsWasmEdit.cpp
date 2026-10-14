/*
 * ControlCommandsWasmEdit.cpp - the wasm.* command group's MUTATING half: load,
 *                               unload and set_param (SPEC A11-A16, item #614),
 *                               and device_load (0.5.0 E22).
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

#include <QDomDocument>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlWasmSupport.h"
#include "Effect.h"
#include "Engine.h"
#include "Song.h"

#include "src/wasm/WasmAbi.h"
#include "src/wasm/WasmSandboxHost.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
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
	const double requestedValue = args.value(QStringLiteral("value")).toDouble();
	const double maxFloat = static_cast<double>(std::numeric_limits<float>::max());
	if (!std::isfinite(requestedValue) || requestedValue < -maxFloat || requestedValue > maxFloat)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			wasmNotFound(QStringLiteral("wasm.set_param"),
				QStringLiteral("value must be finite and within the float range")));
	}
	const float value = static_cast<float>(requestedValue);
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

// ---------------------------------------------------------------------------
// wasm.device_load (0.5.0 E22): a module into a wasm_effect DEVICE, in any chain
// ---------------------------------------------------------------------------

//! The controls element a wasm_effect saves its module path and parameters in.
const QString kDeviceControls = QStringLiteral("wasmeffectcontrols");

/*! @a state (a controlEffectStateXml document) with its module set to @a path, in @a changed, and the
 *  module it named before in @a previous. False when the device is not a wasm_effect - its document
 *  has no wasmeffectcontrols element. The device takes the module through its own settings, the way
 *  plugin.impulse_load hands a Convolution Reverb its response: the core never links the plugin. */
bool withDeviceModule(const QString& state, const QString& path, QString* changed, QString* previous)
{
	QDomDocument doc;
	if (!doc.setContent(state)) { return false; }
	QDomElement controls = doc.elementsByTagName(kDeviceControls).item(0).toElement();
	if (controls.isNull()) { return false; }
	*previous = controls.attribute(QStringLiteral("module"));
	controls.setAttribute(QStringLiteral("module"), path);
	*changed = doc.toString();
	return true;
}

//! The module the device's settings name now (empty when none is hosted).
QString deviceModule(Effect* effect)
{
	QDomDocument doc;
	doc.setContent(controlEffectStateXml(effect));
	return doc.elementsByTagName(kDeviceControls).item(0).toElement().attribute(QStringLiteral("module"));
}

//! Undo/redo: the device found again by its ids (fx-<n> is unique, rack chains included), then the
//! captured settings document restored. No pointer is held: the step may run after a reload.
void restoreDevice(const QString& targetId, const QString& pluginId, const QString& state)
{
	ControlTarget target;
	ControlResult error;
	if (!resolveControlTarget(targetId, &target, &error)) { return; }
	if (Effect* effect = resolveControlEffect(target, pluginId, &error)) { controlRestoreEffectState(effect, state.toUtf8()); }
}

ControlResult deviceRefusal(ControlErrorKind kind, const QString& why)
{
	return ControlResult::failure(kind, wasmNotFound(QStringLiteral("wasm.device_load"), why));
}

ControlResult handleDeviceLoad(const QJsonObject& args)
{
	const QString targetId = args.value(QStringLiteral("target")).toString();
	const QString pluginId = args.value(QStringLiteral("plugin")).toString();
	ControlTarget target;
	ControlResult error;
	if (!resolveControlTarget(targetId, &target, &error)) { return error; }
	Effect* effect = resolveControlEffect(target, pluginId, &error);
	if (effect == nullptr) { return error; }
	const QString path = QFileInfo(args.value(QStringLiteral("path")).toString()).absoluteFilePath();
	if (!QFileInfo::exists(path)) { return deviceRefusal(ControlErrorKind::NotFound, QStringLiteral("no module file at '%1'").arg(path)); }
	const QString before = controlEffectStateXml(effect);
	QString after;
	QString previous;
	if (!withDeviceModule(before, path, &after, &previous))
	{
		return deviceRefusal(ControlErrorKind::Refused, QStringLiteral("%1 is not a wasm_effect device").arg(pluginId));
	}
	// Refused BEFORE the device is touched: the module it runs keeps running.
	QString why;
	if (!wasmProbeDeviceModule(path, &why))
	{
		return deviceRefusal(ControlErrorKind::Refused, QStringLiteral("'%1' cannot be hosted: %2").arg(path, why));
	}
	const ControlResult restored = controlRestoreEffectState(effect, after.toUtf8());
	if (!restored.ok) { return restored; }
	if (deviceModule(effect) != path)
	{
		// Sound, but the device's worker could not host it (the pool is full, or the load timed out); the
		// device has put its previous module back, so there is nothing to undo.
		return deviceRefusal(ControlErrorKind::Refused, QStringLiteral("'%1' passed the checks but the device could not host it "
			"(wasm.pool reports the worker pool)").arg(path));
	}
	addUndoStep([targetId, pluginId, before]() { restoreDevice(targetId, pluginId, before); },
		[targetId, pluginId, after]() { restoreDevice(targetId, pluginId, after); });
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("target"), targetId}, {QStringLiteral("plugin"), pluginId},
		{QStringLiteral("module"), path}, {QStringLiteral("previous_module"), previous}});
}

void registerWasmDeviceLoadCommand(ControlRegistry& registry)
{
	ControlCommand cmd = wasmCommand(QStringLiteral("device_load"),
		QStringLiteral("Load a module into a wasm_effect DEVICE - 'plugin' fx-<n> on 'target' (trk-<n> or ch-<n>), in the "
			"target's own chain or any parallel chain of a channel's rack (plugin.load with 'chain' puts a wasm_effect "
			"there) - without the device's file chooser. 'path' is a .wasm module exporting process(). ALL OR NOTHING: "
			"the module is compiled and instantiated first and one that fails is refused, typed, with the device's "
			"module still running; the swap itself waits for the audio period to end. The path is saved with the "
			"project like the chooser's, and one control.undo puts the device's previous module (or none) back - "
			"re-instantiated from a cold memory image, so a stateful module starts over. Unlike wasm.load, this is "
			"heard: the device is on the signal path."),
		{{QStringLiteral("target"), stringProperty()}, {QStringLiteral("plugin"), stringProperty()},
			{QStringLiteral("path"), stringProperty()}},
		{QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("path")},
		{{QStringLiteral("target"), stringProperty()}, {QStringLiteral("plugin"), stringProperty()},
			{QStringLiteral("module"), stringProperty()}, {QStringLiteral("previous_module"), stringProperty()}},
		true, [](const QJsonObject& args) { return handleDeviceLoad(args); });
	cmd.requiresEngine = true;  // a device lives in the project, unlike the host's own sandbox
	registry.registerCommand(cmd);
}

} // namespace

void registerWasmEditCommands(ControlRegistry& registry)
{
	registerWasmLoadCommand(registry);
	registerWasmUnloadCommand(registry);
	registerWasmSetParamCommand(registry);
	registerWasmDeviceLoadCommand(registry);
}

} // namespace lmms
