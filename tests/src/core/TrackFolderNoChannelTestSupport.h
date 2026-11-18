/*
 * TrackFolderNoChannelTestSupport.h - construct the state left after an
 * unsuccessful routing-channel restore, for the TrackFolder regression.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING).
 */

#ifndef LMMS_TRACK_FOLDER_NO_CHANNEL_TEST_SUPPORT_H
#define LMMS_TRACK_FOLDER_NO_CHANNEL_TEST_SUPPORT_H

#include <QDomDocument>
#include <QDomElement>

#include "Track.h"
#include "TrackFolder.h"

namespace track_folder_test
{

inline void linkToRoutingFolderWithoutChannel(lmms::TrackFolder* folder, lmms::Track* child)
{
	QDomDocument document;
	QDomElement settings = document.createElement(QStringLiteral("trackfolder"));
	settings.setAttribute(QStringLiteral("mode"), QStringLiteral("routing"));
	folder->loadTrackSpecificSettings(settings);
	child->setParentFolder(folder);
}

} // namespace track_folder_test

#endif // LMMS_TRACK_FOLDER_NO_CHANNEL_TEST_SUPPORT_H
