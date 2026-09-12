/*
 * ControlVocabulary.h - the one place the command surface's shared vocabulary
 *                       lives: the JSON-schema subset every command group
 *                       builds its argsSchema/resultSchema from, and the
 *                       trk-/clip-/note-/ch-/fx-/dev- id grammar.
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

#ifndef LMMS_CONTROL_VOCABULARY_H
#define LMMS_CONTROL_VOCABULARY_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

class Track;

namespace control
{

// ---------------------------------------------------------------------------
// The JSON-schema subset (AGENT-TOOLING.md #4). The registry validates exactly
// this much: type, properties, required, additionalProperties, minimum,
// maximum, enum. Anything else in a schema is ignored.
//
// ONE definition, in ControlVocabulary.cpp. This is deliberate: the four merge
// repairs before this file existed were each two lanes re-deriving the same
// helper, and the last one was a duplicate-symbol link failure. A command group
// that needs a schema primitive adds it HERE, not as a file-local copy.
// ---------------------------------------------------------------------------

LMMS_EXPORT QJsonObject objectSchema(QJsonObject properties = {}, QJsonArray required = {});
LMMS_EXPORT QJsonObject stringProperty();
LMMS_EXPORT QJsonObject booleanProperty();
LMMS_EXPORT QJsonObject numberProperty();
//! A bare integer property, unbounded.
LMMS_EXPORT QJsonObject integerProperty();
//! An integer property with the bounds the registry enforces.
LMMS_EXPORT QJsonObject integerProperty(int minimum, int maximum);
LMMS_EXPORT QJsonObject arrayProperty();
//! A bare {"type":"object"} property, for a free-form object result.
LMMS_EXPORT QJsonObject objectProperty();
//! A tick position: an integer that cannot be negative.
LMMS_EXPORT QJsonObject tickProperty();

// ---------------------------------------------------------------------------
// The id grammar (AGENT-TOOLING.md #4). Every formatter is
// "<prefix>-<decimal>"; idToIndex() parses it back.
//
// NOTE (SPEC-stable-ids.md): whether the number a formatter is handed is a
// container position or a creation-assigned persistent id is decided by the
// CALLER, not here. The formatter only knows the grammar.
// ---------------------------------------------------------------------------

//! "trk-<n>" for a Song track index or track id (see SPEC-stable-ids.md).
LMMS_EXPORT QString trackId(int index);
/*! The stable "trk-<n>" id of \a track.
 *
 * This is the form every state emitter must use: since SPEC-stable-ids.md the
 * number is the track's creation-assigned id (Track::id()), NOT its position in
 * Song::tracks(). Deriving it from a loop index is the defect the spec exists to
 * remove, so the conversion lives in one place and callers pass the object.
 * A nullptr answers an empty string (an object with no track).
 */
LMMS_EXPORT QString trackIdOf(const Track* track);
//! "clip-<n>" for a clip's arrangement ordinal.
LMMS_EXPORT QString clipId(int ordinal);
//! "note-<n>" for a note's index in its clip's note list.
LMMS_EXPORT QString noteId(int index);
//! "ch-<n>" for a mixer channel index.
LMMS_EXPORT QString channelId(int index);
//! "dev-<n>" for an index in the build's device catalogue (plugin.list).
LMMS_EXPORT QString deviceId(int index);
//! "fx-<n>" for a device instance's index in its target's chain.
LMMS_EXPORT QString effectId(int index);
//! Parses "<prefix>-<n>"; returns -1 when malformed.
LMMS_EXPORT int idToIndex(const QString& id, const QString& prefix);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_VOCABULARY_H
