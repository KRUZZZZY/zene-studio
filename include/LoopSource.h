/*
 * LoopSource.h - what an imported loop was detected to be, and what its import applied
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
/*! 0.5.0 L05 (Q050-093): a sample clip made by clip.import_file remembers what the detector found in its file
 *  (source tempo and key) and which loop mode its import took. Control-thread state only: the playback path never
 *  reads it - what plays is the clip's warp state and transpose, which the import set from it. Saved as an
 *  additive `<loopsource>` child of the clip; a clip without one (every clip before this feature, and every clip
 *  not made by an import) has `analysed` false and serialises exactly as it did. */
#ifndef LMMS_LOOP_SOURCE_H
#define LMMS_LOOP_SOURCE_H

#include <QString>

namespace lmms
{

struct LoopSource
{
	bool analysed = false;          //!< an import ran the detector on this clip's file
	bool tempoFound = false;
	double bpm = 0.0;               //!< the detector's own estimate
	bool keyFound = false;
	int tonicPitchClass = -1;       //!< 0 = C .. 11 = B
	QString tonicName;
	QString scaleName;
	bool followProject = false;     //!< the import's loop mode: follow the project, or keep unstretched
	bool tempoFollowed = false;     //!< the clip was set to lead at its source tempo
	bool keyFollowed = false;       //!< the clip was transposed to the project's tonic
	int transposeApplied = 0;       //!< the semitones the import set (0 unless keyFollowed)

	bool operator==(const LoopSource&) const = default;
};

} // namespace lmms

#endif // LMMS_LOOP_SOURCE_H
