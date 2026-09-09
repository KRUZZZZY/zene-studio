/*
 * StemTrackBuilder.h - turn separated stems into SampleTracks
 *
 * Copyright (c) 2026 LMMS Developers
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

#ifndef LMMS_STEM_TRACK_BUILDER_H
#define LMMS_STEM_TRACK_BUILDER_H

#include <QList>
#include <QString>

#include "StemSeparation/StemTypes.h"
#include "lmms_export.h"

namespace lmms
{

class SampleTrack;
class TimePos;
class TrackContainer;

// Materialises the result of a separation job as new SampleTracks. Runs on the
// GUI thread (it mutates the model), never on the audio thread. Buffers are
// shared with the clips, so no audio data is copied.
class LMMS_EXPORT StemTrackBuilder
{
public:
	// Creates one SampleTrack per stem in the fixed order drums, bass, other,
	// vocals, each carrying a single SampleClip at `position`. The container
	// takes ownership of the tracks. Returns the created tracks, or an empty
	// list (and sets `error`) if any stem buffer is missing.
	static QList<SampleTrack*> createStemTracks(TrackContainer* container,
		const StemSet& stems,
		const TimePos& position,
		const QString& sourceName,
		QString* error = nullptr);
};

} // namespace lmms

#endif // LMMS_STEM_TRACK_BUILDER_H
