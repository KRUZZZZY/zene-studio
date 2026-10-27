/*
 * DrumPadState.h - the state contract of one drum-rack pad (Q050-066)
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

#ifndef LMMS_DRUM_PAD_STATE_H
#define LMMS_DRUM_PAD_STATE_H

#include <vector>

#include <QJsonObject>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

class InstrumentTrack;
class Track;
class TrackFolder;

namespace control
{

/*! Q050-066 stage 1: what one drum-rack pad IS, in one place, so every reader - the read-back command,
 *  the pad panel and (later stages) a transfer to another pad model - agrees on the fields. A pad is a
 *  sampler track (AudioFileProcessor) inside the rack's folder, as track.drum_rack builds it; nothing here
 *  is stored twice: every field is read from the track, its sampler and its mixer channel, which the
 *  project file already carries. So a rack saved before this contract existed reads back the same. */
struct DrumPadPlayback
{
	double amp = 100.0;          //!< the sampler's amplification, percent
	double start = 0.0;          //!< start point, 0..1 of the sample
	double end = 1.0;            //!< end point, 0..1
	double loopStart = 0.0;      //!< loop-back point, 0..1
	int loop = 0;                //!< 0 off, 1 forward, 2 ping-pong
	bool reversed = false;
	bool stutter = false;
	int interpolation = 1;       //!< 0 none, 1 linear, 2 sinc
	double attack = 0.0;         //!< attack, ms
	double release = 3.0;        //!< release, ms
	int voiceLimit = 64;         //!< the most notes the pad plays at once (1..64)
};

struct DrumPadState
{
	QString track;     //!< the pad's track, trk-<n>
	QString name;      //!< the pad's (track's) name
	QString sample;    //!< the sample file, absolute; empty for sample data embedded in the project
	int key = 36;      //!< trigger key 0..127: the note that plays the sample at its own pitch (the base note)
	QString channel;   //!< the pad's mixer channel (its chain), ch-<n>
	QString sendsTo;   //!< the one channel the pad's channel sends to (the rack's bus), ch-<n>; empty unless exactly one
	DrumPadPlayback playback;

	QJsonObject toJson() const;
};

//! The sampler's plug-in name a pad plays through.
LMMS_EXPORT QString drumPadSampler();

//! Whether @a track is a pad: an instrument track playing through the sampler.
LMMS_EXPORT bool isDrumPad(const Track* track);

//! @a track's pad state; false (and @a out untouched) when it is not a pad.
LMMS_EXPORT bool readDrumPad(InstrumentTrack* track, DrumPadState* out);

//! The pads of @a folder in track order (its other children are not pads and are left out).
LMMS_EXPORT std::vector<DrumPadState> drumPadsOf(const TrackFolder* folder);

/*! The whole rack's state as track.drum_rack_get_state answers it: the folder, its name, the bus (the
 *  channel its pads send to when they all send to one; empty otherwise) and the pads. */
LMMS_EXPORT QJsonObject drumRackState(const TrackFolder* folder);

} // namespace control

} // namespace lmms

#endif // LMMS_DRUM_PAD_STATE_H
