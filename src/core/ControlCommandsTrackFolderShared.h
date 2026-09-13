/*
 * ControlCommandsTrackFolderShared.h - what the halves of the folder.* command
 *                                      group share (SPEC A11-A16; owner items
 *                                      3+20+21).
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
 */

/* Why this header exists.
 *
 * The folder.* group is what makes a folder track drivable: the edit half
 * (ControlCommandsTrackFolder.cpp: set_folder, the two flags, the mode switch,
 * the state read) and the set half (ControlCommandsTrackFolderSets.cpp: the
 * four track.visibility_set_* verbs). What both halves report is a FOLDER and the
 * tracks it holds, and the three helpers that turn that into the wire shape -
 * the typed folder resolver, a child's own mixer channel, one folder's whole
 * state - are the group's, not one file's.
 *
 * They were inline in the edit half until the edit half reached the 500-line
 * file ratchet. The alternative this codebase has already paid for four times is
 * each half carrying its own copy of the same rule (ControlVocabulary.cpp's
 * header records the duplicate-symbol link failure that ends that way), so the
 * shared half lives here - the same split, for the same reason, as
 * ControlCommandsSessionShared.h for the session.* group.
 *
 * Nothing is exported: `inline` definitions in a named namespace, so every
 * translation unit that includes this header sees one source for each helper and
 * no translation unit depends on another's static initialisation order.
 */

#ifndef LMMS_CONTROL_COMMANDS_TRACK_FOLDER_SHARED_H
#define LMMS_CONTROL_COMMANDS_TRACK_FOLDER_SHARED_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlEdit.h"      // control::resolveTrack(), control::trackTypeNameOf()
#include "ControlRegistry.h"  // ControlResult, ControlErrorKind
#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"
#include "TrackFolder.h"

namespace lmms
{
namespace trackfoldercontrol
{

//! Resolve a `track` argument that must name a FOLDER. A well-formed id naming a
//! track of another type is a typed Refused - the same shape track.set_arm uses -
//! and never a silent no-op.
inline TrackFolder* resolveFolder(const QJsonObject& args, ControlResult* error)
{
	Track* track = control::resolveTrack(args.value(QStringLiteral("track")).toString(), error);
	if (track == nullptr) { return nullptr; }
	if (track->type() != Track::Type::Folder)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("trk-%1 is a %2 track, not a folder: only a folder holds tracks or "
				"carries a mode, a pin or a collapse state")
				.arg(track->id()).arg(control::trackTypeNameOf(track->type())));
		return nullptr;
	}
	return static_cast<TrackFolder*>(track);
}

//! Index of \a track in the SONG container, or -1 when it is not in it (the
//! index the flat `tracks` array of every get_state uses).
inline int trackIndexInSong(Track* track)
{
	const TrackContainer::TrackList& list = Engine::getSong()->tracks();
	for (int i = 0; i < static_cast<int>(list.size()); ++i)
	{
		if (list[i] == track) { return i; }
	}
	return -1;
}

//! Every child's own mixer channel, as the before-state of a mode switch.
inline QJsonArray childChannels(TrackFolder* folder)
{
	QJsonArray channels;
	for (Track* child : folder->children())
	{
		IntModel* model = child->mixerChannelModel();
		QJsonObject entry;
		entry.insert(QStringLiteral("track"), control::trackIdOf(child));
		entry.insert(QStringLiteral("mixer_channel"), model != nullptr ? model->value() : -1);
		channels.append(entry);
	}
	return channels;
}

//! One folder and its children, as every read of the group reports them.
inline QJsonObject folderState(TrackFolder* folder)
{
	QJsonArray children;
	for (Track* child : folder->children())
	{
		QJsonObject entry;
		entry.insert(QStringLiteral("track"), control::trackIdOf(child));
		entry.insert(QStringLiteral("name"), child->name());
		entry.insert(QStringLiteral("type"), control::trackTypeNameOf(child->type()));
		IntModel* model = child->mixerChannelModel();
		// -1 for a child with no mixer channel of its own (an automation track):
		// routing mode leaves such a child where it is, and the read says so
		// rather than inventing a channel it does not have.
		entry.insert(QStringLiteral("mixer_channel"), model != nullptr ? model->value() : -1);
		children.append(entry);
	}

	QJsonObject state;
	state.insert(QStringLiteral("track"), control::trackIdOf(folder));
	state.insert(QStringLiteral("name"), folder->name());
	state.insert(QStringLiteral("mode"), folder->isRouting()
		? QStringLiteral("routing") : QStringLiteral("group"));
	state.insert(QStringLiteral("routing"), folder->isRouting());
	state.insert(QStringLiteral("collapsed"), folder->isCollapsed());
	state.insert(QStringLiteral("pinned"), folder->isPinned());
	state.insert(QStringLiteral("mixer_channel"), static_cast<int>(folder->mixerChannel()));
	state.insert(QStringLiteral("child_count"), folder->childCount());
	state.insert(QStringLiteral("children"), children);
	return state;
}

} // namespace trackfoldercontrol
} // namespace lmms

#endif // LMMS_CONTROL_COMMANDS_TRACK_FOLDER_SHARED_H
