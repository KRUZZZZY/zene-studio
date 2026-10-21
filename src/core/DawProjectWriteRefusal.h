/*
 * DawProjectWriteRefusal.h - the writer's refusals of a tempo and a metre.
 *
 * An internal header (src/core, not include/): only DawProjectWrite.cpp uses
 * these, and they exist so that file stays under gate 7's line cap. Each
 * returns the reason the writer refuses the model, or an empty string when the
 * model is one the engine can read back. Defined in DawProjectWriteRefusal.cpp.
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
#ifndef LMMS_DAWPROJECT_WRITE_REFUSAL_H
#define LMMS_DAWPROJECT_WRITE_REFUSAL_H

#include <QString>

#include "DawProjectInterchange.h"

namespace lmms
{
namespace interchange
{

//! A session tempo or tempo-map point the engine cannot read back: outside
//! the engine's own bounds, or not finite.
QString tempoRefusal(const DawProjectModel& model);

//! A time signature or metre point the engine cannot read back: a numerator
//! outside the reader's 1..32, or a denominator that is not a power of two.
QString meterRefusal(const DawProjectModel& model);

//! A note whose channel, key, velocity or release velocity the reader would
//! reject: outside the supported MIDI/velocity ranges, or non-finite.
QString noteFieldsRefusal(const DawProjectNote& note);

//! Mixer strips and track strips whose scalar parameters the reader would
//! reject: outside their supported ranges, or non-finite.
QString scalarFieldsRefusal(const DawProjectModel& model);

} // namespace interchange
} // namespace lmms

#endif // LMMS_DAWPROJECT_WRITE_REFUSAL_H
