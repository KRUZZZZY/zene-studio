/*
 * ClipConsolidate.h - R3.2: render the clips of a sample-track region to one audio file
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

#ifndef LMMS_CLIP_CONSOLIDATE_H
#define LMMS_CLIP_CONSOLIDATE_H

#include <vector>

#include <QString>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class SampleClip;

/*! The render behind clip.consolidate.
 *
 *  WHAT IT RENDERS, AND WHY IT IS NOT BounceInPlace
 *  -------------------------------------------------
 *  Consolidation puts the result back on the SAME track, so it must be the clips' own
 *  audio - their windows, warps, clip gain, fades and the composite's lane gate - and not
 *  the track's output: a bounce carries the track's devices, fader and pan, and those would
 *  then apply twice. Each clip is played through its own SamplePlayHandle, exactly the
 *  object the arrangement plays it through, from the tick the arrangement would start it
 *  at, and the handles are summed at their timeline offsets. Nothing reaches the engine:
 *  the handles are never registered, so the running instance's audio is untouched.
 *
 *  The file is stereo 32-bit float WAV at the engine's output rate, covering exactly
 *  [start, end) ticks.
 */
class LMMS_EXPORT ClipConsolidate
{
public:
	struct Result
	{
		bool ok = false;
		QString error;
		QString path;
		int sampleRate = 0;
		qint64 frames = 0;
	};

	static Result render(const std::vector<SampleClip*>& clips, tick_t start, tick_t end,
		const QString& outPath);
};

} // namespace lmms

#endif // LMMS_CLIP_CONSOLIDATE_H
