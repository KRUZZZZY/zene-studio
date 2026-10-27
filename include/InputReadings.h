/*
 * InputReadings.h - the capture channels' levels and the inputs of the record-armed tracks, read together
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

#ifndef LMMS_INPUT_READINGS_H
#define LMMS_INPUT_READINGS_H

#include <vector>

#include <QJsonObject>
#include <QString>

#include "InputMeter.h"
#include "lmms_export.h"

namespace lmms
{

class Track;

/*! Q050-340: one look at the inputs - every capture channel's peak since the reader's last look
 *  (inputmeter::takeChannels) and, for every record-armed route, the peak of the input channel it selected,
 *  with the track it records (route n is the song's n-th track, track.set_arm's rule). Built on the
 *  interface thread (the Large Meters' input meters) or the control thread (meter.inputs), each from its own
 *  reader, so neither empties the other. Independent of input monitoring: the level is the input's. */
namespace inputreadings
{

struct Armed
{
	int route = 0;
	Track* track = nullptr;  //!< the song's track at the route's position; nullptr when there is none
	int inputChannel = 0;
	float peak = 0.f;  //!< linear; 0 when the selected channel delivered nothing (silence)
};

struct Readings
{
	int channelCount = 0;  //!< capture channels the newest input carried (0: none yet)
	std::vector<float> peaks;  //!< one per capture channel, linear
	std::vector<Armed> armed;
};

LMMS_EXPORT Readings take(inputmeter::Reader reader);
//! A peak in dB, -120 for silence (meter.input's rule).
LMMS_EXPORT double peakDb(float peak);
//! The readings as meter.inputs answers them.
LMMS_EXPORT QJsonObject toJson(const Readings& readings);

} // namespace inputreadings

} // namespace lmms

#endif // LMMS_INPUT_READINGS_H
