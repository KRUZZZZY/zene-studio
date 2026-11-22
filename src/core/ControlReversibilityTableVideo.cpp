/*
 * ControlReversibilityTableVideo.cpp - the video.* group's rows of the SPEC A16 table
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
 *
 */
#include "ControlReversibility.h"

namespace lmms
{
namespace control
{

namespace
{

using RC = ReversibilityClass;

//! A literal row: R(id, class, reversible, reason, mechanism, fallback).
#define R(id, cls, rev, reason, mechanism, fallback) \
	{ id, cls, reason, mechanism, fallback, rev, nullptr }

const ReversibilityRow kVideoRows[] = {
	R("video.attach", RC::TrueInverse, true, "it makes the project's video track one clip of the given file, replacing "
		"every clip on it (<video>)",
		"action checkpoint: the recorded undo step puts the previous clip list back (none, or every replaced clip, ids "
		"included)", ""),
	R("video.detach", RC::TrueInverse, true, "it takes every video clip off the project (the files are left alone)",
		"action checkpoint: the recorded undo step puts the same clips back at the same offsets, ids included", ""),
	R("video.clip_add", RC::TrueInverse, true, "it adds a video clip to the project's video track (<video>/<clip>)",
		"action checkpoint: the recorded undo step puts the previous clip list back", ""),
	R("video.clip_remove", RC::TrueInverse, true, "it removes one video clip from the project's video track",
		"action checkpoint: the recorded undo step puts the previous clip list back, ids included", ""),
	R("video.get_state", RC::NotMutating, false, "it reads the project's video track. Nothing is written", "no write", ""),
	R("video.set_offset", RC::TrueInverse, true, "it moves the song tick the named video clip starts at",
		"action checkpoint: the recorded undo step writes the previous offset back", ""),
	R("video.extract_audio", RC::TrueInverse, true, "it writes the video's sound to a WAV and adds an audio track holding it at "
		"the video's offset",
		"action checkpoint: the recorded undo step removes the track (control::recordCreatedTrack); its redo builds it again. "
		"The WAV stays on disk, as a bounce's render does. With 'background' the reply is an acknowledgement and the same "
		"one step is recorded when the decode completes (none when it fails or is cancelled)", ""),
	R("video.render_frames", RC::NotMutating, false, "it writes PNG images of the video track over a song range into a "
		"folder; the project is not changed", "no project write, as render.render", ""),
	R("video.window_show", RC::NotMutating, false, "it shows or hides the video window: interface state", "no write", ""),
};

#undef R

constexpr int kVideoRowCount = static_cast<int>(sizeof(kVideoRows) / sizeof(kVideoRows[0]));

} // namespace

const ReversibilityRow* reversibilityVideoRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kVideoRowCount; }
	return kVideoRows;
}

} // namespace control
} // namespace lmms
