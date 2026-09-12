/*
 * SampleWindow.h - the authored source window of a clip
 *
 * Copyright (c) 2026 Zene Studio developers
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

#ifndef LMMS_SAMPLE_WINDOW_H
#define LMMS_SAMPLE_WINDOW_H

#include <algorithm>

#include "LmmsTypes.h"

namespace lmms
{

/*! The authored window of a clip's source, in source frames.
 *
 *  `sourceIn` is the first source frame the clip plays and `sourceOut` is one
 *  past the last, so the window is the half-open range [sourceIn, sourceOut). A
 *  clip that plays its whole source has `sourceOut == bufferFrames`, which is
 *  what every project written before the clip-and-capture wave (task #611,
 *  docs/CLIP-CAPTURE-DESIGN.md §2.2) stores.
 *
 *  Slice 0 makes this authored state: the playback path reads it and never
 *  writes it (`SampleClip::sampleWindow()` is the only writer), and the frames
 *  this window names are what a playback pass renders.
 */
struct SampleWindow
{
	f_cnt_t sourceIn = 0;
	f_cnt_t sourceOut = 0;

	SampleWindow() = default;
	SampleWindow(f_cnt_t in, f_cnt_t out) : sourceIn(in), sourceOut(out) { }

	//! The whole source: the default, and what a clip without a trim has.
	static SampleWindow full(f_cnt_t bufferFrames) { return { 0, bufferFrames }; }

	/*! The window clamped into [0, bufferFrames].
	 *
	 *  I4: the caller rejects an empty result rather than clamping an edit into
	 *  something the user did not ask for, so this is the one place the well-
	 *  formedness rule is expressed.
	 */
	static SampleWindow clamped(f_cnt_t sourceIn, f_cnt_t sourceOut, f_cnt_t bufferFrames)
	{
		const auto in = std::min(sourceIn, bufferFrames);
		const auto out = std::clamp(sourceOut, in, bufferFrames);
		return { in, out };
	}

	//! Source frames in the window; never underflows, so `length()` is safe to
	//! multiply into a play-handle length even for a degenerate window.
	f_cnt_t length() const { return sourceOut > sourceIn ? sourceOut - sourceIn : 0; }

	//! A window with no frames in it is not a playable window.
	bool empty() const { return sourceOut <= sourceIn; }

	bool operator==(const SampleWindow& other) const
	{
		return sourceIn == other.sourceIn && sourceOut == other.sourceOut;
	}

	bool operator!=(const SampleWindow& other) const { return !(*this == other); }
};

} // namespace lmms

#endif // LMMS_SAMPLE_WINDOW_H
