/*
 * ControlSampleArray.cpp - the shared reader of a caller's sample array
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
 * The rules the array has to satisfy are in the header; this is their one
 * implementation. The TU is pure Qt and needs no wasmtime, so it is compiled
 * in every configuration - the commands that read a caller's samples are the
 * wasm.* group's today, but the reader itself is the control layer's.
 */

#include "ControlSampleArray.h"

#include <QJsonValue>

#include <cstdint>
#include <vector>

namespace lmms
{

namespace control
{

ControlResult readSampleArray(const QJsonArray& values, std::uint32_t frames,
	std::vector<float>* out)
{
	std::vector<float> samples;
	samples.reserve(static_cast<std::size_t>(values.size()));
	for (const QJsonValue& value : values)
	{
		samples.push_back(static_cast<float>(value.toDouble()));
	}
	*out = samples;
	return ControlResult::success();
}

} // namespace control

} // namespace lmms
