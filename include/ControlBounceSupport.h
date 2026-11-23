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
//! Why a flatten (track.flatten, or render.queue_bounce's flatten) cannot commit \a track: the refusal text, or
//! empty when it can. Only a track that makes audio has a render to replace it with.
LMMS_EXPORT QString flattenRefusal(const Track* track);
//! A render's reply fields (path, rate, frames, bytes, sha256, the ticks it covers).
LMMS_EXPORT QJsonObject renderJson(const BounceInPlace::Result& rendered);
//! Region freeze preparation: cuts the track's clips that cross the region's edges at those edges and returns
//! the clips now inside it (the ones the freeze mutes); \a cut lists the clips that were cut, as they were.
LMMS_EXPORT std::vector<Track::FrozenTake::MutedClip> isolateFreezeRegion(Track* track,
	const BounceInPlace::Range& range, QJsonArray* cut);
/*! Cluster 3971: what `mute_sources` mutes for a RANGE bounce, so the bounced region is replaced and not the
 *  whole source tracks. \a tracks' pre-mute state is recorded as ONE undo step (the caller's landing action
 *  merges with it).
 *
 *  - An audio track whose audible clips all lie inside [range.start, range.end) is muted as a whole: the
 *    bounce replaces its entire output.
 *  - Otherwise the region is replaced: the clips that cross the range's edges are cut there and every piece
 *    inside is muted, so audio before and after the range keeps playing. Automation and hidden-automation
 *    tracks are treated the same way at clip level, so their automation outside the range keeps running
 *    (muting such a track as a whole would switch the lane off outside the bounce as well).
 *  - A FROZEN track's take plays instead of its clips, so the take is rewritten with its [range overlap]
 *    frames silent (the rest of the take untouched) and installed as the same take window; the track keeps
 *    playing the take outside the range and is silent only inside it.
 *  - A folder or neutral row carries no audio of its own and is muted as a whole, as before.
 *
 *  Returns the typed refusal (nothing recorded, nothing muted) when a frozen take cannot be rewritten;
 *  empty on success. Call only after the render succeeded. */
LMMS_EXPORT QString muteRangeSources(const std::vector<Track*>& tracks, const BounceInPlace::Range& range);
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
