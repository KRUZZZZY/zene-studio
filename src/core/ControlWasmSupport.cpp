/*
 * ControlWasmSupport.cpp - the vocabulary the wasm.* command group's two halves
 *                          share.
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

#include "ControlWasmSupport.h"

#include "ControlRegistry.h"

#include "src/wasm/WasmAbi.h"
#include "src/wasm/WasmSandboxHost.h"

#include <QJsonArray>

#include <cstdint>

namespace lmms
{
namespace control
{

QString wasmDefaultModuleRoot()
{
	return QStringLiteral("wasm-modules");
}

QString wasmText(const std::string& text)
{
	return QString::fromStdString(text);
}

QString wasmNotFound(const QString& id, const QString& why)
{
	return QStringLiteral("%1: %2").arg(id, why);
}

ControlCommand wasmCommand(const QString& verb, const QString& description,
	const QJsonObject& properties, const QJsonArray& requiredList,
	const QJsonObject& resultProperties, bool mutating,
	const std::function<ControlResult(const QJsonObject&)>& handler)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("wasm.") + verb;
	cmd.group = QStringLiteral("wasm");
	cmd.verb = verb;
	cmd.description = description;
	cmd.requiresEngine = false;
	cmd.argsSchema = objectSchema(properties, requiredList);
	cmd.resultSchema = objectSchema(resultProperties);
	cmd.mutating = mutating;
	cmd.handler = handler;
	return cmd;
}

const QJsonObject& wasmStateProperty()
{
	static const QJsonObject property = objectProperty();
	return property;
}

QJsonObject wasmStateJson()
{
	wasm::WasmSandboxHost& host = wasm::WasmSandboxHost::instance();

	QJsonObject out;
	out.insert(QStringLiteral("loaded"), host.isLoaded());
	out.insert(QStringLiteral("path"), wasmText(host.modulePath()));
	out.insert(QStringLiteral("format"), wasmText(host.moduleFormat()));
	out.insert(QStringLiteral("fuel_budget"), static_cast<double>(host.fuelBudget()));
	if (!host.isLoaded()) { return out; }

	out.insert(QStringLiteral("channels"), host.declaredChannels());
	out.insert(QStringLiteral("latency"), host.declaredLatency());
	out.insert(QStringLiteral("has_process"), host.hasProcess());
	out.insert(QStringLiteral("memory_bytes"), static_cast<double>(host.memoryBytes()));

	QJsonArray params;
	for (std::uint32_t index = 0; index < wasm::abi::maxParams; ++index)
	{
		params.append(static_cast<double>(host.param(index)));
	}
	out.insert(QStringLiteral("params"), params);
	return out;
}

} // namespace control
} // namespace lmms
