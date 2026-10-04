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
	R("video.attach", RC::TrueInverse, true, "it attaches a video to the project, replacing an attached one (<video>)",
		"action checkpoint: the recorded undo step puts the previous attachment back (none, or the replaced video)", ""),
	R("video.detach", RC::TrueInverse, true, "it takes the project's video off (the file is left alone)",
		"action checkpoint: the recorded undo step attaches the same video at the same offset again", ""),
	R("video.get_state", RC::NotMutating, false, "it reads the project's video. Nothing is written", "no write", ""),
	R("video.set_offset", RC::TrueInverse, true, "it moves the song tick the video starts at",
		"action checkpoint: the recorded undo step writes the previous offset back", ""),
	R("video.extract_audio", RC::TrueInverse, true, "it writes the video's sound to a WAV and adds an audio track holding it at "
		"the video's offset",
		"action checkpoint: the recorded undo step removes the track (control::recordCreatedTrack); its redo builds it again. "
		"The WAV stays on disk, as a bounce's render does", ""),
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
