/*
 * BounceInPlace.h - render a track's (or a region's) output to audio, the
 *                   engine half of freeze / bounce-in-place.
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

#ifndef LMMS_BOUNCE_IN_PLACE_H
#define LMMS_BOUNCE_IN_PLACE_H

#include <QString>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class Track;

/*! The offline render behind bounce-in-place and freeze.
 *
 *  WHAT IT RENDERS, AND WHY IT IS A CHILD PROCESS
 *  ----------------------------------------------
 *  `renderTrack` produces ONE track's own contribution to the mix - its
 *  devices, its fader, its pan and its sends, which is what the engine's stem
 *  export already means by a stem - by serialising the session, muting every
 *  other track in the serialised copy, and running the product's own CLI render
 *  (`zene render <file> -o <out>`) on that copy.
 *
 *  It is a CHILD PROCESS for the reason ControlCommandsProject.cpp's
 *  render.render documents: rendering in this process would drive the running
 *  instance's audio engine (ProjectRenderer calls audioEngine()->
 *  startProcessing()/stopProcessing(), which owns the device thread) and leave
 *  the instance unable to quit cleanly. The session is serialised first, so the
 *  render is the audio truth of exactly the state the caller has built, and the
 *  running instance is not touched at all - which is what makes `bounce.in_place`
 *  a `not_mutating` command in the SPEC A16 table.
 *
 *  Range: `end <= start` means the whole track. A range is rendered by
 *  rendering the track and trimming the result to the range's frame window
 *  (`trimWav`), so the returned file covers exactly the ticks asked for.
 */
class LMMS_EXPORT BounceInPlace
{
public:
	/*! The timeline window a render covers, in ticks. `end <= start` means the
	 *  whole track: the render stops at the track's own length. */
	struct Range
	{
		tick_t start = 0;
		tick_t end = 0;

		bool coversWholeTrack() const { return end <= start; }
	};

	struct Result
	{
		bool ok = false;
		QString error;      //!< why it failed, empty when it did not
		QString path;       //!< the audio file written
		Range range;        //!< the ticks the file covers
		int sampleRate = 0;
		qint64 frames = 0;  //!< frames in the written file
		qint64 bytes = 0;
		QString sha256;

		//! The typed refusal text for a caller that has its own error type.
		static Result failure(const QString& why);
	};

	/*! Renders \a track's output to \a outPath (WAV, 44100 Hz by default).
	 *
	 *  The track is rendered UNMUTED even when the session has it muted - a
	 *  bounce of a muted track is its audio, not silence - and every other
	 *  track is muted in the serialised copy only. The session in this process
	 *  is not modified.
	 *
	 *  `range.coversWholeTrack()` renders the whole track; a range renders the
	 *  track and trims the result to the range's frame window.
	 */
	static Result renderTrack(Track* track, const QString& outPath, const Range& range,
			int sampleRate = 44100);

	//! The whole-track bounce: the same call with an empty range. Declared here
	//! and defined in the .cpp because a default argument of `Range{}` is
	//! ill-formed for a NESTED class (its default member initializers are not
	//! complete until the end of the enclosing class).
	static Result renderTrack(Track* track, const QString& outPath);

	/*! Copies frames [\a startFrame, \a startFrame + \a frameCount) of a WAV to
	 *  a new WAV, clamping the window to the source. Returns false and fills
	 *  \a error when either file cannot be opened. \a framesWritten receives
	 *  the number of frames actually written.
	 *
	 *  Public because it is the whole of the range half of a bounce and is
	 *  directly testable without a render. */
	static bool trimWav(const QString& source, const QString& target,
			qint64 startFrame, qint64 frameCount, qint64* framesWritten, QString* error);

	//! Frames in a WAV file, -1 when it is not a readable WAV. Used for the
	//! `frames` a render reports, measured from the file rather than asserted.
	static qint64 wavFrameCount(const QString& path);

	//! sha256 of a file, empty when it cannot be read.
	static QString sha256OfFile(const QString& path);
};

} // namespace lmms

#endif // LMMS_BOUNCE_IN_PLACE_H
