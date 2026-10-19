/*
 * ControlSampleArray.h - the shared reader of a caller's sample array
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
 * WHY THIS EXISTS. A caller may hand a command an array of samples - the
 * stimulus a render or a process is driven with. Two commands take one
 * (wasm.process and wasm.render_offline), and each of them used to convert
 * the array with its OWN file-local loop, so the rules the array has to
 * satisfy were written twice and could drift apart.
 *
 * The rules live here, once:
 *   - the array may not be LONGER than the number of frames the call asks
 *     for: a render or a process consumes at most that many, and a longer
 *     array is reserved and copied whole for samples nobody reads;
 *   - every element must be a NUMBER: a string or a bool is not a sample,
 *     and QJsonValue::toDouble() turns one into a 0 the caller never sent;
 *   - every value must be FINITE and within the float range: the samples are
 *     floats, and a double outside that range (1e300) converts to infinity,
 *     which is undefined behaviour rather than a loud sample.
 *
 * A refused array is the CALLER's mistake, so the refusal is an InvalidArgs
 * failure, and the samples are not written at all.
 */

#ifndef LMMS_CONTROL_SAMPLE_ARRAY_H
#define LMMS_CONTROL_SAMPLE_ARRAY_H

#include "ControlRegistry.h" // ControlResult, the shape every refusal takes

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <vector>

namespace lmms
{

namespace control
{

/*! Read \p values as the samples of one render or process.
 *
 *  \p frames is the number of frames the call asks for: the array may not
 *  hold more samples than that. \p out receives the samples, in the caller's
 *  order, when the call is accepted, and is left untouched when it is not.
 *
 *  A refused array is an InvalidArgs failure whose message says why, so the
 *  caller can wrap the reason in the shape its own refusals take.
 */
LMMS_EXPORT ControlResult readSampleArray(const QJsonArray& values, std::uint32_t frames,
	std::vector<float>* out);

/*! Read the 'input' argument of \p args into \p out.
 *
 *  When the argument is ABSENT, \p out is left exactly as the caller set it
 *  (the caller's own default stimulus - a ramp) and the call succeeds. When
 *  it is present, it is read and checked by readSampleArray, and \p out is
 *  either the caller's samples or untouched - a refused array never replaces
 *  what the caller had.
 */
LMMS_EXPORT ControlResult readInputArgument(const QJsonObject& args, std::uint32_t frames,
	std::vector<float>* out);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_SAMPLE_ARRAY_H
