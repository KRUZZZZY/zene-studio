/*
 * ControlWasmSupport.h - the vocabulary the wasm.* command group's two halves
 *                        share (the read half and the edit half).
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

#ifndef LMMS_CONTROL_WASM_SUPPORT_H
#define LMMS_CONTROL_WASM_SUPPORT_H

#include "ControlRegistry.h"   // ControlCommand / ControlResult, the group's shapes
#include "ControlVocabulary.h" // the shared schema vocabulary

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <string>

namespace lmms
{

namespace control
{

/*! The directory wasm.list enumerates when the caller names none: the one the
 *  build assembles the demo modules into (src/wasm/CMakeLists.txt), resolved
 *  against the process's working directory. Named once, so the default and the
 *  message that explains it cannot drift apart. */
LMMS_EXPORT QString wasmDefaultModuleRoot();

//! A std::string as the wire carries it.
LMMS_EXPORT QString wasmText(const std::string& text);

//! "&lt;command&gt;: &lt;why&gt;" - the shape of every refusal this group returns.
LMMS_EXPORT QString wasmNotFound(const QString& id, const QString& why);

/*! One command of the group. The sandbox needs neither a project nor an audio
 *  device, so every wasm.* command answers before readiness: requiresEngine is
 *  false, set once here instead of six times. \p properties/\p requiredList and
 *  \p resultProperties go straight to control::objectSchema(). */
LMMS_EXPORT ControlCommand wasmCommand(const QString& verb, const QString& description,
	const QJsonObject& properties, const QJsonArray& requiredList,
	const QJsonObject& resultProperties, bool mutating,
	const std::function<ControlResult(const QJsonObject&)>& handler);

//! The schema of the `state` object every wasm.* result carries.
LMMS_EXPORT const QJsonObject& wasmStateProperty();

/*! The hosted module's state, the one object every wasm.* result carries:
 *  whether anything is hosted and its path/format/fuel budget, and - when one
 *  is - the ABI values the host has read from it (channels, latency, whether it
 *  exports process(), its linear memory size) plus all 16 parameter slots.
 *
 *  The ABI values are the host's own after the clamp (ABI doc section 4.1), not
 *  what the module's globals literally said, because the clamp is what the host
 *  acts on. */
LMMS_EXPORT QJsonObject wasmStateJson();

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_WASM_SUPPORT_H
