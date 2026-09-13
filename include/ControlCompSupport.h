/*
 * ControlCompSupport.h - the helpers the comp.* command group shares between its
 *                       take half (ControlCommandsComp.cpp) and its composite
 *                       half (ControlCommandsCompEdits.cpp).
 *
 * They live in one translation unit rather than in the file that registers the
 * group, for the same reason include/ControlRackSupport.h has one: a helper the
 * two halves would otherwise derive twice is exactly the drift this split
 * prevents, and the composite block (`compositeState`) is reported by five
 * commands that must not disagree about what a comp is.
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

#ifndef LMMS_CONTROL_COMP_SUPPORT_H
#define LMMS_CONTROL_COMP_SUPPORT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "TakeLane.h"
#include "lmms_export.h"

namespace lmms
{

class Clip;
class Track;
struct ControlResult;

namespace control
{

/*! Forward declaration, in ITS OWN namespace: `ClipRef` is
 *  `lmms::control::ClipRef` (include/ControlEdit.h). Declaring it one namespace
 *  up makes every unqualified `ClipRef` in a file that includes both headers
 *  ambiguous, which is a compile error, not a link error. */
struct ClipRef;

//! clip-<n> of \p clip, or an empty string when it is not in the song.
LMMS_EXPORT QString takeClipId(const Clip* clip);

/*! Resolves \p id to a clip this release can hold as a take.
 *
 *  Take lanes carry AUDIO takes in this release (docs/CLIP-CAPTURE-DESIGN.md §3:
 *  the container is type-agnostic, the comp path built here is the audio one), so
 *  a MIDI clip is refused with a typed error instead of being tagged with a lane
 *  whose tag nothing would persist (the same refusal shape clip.set_fade uses).
 */
LMMS_EXPORT bool resolveTakeClip(const QString& id, ClipRef* ref, ControlResult* error);

//! One lane: its index, its name and the ids of the takes assigned to it.
LMMS_EXPORT QJsonObject laneState(const Track* track, const TakeLane& lane);
//! Every lane of \p track, in index order.
LMMS_EXPORT QJsonArray lanesState(const Track* track);
//! One composite segment: begin, end, lane, srcpos and the length it covers.
LMMS_EXPORT QJsonObject segmentState(const TakeLaneSegment& seg);
/*! What the composite resolves to, one entry per segment: the take clip that
 *  supplies the segment's first tick and the source frame it starts at, with
 *  `status` = "bound" or "unresolved". An unresolved entry is reported, never
 *  guessed at: it is exactly the case where the comp names a lane with no take
 *  covering that tick. */
LMMS_EXPORT QJsonArray resolvedState(const Track* track);
/*! The composite block every comp.* result carries: the ordered segment list,
 *  its resolution, its span and whether the track is comped at all. */
LMMS_EXPORT QJsonObject compositeState(const Track* track);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_COMP_SUPPORT_H
