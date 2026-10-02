/*
 * TrackMixModels.cpp - a track's own volume and pan models, whatever kind of track it is
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

#include "TrackMixModels.h"

#include "InstrumentTrack.h"
#include "SampleTrack.h"

namespace lmms
{

FloatModel* trackVolumeModel(Track* track)
{
	if (auto* instrument = dynamic_cast<InstrumentTrack*>(track)) { return instrument->volumeModel(); }
	if (auto* sample = dynamic_cast<SampleTrack*>(track)) { return sample->volumeModel(); }
	return nullptr;
}

FloatModel* trackPanningModel(Track* track)
{
	if (auto* instrument = dynamic_cast<InstrumentTrack*>(track)) { return instrument->panningModel(); }
	if (auto* sample = dynamic_cast<SampleTrack*>(track)) { return sample->panningModel(); }
	return nullptr;
}

} // namespace lmms
