/*
 * ControlBounceSupport.h - what the bounce and freeze commands share
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

#ifndef LMMS_CONTROL_BOUNCE_SUPPORT_H
#define LMMS_CONTROL_BOUNCE_SUPPORT_H

#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "BounceInPlace.h"
#include "Track.h"
#include "lmms_export.h"

namespace lmms
{

class Track;

namespace control
{

//! `start`/`end` (ticks, together or neither) as a range; the refusal text, or empty when fine.
//! \a required: a region is needed (freeze.region).
LMMS_EXPORT QString parseRange(const QJsonObject& args, bool required, BounceInPlace::Range* range);
//! `out`, or the default `<project>-bounces/<trk>-<verb>[-<start>-<end>].wav`.
LMMS_EXPORT QString resolveOutPath(const QJsonObject& args, Track* track, const QString& verb,
	const BounceInPlace::Range& range);
//! A render's reply fields (path, rate, frames, bytes, sha256, the ticks it covers).
LMMS_EXPORT QJsonObject renderJson(const BounceInPlace::Result& rendered);
//! Region freeze preparation: cuts the track's clips that cross the region's edges at those edges and returns
//! the clips now inside it (the ones the freeze mutes); \a cut lists the clips that were cut, as they were.
LMMS_EXPORT std::vector<Track::FrozenTake::MutedClip> isolateFreezeRegion(Track* track,
	const BounceInPlace::Range& range, QJsonArray* cut);
//! Adds `audio_missing` and `recovery` to a frozen-take report when the take's file is gone: the ways out are
//! project.missing_assets / project.relink (point it at the moved file) and freeze.unfreeze (undoable).
LMMS_EXPORT void insertFrozenRecovery(Track* track, QJsonObject* report);
//! 0.5.0 L12 (ED-207, WF-045): a new audio track named @a name playing the file @a path from tick @a at,
//! moved to @a index in the song's list (left at the end when @a index is negative); nullptr when it
//! could not be made (ControlCommandsTrackFlatten.cpp).
LMMS_EXPORT Track* landAudioTrack(const QString& name, const QString& path, tick_t at, int index);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_BOUNCE_SUPPORT_H
