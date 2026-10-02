/*
 * TrackMixModels.h - a track's own volume and pan models, whatever kind of track it is
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

#ifndef LMMS_TRACK_MIX_MODELS_H
#define LMMS_TRACK_MIX_MODELS_H

#include "lmms_export.h"

namespace lmms
{

class FloatModel;
class Track;

//! The track's own volume / pan model (an instrument or a sample track's), or nullptr for a
//! track that has none (automation, pattern, folder). One definition for the Inspector's knobs
//! and the control surface's faders.
LMMS_EXPORT FloatModel* trackVolumeModel(Track* track);
LMMS_EXPORT FloatModel* trackPanningModel(Track* track);

} // namespace lmms

#endif // LMMS_TRACK_MIX_MODELS_H
