/*
 * ControlTrackRouting.h - where a new track plays: an output, or a mixer channel of its own (Q050-212)
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

#ifndef LMMS_CONTROL_TRACK_ROUTING_H
#define LMMS_CONTROL_TRACK_ROUTING_H

#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "Track.h"
#include "lmms_export.h"

namespace lmms
{

class MixerChannel;

namespace control
{

/*! What a created track is finished with once it exists. A plain value, so a redo builds an equal track. */
struct NewTrackSpec
{
	QString name;        //!< empty: the type's default name
	QString instrument;  //!< empty: no plug-in
	QString output;      //!< a ch-<n> to play into; empty: the channel of its own, or the default route
	bool ownChannel = false;  //!< app/autotrackchannel was on when it was asked for, and no output was named
};

//! The preference app/autotrackchannel (off unless set).
LMMS_EXPORT bool autoTrackChannelEnabled();

/*! Reads `name`, `instrument` and `output` from @a args for a track of @a type, the auto-channel preference with them.
 *  Refuses, changing nothing, an output on a track type with no channel, an unknown channel, a parallel bus (it takes
 *  no track input, so the track would be silent), and a plug-in this build does not have. */
LMMS_EXPORT bool readNewTrackSpec(const QJsonObject& args, Track::Type type, NewTrackSpec* spec, ControlResult* error);

/*! Loads, names and routes @a track as @a spec says. A channel of its own is made under *@a channelId when that is
 *  already an id (a redo) and writes the id there otherwise. */
LMMS_EXPORT void finishNewTrack(Track* track, const NewTrackSpec& spec, int* channelId);

//! A new regular channel called @a name, under *@a channelId when that is an id, which is written back otherwise.
LMMS_EXPORT MixerChannel* makeNamedChannel(const QString& name, int* channelId);

//! Deletes the channel whose stable id is @a channelId; nothing when no channel has it (or it is -1).
LMMS_EXPORT void deleteChannelWithId(int channelId);

//! Plays @a track into @a channel. Records no undo step of its own: the step that created the track covers it.
LMMS_EXPORT void routeNewTrack(Track* track, const MixerChannel* channel);

//! Adds what a reply says about @a track's destination: `channel`, `channel_name`, `channel_linked`.
LMMS_EXPORT void insertChannelFields(QJsonObject* result, Track* track);

/*! For a creation with no undo step of its own (a plug-in or file opened from a browser): when the preference is on and
 *  @a track is an instrument or sample track of the song, makes and links its channel. Nothing otherwise. */
LMMS_EXPORT void assignNewTrackChannel(Track* track);

/*! recordCreatedTrack() for a command's new track that the preference may give a channel: the channel is made now and
 *  the one undo step covers both. With the preference off it is exactly recordCreatedTrack(). */
LMMS_EXPORT void recordCreatedRoutedTrack(Track* track);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_TRACK_ROUTING_H
