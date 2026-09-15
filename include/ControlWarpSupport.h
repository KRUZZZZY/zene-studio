/*
 * ControlWarpSupport.h - the helpers the warp.* command group shares between
 *                        its read half (ControlCommandsWarp.cpp) and its
 *                        edit half (ControlCommandsWarpEdit.cpp).
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

#ifndef LMMS_CONTROL_WARP_SUPPORT_H
#define LMMS_CONTROL_WARP_SUPPORT_H

#include <array>
#include <span>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlVocabulary.h" // the shared schema + id vocabulary
#include "LmmsTypes.h"
#include "SampleClip.h"        // WarpTempoMode + the clip's warp accessors
#include "WarpMarkers.h"

namespace lmms
{

struct ControlResult;

namespace control
{

struct ClipRef;

//! The largest integer the schema subset can carry: ControlVocabulary's
//! integerProperty() takes ints, so a bound past this cannot be written down.
//! A source frame that large is ~13.5 hours of 44.1 kHz audio, so no real clip
//! reaches it; a frame the schema cannot express is refused as invalid_args.
constexpr int MaxSchemaInteger = 2147483647;

//! One marker set as a stack array: the engine's own fixed capacity, so nothing
//! on this path allocates (docs/CONVENTIONS.md, realtime rule I8).
using MarkerArray = std::array<WarpMarker, WarpMarkers::MaxMarkers>;

//! The wire name of a tempo mode.
QString tempoModeName(WarpTempoMode mode);

/*! The wire name of a stretch mode (row 30 of the 0.3.0 list): how the clip
 *  renders a rate change. "resample" is the historical one — the pitch moves
 *  with the rate — and "preserve_pitch" is the WSOLA stretch. */
QString stretchModeName(WarpStretchMode mode);

//! One marker as the wire reports it: its position in the engine's order, the
//! source frame it pins and where that frame lands.
QJsonObject markerJson(const WarpMarker& marker, int index);

//! The clip's warp state: the read-back every command of the group returns, so
//! a caller never has to guess what the map became.
QJsonObject warpState(const ClipRef& ref, const SampleClip& clip);

//! The transaction's before-state. Bounded BY CONSTRUCTION: at most the
//! engine's own MaxMarkers (128) marker pairs, so a record can never approach
//! MaxTransactionBytes.
QJsonObject warpBefore(const ClipRef& ref, const SampleClip& clip);

//! The recorded inverse: warp.set issued with the before-state's own values, so
//! the inverse is both a re-issuable command and what control.undo reaches
//! through the clip's journal checkpoint.
QJsonObject warpInverse(const QJsonObject& before);

//! Resolves the clip the arguments name and insists it is a SampleClip: warp
//! markers are a SampleClip's child element, so a MIDI or pattern clip is a
//! typed refusal rather than an empty success.
SampleClip* resolveSampleClip(const QJsonObject& args, ClipRef* ref, ControlResult* error);

//! Reads one {source_frame, offset_ticks} pair out of \a value.
bool readMarker(const QJsonValue& value, WarpMarker* marker, ControlResult* error);

//! Copies the clip's markers into the stack array, reporting how many.
MarkerArray markerArrayOf(const WarpMarkers& warp, int* count);

//! Index of the marker pinned to \a sourceFrame, or -1. A source frame is the
//! marker's identity on the audio and the set is strictly increasing in it, so
//! it is unique - which is why the verbs address a marker this way and not by a
//! position that shifts when a sibling is added.
int indexOfSourceFrame(const WarpMarkers& warp, f_cnt_t sourceFrame);

//! The offsets the marker at \a index may take and keep the set strictly
//! increasing in timeline position (the engine's own rule, stated so a refusal
//! can name the permitted interval).
void offsetBounds(const WarpMarkers& warp, int index, tick_t* low, tick_t* high);

/*! Applies \a markers to \a clip, or clears the map when \a count is 0.
 *
 *  The set is proved against the engine's OWN value type (WarpMarkers::set)
 *  before anything is written, so a refused set leaves the clip exactly as it
 *  was: a command that half-applied would need a second command to describe its
 *  failure (I4, and the reason clip.split refuses an out-of-range cut up front).
 */
bool applyMarkers(SampleClip* clip, const MarkerArray& markers, int count, ControlResult* error);

//! The schema of every read-back this group returns; \a extra adds the keys one
//! command reports on top of it.
QJsonObject warpStateSchema(QJsonObject extra = QJsonObject());

//! The two schema primitives the group needs: a closed set of tempo mode names
//! (the same inline enum form track.add uses for its track types) and the marker
//! object/array shapes warp.set accepts.
QJsonObject tempoModeProperty();
//! The closed set of stretch mode names (row 30).
QJsonObject stretchModeProperty();
QJsonObject markerProperty();
QJsonObject markersProperty();
QJsonObject offsetTicksProperty();

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_WARP_SUPPORT_H
