/*
 * ContainedCrossfade.h - a crossfade between a clip and one that lies wholly inside it
 *                        (owner decision 2026-10-08, round 2).
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

#ifndef LMMS_CONTAINED_CROSSFADE_H
#define LMMS_CONTAINED_CROSSFADE_H

#include <QString>

#include "ClipEdits.h"
#include "lmms_export.h"
#include "LmmsTypes.h"

namespace lmms
{

class Clip;

/*! A clip lying wholly inside another is crossfaded at BOTH of its ends: at its start the container ducks under
 *  it (the contained clip fades in as the container fades out), and at its end the container comes back (the
 *  contained clip fades out as the container fades in). The two ramps are adjacent and together span the
 *  contained clip: `entry + exit == its length`, so the container's envelope is a V (down over the entry ramp,
 *  up over the exit ramp) and never steps.
 *
 *  It is the offset fades that place the container's two ramps inside it (ClipEdits): its fade-out ends
 *  `fadeOutOffsetTicks` before its end, its fade-in starts `fadeInOffsetTicks` after its start, and it is
 *  unfaded outside them. The container's own edge fades are replaced.
 *
 *  Lengths: with neither given, the exit is the contained clip's own fade-out when it has one, else half of it,
 *  and the entry is the rest. With one given the other is the rest. A pair that does not sum to the contained
 *  clip's length is refused. */
namespace containedcrossfade
{

//! Which of the two clips contains the other, when one lies wholly inside the other and they are not the same
//! span (they share at most one edge). False for a plain overlap.
LMMS_EXPORT bool containment(const Clip* a, const Clip* b, const Clip** container, const Clip** contained);

/*! The entry and exit ramp lengths for \p contained inside \p container. \p given is the length the caller asked
 *  for (0: none) and \p givenIsEntry says which ramp it names. Empty on success, else why not. */
LMMS_EXPORT QString ramps(const Clip& container, const Clip& contained, int given, bool givenIsEntry, int* entry, int* exit);

/*! Writes the four ramps onto \p containerEdits and \p containedEdits (lengths and offsets; the shapes are the
 *  caller's: \p downShape for the two ramps that go down, \p upShape for the two that go up). */
LMMS_EXPORT void apply(const Clip& container, const Clip& contained, int entry, int exit, FadeShape downShape,
	FadeShape upShape, ClipEdits* containerEdits, ClipEdits* containedEdits);

} // namespace containedcrossfade

} // namespace lmms

#endif // LMMS_CONTAINED_CROSSFADE_H
