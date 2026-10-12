/*
 * ControlProjectTransplant.h - moving parts of one project's document into this project
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

#ifndef LMMS_CONTROL_PROJECT_TRANSPLANT_H
#define LMMS_CONTROL_PROJECT_TRANSPLANT_H

#include <map>
#include <vector>

#include <QDomElement>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "ControlRegistry.h"

namespace lmms::control::transplant
{

/*! What project.import_tracks (WF-036), project.paste_tracks (WF-042) and the reference project's copies
 *  (reference.*, WF-042) share - defined in ControlCommandsImportTracks.cpp, where they began. */

//! Drop the stable ids a copied subtree carries and point its mixer channel at the master; a track's
//! 'folder' goes too unless @a keepFolders (it is another track's id).
void detach(QDomElement element, bool keepFolders = false);

//! Point a copied subtree's media references that resolve against @a sourceDir (the source project's
//! folder) at their files, so they do not resolve against this project's folder instead.
void rebaseMedia(QDomElement element, const QString& sourceDir);

//! The song's <track> elements under a project document's @a content (its <song> element).
QList<QDomElement> songTracks(const QDomElement& content);

/*! project.import_tracks' copy, from tracks already read: the ones @a args' 'tracks' names (indices; all
 *  when it is left out) of @a tracks, detached and media rebased from @a sourceDir, made in this song as
 *  ONE undo step. The reply lists them under @a key ('imported', 'copied'...). */
ControlResult importTrackElements(const QList<QDomElement>& tracks, const QJsonObject& args, const QString& sourceDir,
	const QString& key, const QDomElement& sourceMixer = {});

/*! 0.5.0 L09 (Q050-027): the mixer channels copied tracks play into. A mixer channel number means nothing in
 *  another project, so a track carries its channel as a description - name, colour, bus flag, volume, pan and
 *  its <hardwareio> (direct out, cue sends, hardware insert send and return; stored as interface channel
 *  numbers, so they name the same buses on the same machine). Building the tracks resolves each description
 *  by NAME: the lowest-numbered channel of that name here, or a new channel made from it. */
struct ChannelNeed
{
	QString name;
	QString xml;  //!< the description, a serialized <mixerchannel>
};

//! The tracks to make, as detached documents, with the channel each wants (an index into @a needs, or -1: the master).
struct TrackPlan
{
	QStringList documents;
	std::vector<int> need;
	std::vector<ChannelNeed> needs;
};

//! The element of a saved <track> that holds its 'mixch' (<instrumenttrack>, <sampletrack>), or a null element.
QDomElement channelSettingsOf(const QDomElement& track);

//! Collects the channel descriptions of copied tracks, one per source channel.
class ChannelNeeds
{
public:
	//! @a sourceMixer: the source project's <mixer> element (null: every track plays into the master).
	explicit ChannelNeeds(const QDomElement& sourceMixer, TrackPlan* plan) : m_mixer(sourceMixer), m_plan(plan) {}
	/*! The need of @a track, an instrument or sample track whose source channel number is its @a attribute
	 *  (in its settings element, channelSettingsOf; 'mixch' in a project file); that attribute is removed again when it is not 'mixch'. -1 when it plays into the
	 *  master or its channel is not described. */
	int noteFor(QDomElement track, const QString& attribute);

private:
	QDomElement m_mixer;
	TrackPlan* m_plan;
	std::map<int, int> m_seen;  //!< source channel number -> index in the plan's needs
};

//! The description of the live channel @a num (a <mixerchannel num=...> element appended to @a parent), for the clipboard.
void describeLiveChannel(QDomElement parent, int num);

//! For the clipboard: record the channel the saved @a track plays into - its number in @a attribute and its description
//! in @a mixer - before detach() points the track at the master. Tracks that choose no channel are left alone.
void noteLiveChannel(QDomElement track, QDomElement mixer, const QString& attribute);

//! What a build made: the tracks' stable ids and the ids of the channels it created for them.
struct Built
{
	std::vector<int> tracks;
	std::vector<int> channels;
};

//! The plan's tracks made in the song, each in its channel (resolved or created); ONE undo step removes the tracks
//! and the channels created for them, its redo makes them again.
Built buildAsOneStep(const TrackPlan& plan);

//! The reply of an import or paste: the new tracks' ids under @a key, the channels created for them, and how many
//! of the @a asked documents made no track (a track type this build cannot create - a newer file's).
QJsonObject madeResult(const QString& key, const Built& built, int asked);

} // namespace lmms::control::transplant

#endif // LMMS_CONTROL_PROJECT_TRANSPLANT_H
