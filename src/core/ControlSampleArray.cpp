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

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace lmms
{

namespace control
{

ControlResult readSampleArray(const QJsonArray& values, std::uint32_t frames,
	std::vector<float>* out)
{
	// A render or a process reads at most `frames` samples, so a longer array
	// is samples nobody reads - and it is reserved and copied whole before
	// that is noticed.
	if (static_cast<quint64>(values.size()) > static_cast<quint64>(frames))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the sample array holds %1 samples but the call reads at most %2")
				.arg(static_cast<qlonglong>(values.size()))
				.arg(static_cast<qulonglong>(frames)));
	}

	const double maxFloat = static_cast<double>(std::numeric_limits<float>::max());
	std::vector<float> samples;
	samples.reserve(static_cast<std::size_t>(values.size()));
	for (const QJsonValue& value : values)
	{
		// toDouble() would turn a string or a bool into a 0 the caller never
		// sent, and the render would run on a stimulus that is not the one
		// that was asked for.
		if (!value.isDouble())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("every sample must be a number"));
		}
		const double sample = value.toDouble();
		// The samples are floats: a double outside the float range (1e300) and
		// an infinity both convert to an infinity, which is undefined
		// behaviour rather than a loud sample.
		if (!std::isfinite(sample) || sample < -maxFloat || sample > maxFloat)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("every sample must be finite and within the float range"));
		}
		samples.push_back(static_cast<float>(sample));
	}
	*out = samples;
	return ControlResult::success();
}

ControlResult readInputArgument(const QJsonObject& args, std::uint32_t frames,
	std::vector<float>* out)
{
	// An ABSENT argument is not an empty array: the caller sent no samples
	// and the caller's own stimulus (the ramp) stands. An EMPTY array is a
	// present argument with no samples in it - silence - and it is read like
	// any other.
	if (!args.contains(QStringLiteral("input"))) { return ControlResult::success(); }
	return readSampleArray(args.value(QStringLiteral("input")).toArray(), frames, out);
}

} // namespace control

} // namespace lmms
