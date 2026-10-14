/*
 * LinkedClipEdits.h - shared content edits for linked clips
 *                     (owner decision 2026-10-09, docs/LINKED-CLIPS.md section 2).
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

#ifndef LMMS_LINKED_CLIP_EDITS_H
#define LMMS_LINKED_CLIP_EDITS_H

#include "lmms_export.h"

namespace lmms
{

class Clip;
class MidiClip;

namespace ClipLinks
{

//! Captures every track holding linked content as ONE undo step, before replacing the edited instance.
LMMS_EXPORT void checkpointGroupTracks(const Clip* source);

/*! The arranger's trim of content writes the re-based notes onto every linked
 *  instance. Their independent windows and offsets are left unchanged.
 *  Mirroring must already be suspended by the caller. */
LMMS_EXPORT int adoptTrimmedContent(MidiClip* trimmed);

} // namespace ClipLinks

} // namespace lmms

#endif // LMMS_LINKED_CLIP_EDITS_H
